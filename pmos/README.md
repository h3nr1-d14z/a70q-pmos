# pmOS cho Galaxy A70 (a70q): hướng dẫn kỹ thuật

Kernel downstream 4.14.190 (Samsung/LineageOS) build từ source, rootfs postmarketOS edge (OpenRC)
trong phân vùng USERDATA, giao diện Sxmo (sway). Lịch sử và nguyên nhân từng lỗi: `../ERRORS.md`
(mục F trở đi), nhật ký: `../LOG.md`.

## 1. Trạng thái máy hiện tại (2026-11-14)

| Thành phần | Phiên bản đang chạy | Nguồn |
|---|---|---|
| BOOT | kernel `linux-samsung-a70q` r15 + initramfs pmOS | `tools/mkboot.sh 15 ...` |
| DTBO | DTBO gốc bỏ connector writeback | `tools/dtbo-drop-wb.sh ../backups/dtbo.img ...` |
| VBMETA | flags=2 (tắt verify) | xem mục 4 |
| USERDATA | rootfs pmOS (GPT con, sector 4096) | `pmb/run.sh install` |
| `device-samsung-a70q` | r16 | `pmaports/device/downstream/` |
| `a70q-qcom-userspace` | r0 | như trên |
| `mesa-vulkan-freedreno-kgsl` | r1 | như trên |
| UI | `postmarketos-ui-sxmo-de-sway` (cài bằng apk sau khi flash) | Alpine/pmOS |

Package tự build được cài trên máy bằng `apk add --allow-untrusted <file>.apk` (không flash lại rootfs).

## 2. Truy cập

- USB (cổng USB-C **phía sau** Mac; cổng trước để lại node Download Mode "ma", ERRORS C23): mạng ECM,
  `ssh user@172.16.42.1`
- WiFi: NetworkManager quản lý `wlan0` (`nmcli`); máy tự kết nối mạng đã lưu khi boot
- Mật khẩu tạm của `user`: `147147` (đặt lúc `pmbootstrap install`, nên đổi)
- Về Download Mode từ xa: `sudo reboot-mode download`. Gọi qua SSH thì phiên SSH treo vì máy reboot
  giữa chừng → chạy lệnh ở nền phía Mac rồi chờ thiết bị DM (ERRORS C44, C50)

## 3. Build

Mọi thứ chạy trong Docker trên Mac Apple Silicon (aarch64 native, không cần cross-compile).

```sh
# package (pmbootstrap 3.12.1 trong container; volume a70q-pmbwork)
pmos/pmb/run.sh build --arch aarch64 linux-samsung-a70q device-samsung-a70q
# sha512sums: `pmb/run.sh checksum` chỉ sửa bản trong work volume, không sửa repo -> tự cập nhật trong APKBUILD

# boot.img từ package kernel r<N>
pmos/tools/mkboot.sh <N> pmos/out/boot-r<N>.img

# DTBO không có writeback connector
pmos/tools/dtbo-drop-wb.sh backups/dtbo.img pmos/out/dtbo-nowb.img
```

- `pmb/run.sh`: tạo config pmbootstrap (device samsung-a70q, `service_manager = openrc` vì systemd cần
  kernel ≥5.4, `ui = none`), chạy vòng mknod cho loop partition (container không có udev), đồng bộ
  `pmaports/device/downstream/*` vào pmaports trong work volume
- `kernel/build.sh <variant> [fragment]`: build kernel nhanh ngoài pmbootstrap (volume `a70q-ksrc`,
  clang 19, dtc 1.4.4 `-H epapr` cho DTB giống từng byte bản LOS); `NOPATCH=1` để build source gốc
- `mesa/build.sh`: build thử turnip-kgsl (~1 phút)
- `tools/mkboot.sh`: kernel từ apk + initramfs + cmdline (deviceinfo + UUID phân vùng rootfs) + header
  copy từ boot LOS (`--like`) để qua anti-rollback của Samsung (os_version 15.0.0, SPL 2026-06).
  Initramfs mặc định là `pmos/out/pmos-r1/initramfs` (lấy từ `chroot_rootfs_samsung-a70q/boot/` sau lần
  install đầu; `out/` không nằm trong git). Cài lại rootfs thì UUID đổi → đặt `PMOS_UUIDS`

## 4. Flash

`tools/flash.sh` bọc heimdall: chờ Download Mode bằng IORegistry (không dùng `heimdall detect`), thử lại
khi lần bắt tay đầu bị treo, luôn ghi log `/tmp/a70q-flash-*.log`.

```sh
pmos/tools/flash.sh --BOOT pmos/out/boot-r15.img                 # cập nhật kernel
pmos/tools/flash.sh --DTBO pmos/out/dtbo-nowb.img                # DTBO
```

Cài mới từ đầu (xoá dữ liệu):
1. `pmos/pmb/run.sh install --password <pw>` → `samsung-a70q.img`; `img2simg` thành sparse (heimdall
   chết ở 7% với image thô, ERRORS C33)
2. vbmeta tắt verify: `avbtool make_vbmeta_image --flags 2 --output vbmeta-disabled.img`
3. `flash.sh --BOOT <boot.img> --DTBO <dtbo.img> --VBMETA vbmeta-disabled.img --USERDATA samsung-a70q.sparse.img`
4. Sau khi boot: `apk add postmarketos-ui-sxmo-de-sway`, cài các package tự build, cấu hình WiFi bằng `nmcli`

Quay về LineageOS: flash `../a70-root/artifacts/boot/lineage-boot-orig.img` và `backups/dtbo.img`
(USERDATA đã bị pmOS chiếm, cần format lại trong recovery).

## 5. Kernel

Source `a70q-lineage/android_kernel_samsung_sm6150` nhánh `lineage-22.2` @ `699d9d7da` (trùng với blob
LOS: defconfig == IKCONFIG, DTB giống từng byte). Config = `a70q_defconfig` + fragment k1 (devtmpfs),
k2 (USB ECM), k3 (NLS, SYSVIPC, VT, DRM fbdev, user ns, tắt `FW_LOADER_USER_HELPER_FALLBACK`).

| Patch | Sửa gì |
|---|---|
| 0001 | `u_ether`: gửi lại RX request → USB ECM không đứng sau ~10 gói (C29) |
| 0002–0004 | loop device: block size 4K cho rootfs trên UFS (rescan, xoá page cache, `LOOP_CONFIGURE` đặt queue block size) (C35) |
| 0006 | `sec_battery`: không vào chế độ LPM khi boot từ màn hình sạc (mất cảm ứng) (C45) |
| 0008 | MBHC: thêm kcontrol `Headphone Jack` / `Headset Mic Jack` cho PipeWire (C48) |

`kernel/patches/disabled/`: 0005 (fbdev → bootloop), 0007 (writeback trong DT gốc, không có tác dụng vì
node nằm trong DTBO).

## 6. Các thành phần

**Modem + WiFi** (service OpenRC `a70q-modem` → `/usr/libexec/a70q/a70q-modem-up`, log
`/var/log/a70q-modem.log`): mount firmware apnhlos/modem → qrtr-ns → rmtfs (EFS chỉ đọc), pd-mapper,
tqftpserv → boot modem (`/dev/subsys_modem`) → gửi BDF WLAN qua QMI (`a70q-bdf`) → `boot_wlan` + `ON`
→ `wlan0`. Kernel 4.14 không có QRTR name service và remoteproc class nên các tool linux-msm được vá
(`a70q-qcom-userspace`). Chi tiết: ERRORS mục I và C43.

**Màn hình + Sxmo** (C38, C45, C49): DRM/KMS của SDE (`card0-DSI-1`), wlroots render bằng pixman
(`/etc/profile.d/a70q-wlroots.sh`). Device profile Sxmo cho compatible `qcom,sm6150p-idp`.

**GPU** (C47): driver kgsl (`/dev/kgsl-3d0`), không có DRM render node. `mesa-vulkan-freedreno-kgsl`
(turnip `-Dfreedreno-kmds=msm,kgsl`) cho Vulkan; OpenGL/GLES qua Zink. Biến môi trường trong
`a70q-wlroots.sh`: `TU_DEBUG=noconform` (A612 không có multiview → chỉ Vulkan 1.0), `MESA_VK_WSI_DEBUG=sw`
(compositor pixman không có linux-dmabuf), `GALLIUM_DRIVER=zink`. Firmware GPU (`a630_sqe.fw`,
`a612_rgmu.bin`) chép từ phân vùng vendor lúc boot. Compositor chưa dùng GPU.

**Âm thanh** (C48): `a70q-modem-up` boot ADSP → card q6 `sm6150-idp-snd-card` (WCD937x + WSA881x).
UCM `/usr/share/alsa/ucm2/Samsung/a70q/` (thiết bị Speaker, Headphones, Earpiece, Mic, HeadsetMic);
PipeWire chọn loa/tai nghe theo jack.

**Suspend + nút nguồn** (C51, C52): Sxmo tự suspend 3 s sau khi tắt màn hình; nút nguồn đánh thức và
bật màn hình. Các mảnh ghép trong `device-samsung-a70q`:
- `a70q-modem-up`: `lpm_levels sleep_disabled=0`, WiFi WoWLAN `magic-packet` (WiFi giữ kết nối khi ngủ)
- `/etc/elogind/logind.conf.d/50-a70q.conf`: elogind bỏ qua phím nguồn/nắp (mặc định nó tắt máy)
- deviceprofile Sxmo: `SXMO_NO_MODEM=1`, thêm `/usr/share/a70q` vào `XDG_DATA_DIRS` để Sxmo dùng
  `/usr/share/a70q/sxmo/default_hooks/{wlopm,sxmo_hook_postwake.sh}` (bật màn hình có thử lại;
  bật màn hình khi máy được nút nguồn đánh thức)
- Cắm USB thì không suspend được (wakelock `usb_notify`, `a600000.ssusb`)

## 7. Việc còn lại

- Compositor dùng GPU (cần chia sẻ buffer giữa kgsl và SDE)
- C30: debug-shell mất IP ECM
- Chưa làm: gọi điện/SMS, camera, Bluetooth, cảm biến
