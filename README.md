# Arunika Kernel — Xiaomi 12 Lite

[Unduh ZIP](./Arunika-5.4.302-taoyao-20261028-1233.zip?raw=true)
· [SHA256](./Arunika-5.4.302-taoyao-20261028-1233.zip.sha256?raw=true)

Versi kernel: **5.4.302-Arunika**. Identitas pembuat build:
**ammarbahtiarasli@cloud**.

Build ini memakai source commit `3817998114df` yang sama. Identitas
dikonfigurasi melalui `KBUILD_BUILD_USER=ammarbahtiarasli` dan
`KBUILD_BUILD_HOST=cloud`, lalu kernel dikompilasi ulang secara incremental.
Object yang tidak berubah dari compile penuh sebelumnya digunakan kembali;
Image akhir dan object metadata versi dibuat ulang. Konfigurasi, driver,
installer rilis, tool ARM64, serta `LOCALVERSION` tetap sama.

Ditargetkan pada Xiaomi 12 Lite (`taoyao`) dengan Evolution X Android 17.
Log rilis tetap ringkas tanpa label revisi/TEST, dengan detail error ditampilkan
jika instalasi gagal. Metode installer standalone sebelumnya sudah dilaporkan
berhasil flash melalui recovery AOSP Evolution X.

## Instalasi

Pada recovery pilih **Apply update → Apply from ADB**, lalu jalankan:

```sh
adb sideload Arunika-5.4.302-taoyao-20261028-1233.zip
```

Setelah boot, identitas dapat diperiksa dengan:

```sh
adb shell cat /proc/version
```

Awal banner seharusnya:

```text
Linux version 5.4.302-Arunika (ammarbahtiarasli@cloud)
```

Siapkan boot/kernel yang bekerja untuk pemulihan. Paket belum ditandatangani
dengan kunci ROM; recovery mungkin meminta **Install anyway**. Tidak perlu wipe
data. Installer menargetkan boot slot aktif dan mempertahankan ramdisk,
DTB/DTBO, vendor_boot, dan vbmeta yang ada.

## Source dan verifikasi

- [Source kernel 3817998114df](https://github.com/taoyao-labs/kernel_xiaomi_taoyao/tree/3817998114df6cb356038a64f165907f7ea60683)
- [AnyKernel3](https://github.com/osm0sis/AnyKernel3/tree/020dfeccf9d7e962a48400fc94d3e451df92eead)
- [Magisk v31.1, source tool ARM64](https://github.com/topjohnwu/Magisk/tree/0972f67d97f9b6302b67c5171283330c74114b4a)

Compile metadata, banner Image, konfigurasi tertanam, dan validasi ZIP lulus.
Source serta installer identik dengan paket rilis sebelumnya. Record compile
penuh sebelumnya dan record incremental baru ditautkan melalui hash/provenance.
Build baru belum diuji boot pada perangkat; akurasi SOC, konsumsi daya, suhu,
dan FPS tetap memerlukan uji perangkat. Kompatibilitas HyperOS belum diuji.

SHA256 ZIP:

```text
43327e36c4a0463439ba341175129d59a2d2e5fc141d533258767650827bc8da
```
