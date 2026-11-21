// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi taoyao charger diagnostics, reconstructed from the stock kernel. */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include "qti_battery_taoyao_miev.h"

#define MIEV_MAX_MSG 4096
static DEFINE_KFIFO(miev_fifo, u8, 65536);
static DEFINE_SPINLOCK(miev_lock);
static DECLARE_WAIT_QUEUE_HEAD(miev_wait);

struct taoyao_dfx_event {
	u32 id;
	const char *name;
	const char *key1;
	const char *key2;
	bool stats;
};

/* IDs, field names and event types are taken from the stock callback. */
static const struct taoyao_dfx_event dfx_events[] = {
	[1] = { 0x362e412c, "PdAuthFail", "adapterId" },
	[2] = { 0x362e412d, "CpEnFail", "cpId" },
	[3] = { 0x362e4511, "NoneStandartChg" },
	[4] = { 0x362e4512, "CorrosionDischarge" },
	[5] = { 0x362e4513, "LpdDetected", "lpdFlag" },
	[6] = { 0x362e4514, "CpVbusOvp" },
	[7] = { 0x362e4515, "CpIbusOcp" },
	[8] = { 0x362e4516, "CpVbatOvp", "vbat", "vbatMax" },
	[9] = { 0x362e4517, "CpIbatOcp" },
	[10] = { 0x362e4518, "CpVacOvp" },
	[11] = { 0x362e451c, "Antiburn", "tconn" },
	[12] = { 0x362e48f9, "chgBattCycle", "cycleCnt", NULL, true },
	[13] = { 0x362e48fa, "SocNotFull", "vbat", "soc" },
	[14] = { 0x362e48fc, "SmartEnduratTrig", "soc", NULL, true },
	[15] = { 0x362e48fc, "SmartNaviTrig", "soc", NULL, true },
	[16] = { 0x362e50c9, "FgI2cErr", "soc" },
	[17] = { 0x362e50ca, "CpI2cErr" },
	[18] = { 0x362e50cb, "BattLinkerAbsent" },
	[19] = { 0x362e50cc, "CpTdieHot", "masterTdie", NULL, true },
	[20] = { 0x362e50cf, "NotChgInLowTemp", "tbat" },
	[21] = { 0x362e50d0, "NotChgInHighTemp", "tbat" },
	[22] = { 0x362e50d1, "DualBattLinkerAbsent", "status" },
	[23] = { 0x362e54ba, "SmartEnduraSocErr", "soc", NULL, true },
	[24] = { 0x362e54bb, "SmartNaviSocErr", "soc", NULL, true },
	[25] = { 0x362e5899, "BattAuthFail" },
	[26] = { 0x362e589a, "chgBattAuthFail", "status" },
	[27] = { 0x362e6069, "TbatHot", "tbat", "tbatMax", true },
	[28] = { 0x362e606a, "TbatCold", "tbat", "tbatMin", true },
	[29] = { 0x362e606b, "AntiFail" },
	[30] = { 0x362e73f2, "DualVbatDiff", "chgBaseBattVol", "chgFlipBattVol" },
	[31] = { 0x362e6839, "WlsFastChgFail" },
	[32] = { 0x362e683a, "WlsQLow", "chgQBase", "chgQReal" },
	[33] = { 0x362e6c21, "WlsRxOTP", "chgRxTemp" },
	[34] = { 0x362e6c22, "WlsRxOVP" },
	[35] = { 0x362e6c23, "WlsRxOCP" },
	[36] = { 0x362e6c24, "WlsTrxFod" },
	[37] = { 0x362e6c25, "WlsTrxOCP" },
	[38] = { 0x362e6c26, "WlsTrxUVLO" },
	[39] = { 0x362e7009, "WlsTrxI2cErr" },
	[40] = { 0x362e700c, "wlsRxI2CErr" },
};

static void miev_discard(unsigned int count)
{
	u8 scratch[128];

	while (count) {
		unsigned int n = min_t(unsigned int, count, sizeof(scratch));

		if (kfifo_out(&miev_fifo, scratch, n) != n) {
			kfifo_reset(&miev_fifo);
			return;
		}
		count -= n;
	}
}

static int miev_enqueue(const char *data, u32 len)
{
	unsigned long flags;
	u32 old;
	unsigned int evictions = 0;

	if (!len || len > MIEV_MAX_MSG)
		return -EINVAL;
	spin_lock_irqsave(&miev_lock, flags);
	while (kfifo_avail(&miev_fifo) < sizeof(len) + len) {
		if (++evictions > 2048 ||
		    kfifo_out(&miev_fifo, (u8 *)&old, sizeof(old)) != sizeof(old) ||
		    !old || old > MIEV_MAX_MSG || old > kfifo_len(&miev_fifo)) {
			kfifo_reset(&miev_fifo);
			break;
		}
		miev_discard(old);
	}
	if (kfifo_in(&miev_fifo, (u8 *)&len, sizeof(len)) != sizeof(len) ||
	    kfifo_in(&miev_fifo, data, len) != len) {
		kfifo_reset(&miev_fifo);
		spin_unlock_irqrestore(&miev_lock, flags);
		return -EIO;
	}
	spin_unlock_irqrestore(&miev_lock, flags);
	wake_up_interruptible(&miev_wait);
	return 0;
}

void taoyao_miev_dfx_report(u8 event, s32 value1, s32 value2)
{
	const struct taoyao_dfx_event *d;
	char msg[512];
	int len;

	if (!event || event >= ARRAY_SIZE(dfx_events) || !dfx_events[event].name)
		return;
	d = &dfx_events[event];
	len = scnprintf(msg, sizeof(msg), "EventId %u -t %lld -paraList {\"%s\":\"%s\"",
			d->id, (long long)ktime_get_real_seconds(),
			d->stats ? "chgStatInfo" : "chgErrInfo", d->name);
	if (d->key1)
		len += scnprintf(msg + len, sizeof(msg) - len, ",\"%s\":%ld",
				 d->key1, (long)value1);
	if (d->key2)
		len += scnprintf(msg + len, sizeof(msg) - len, ",\"%s\":%ld",
				 d->key2, (long)value2);
	len += scnprintf(msg + len, sizeof(msg) - len, "}");
	miev_enqueue(msg, len);
}

static ssize_t miev_read(struct file *file, char __user *buf, size_t count,
			 loff_t *offset)
{
	unsigned long flags;
	char *msg;
	u32 len;
	ssize_t ret;

	if (!count)
		return 0;
	msg = kmalloc(MIEV_MAX_MSG, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	spin_lock_irqsave(&miev_lock, flags);
	if (kfifo_len(&miev_fifo) < sizeof(len)) {
		ret = 0;
		goto out_unlock;
	}
	if (kfifo_out_peek(&miev_fifo, (u8 *)&len, sizeof(len)) != sizeof(len) ||
	    !len || len > MIEV_MAX_MSG || kfifo_len(&miev_fifo) < len + sizeof(len)) {
		kfifo_reset(&miev_fifo);
		ret = -EIO;
		goto out_unlock;
	}
	if (count < len) {
		ret = -EMSGSIZE;
		goto out_unlock;
	}
	if (kfifo_out(&miev_fifo, (u8 *)&len, sizeof(len)) != sizeof(len) ||
	    kfifo_out(&miev_fifo, msg, len) != len) {
		kfifo_reset(&miev_fifo);
		ret = -EIO;
		goto out_unlock;
	}
	spin_unlock_irqrestore(&miev_lock, flags);
	ret = copy_to_user(buf, msg, len) ? -EFAULT : (ssize_t)len;
	kfree(msg);
	return ret;
out_unlock:
	spin_unlock_irqrestore(&miev_lock, flags);
	kfree(msg);
	return ret;
}

static ssize_t miev_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *offset)
{
	char *msg;
	int rc;

	count = min_t(size_t, count, MIEV_MAX_MSG);
	if (!count)
		return 0;
	msg = memdup_user(buf, count);
	if (IS_ERR(msg))
		return PTR_ERR(msg);
	rc = miev_enqueue(msg, count);
	kfree(msg);
	if (rc)
		return rc;
	return count;
}

static __poll_t miev_poll(struct file *file, poll_table *wait)
{
	unsigned long flags;
	__poll_t mask = 0;

	poll_wait(file, &miev_wait, wait);
	spin_lock_irqsave(&miev_lock, flags);
	if (!kfifo_is_empty(&miev_fifo))
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&miev_lock, flags);
	return mask;
}

static long miev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if ((cmd & 0xfffc) != 0x6b00)
		return -EINVAL;
	if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;
	switch (cmd) {
	case 0x6b01:
	case 0x80046b02:
	case 0x40046b03:
		return 0;
	default:
		return -ENOTTY;
	}
}

static int miev_release(struct inode *inode, struct file *file)
{
	wake_up_interruptible(&miev_wait);
	return 0;
}

static const struct file_operations miev_fops = {
	.owner = THIS_MODULE,
	.read = miev_read,
	.write = miev_write,
	.poll = miev_poll,
	.unlocked_ioctl = miev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = miev_ioctl,
#endif
	.release = miev_release,
	.llseek = no_llseek,
};

static struct miscdevice miev_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "miev",
	.fops = &miev_fops,
};

static int __init taoyao_miev_init(void)
{
	return misc_register(&miev_device);
}
device_initcall(taoyao_miev_init);
