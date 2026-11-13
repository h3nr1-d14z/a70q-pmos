# Quy trình tiếp tục trên WINDOWS

## Tình trạng máy (đang chờ)
- Máy đang **stuck logo freeze** với kernel gcc-9.4 (image `out/halium-boot.img` hash `26ab381f`) — đã flash bản có DEVTMPFS=y + fbcon + ramoops-DTB + initramfs đầy đủ
- Toàn bộ kernel halium build (clang-13/gcc-9.4) đều **chết siêu sớm** (<1s, trước fbcon/pstore) — chỉ duy nhất LOS kernel chạy được. C20 DEVTMPFS fix = điều kiện cần cho initramfs nhưng chưa chắc đủ
- Misc/pstore đều rỗng (instrumentation đã sửa nhưng kernel chết trước khi init chạy)

## Bước 0 — Khôi phục LOS (máy dùng được lại)
```bash
cd /Volumes/External/Projects/HieuLD/ubuntu-touch-a70  # hoặc đường dẫn Windows nếu ổ exFAT
~/bin/heimdall flash --BOOT backups/full/boot.img --VBMETA backups/full/vbmeta.img --DTBO backups/full/dtbo.img
```
(trên Windows: dùng **heimdall-suite Windows binary** + Samsung USB driver, hoặc **Odin** với 3 file img — flashing qua DM)

## Bước 1 — Đọc bằng chứng còn sót trong misc
LOS boot xong có adb:
```bash
adb shell su -c 'dd if=/dev/block/by-name/misc of=/data/local/tmp/ma.dump bs=4096 count=240'
adb pull /data/local/tmp/ma.dump
# strings ma.dump | grep init-stage — nếu có marker post-mount + dmesg → kernel gcc+devtmpfs đã chạy, đọc tiếp panic
```

## Bước 2 — Build kernel trên Windows

**KHÔNG cần cài WSL2 riêng** — Docker Desktop đã dùng WSL2 backend:
1. `git clone` repo này (private — cần GitHub credentials của bạn)
2. Docker Desktop: `docker run -it ubuntu:20.04 bash` → `apt update && apt install gcc-aarch64-linux-gnu bison flex libssl-dev bc python3 git` → clone kernel rtd1250 + apply patch → build
3. **HOẶC không build gì cả**: kernel blob đã build sẵn `prebuilt/Image.gz-dtb-gcc94-ramoops-dtb.bin` (16.6MB) — chỉ cần package lại nếu đổi cmdline:
   - `pip install mkbootimg` (Windows python3)
   - `mkbootimg --kernel prebuilt/Image.gz-dtb-gcc94-ramoops-dtb.bin --ramdisk patches/initramfs/initramfs-repaired-final.gz --base 0x00000000 --pagesize 4096 --kernel_offset 0x00008000 --ramdisk_offset 0x02000000 --tags_offset 0x01e00000 --second_offset 0x00f00000 --header_version 1 --board SRPRL06C005 --cmdline "console=tty0 androidboot.boot_devices=soc/1d84000.ufshc ramoops.mem_address=0xa1300000 ramoops.mem_size=0x100000 ramoops.console_size=0x40000 ramoops.record_size=0x40000 ramoops.dump_oops=1 panic=0" --os_version 15.0.0 --os_patch_level 2026-06 -o halium-boot.img`
   - (cmdline khuyến nghị: console=tty0 + ramoops + panic=0 — console=tty0 để fbcon hiện log trên AMOLED)
4. Native Windows kbuild KHÔNG hoạt động (kbuild cần POSIX) — dùng WSL2 hoặc Docker như trên

## Lưu ý quan trọng (đã học — chi tiết ERRORS.md)
- `adb kill-server` TRƯỚC heimdall flash; KHÔNG chạy adb/detect trong lúc flash (C21/C22)
- Samsung kernel: DEVTMPFS mặc định TẮT — luôn kiểm `CONFIG_DEVTMPFS=y` (C20)
- mkbootimg cmdline >512 byte = truncation âm thầm (C16); tránh `$` trong cmdline (C17)
- gcc-9 trên kernel này cần vá: mm.h extern-inline, ipa min_t, memshare return type, imagis VLA-memset, audio Kbuild -Werror (đã có trong patches/0006)
- clang-13 (halium tree) VÀ gcc-9.4 đều chết super-early → nghi tree/DTB/soc — era-correct toolchain clang-9 (r3441) là hướng cuối nếu gcc cũng chết
- Instrumentation phải đặt SAU mount /dev (node mới tồn tại — C18)
- misc là flash storage — dữ liệu giữ nguyên qua mọi reboot/tắt máy
