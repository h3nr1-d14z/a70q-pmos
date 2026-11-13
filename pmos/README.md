# pmOS cho Galaxy A70 (a70q) — kernel downstream 4.14.190

Chuyển hướng từ Ubuntu Touch/Halium sang postmarketOS (2026-11-13). Mọi số liệu dưới đây tự kiểm chứng.

## Kernel: tái tạo đúng kernel LOS 22.2 đang boot được
- Source: `a70q-lineage/android_kernel_samsung_sm6150` branch `lineage-22.2` @ **`699d9d7da`** (HEAD `f6d944f0` "r1q support" thêm `CONFIG_SEC_R1Q_PROJECT` — không có trong blob LOS)
- Defconfig `a70q_defconfig` == IKCONFIG nhúng trong blob LOS (100%, trừ symbol r1q)
- Build arm64 native (Mac mini M4 → container ubuntu:24.04, clang/lld 19.1.1), volume ext4 `a70q-ksrc`
- dtc: tree hard-code `tools/dtc` (binary x86-64, DTC 1.4.4-Android) → build dtc 1.4.4 từ `scripts/dtc` bằng tay, `-H epapr` → **DTB giống từng byte DTB của blob LOS**
- Image k0 = 45,092,888 B (đúng bằng blob LOS)

| Biến thể | Thay đổi | Kết quả trên máy |
|---|---|---|
| `k0-los-repro` | không | ✅ boot LOS |
| `k1-devtmpfs` | `DEVTMPFS=y` + `DEVTMPFS_MOUNT=y` | ✅ boot LOS, `/proc/version` = `a70q@pmos`, USB adb OK, devtmpfs có |

## Công cụ
- `kernel/Dockerfile`, `kernel/build.sh <variant> [fragment]` → `kernel/out/<variant>/Image.gz-dtb`
- `tools/bootimg.py info|unpack|pack` — boot header v1, không phụ thuộc; `pack --like <boot gốc>` giữ addr/board/cmdline/os_version (anti-rollback 15.0.0/2026-06)
- `tools/flash.sh --BOOT x.img [...]` — heimdall an toàn (ERRORS C23/C24); `FROM_ADB=1` để tự `adb reboot download`

## Quy trình test một kernel
```sh
pmos/kernel/build.sh k1-devtmpfs pmos/kernel/fragments/k1-devtmpfs.config
python3 pmos/tools/bootimg.py unpack ../a70-root/artifacts/boot/magisk_patched.img /tmp/mag
python3 pmos/tools/bootimg.py pack --kernel pmos/kernel/out/k1-devtmpfs/Image.gz-dtb \
    --ramdisk /tmp/mag/ramdisk --like ../a70-root/artifacts/boot/magisk_patched.img -o pmos/out/k1-boot.img
FROM_ADB=1 pmos/tools/flash.sh --BOOT pmos/out/k1-boot.img     # cắm cổng USB-C phía SAU
```
Trên máy: vbmeta = `out/vbmeta-disabled.img` (flags=2), dtbo = `backups/full/dtbo.img` (== dtbo trong zip LOS).
Rollback: `pmos/tools/flash.sh --BOOT backups/full/boot.img` (= magisk_patched, LOS + root).

## pmOS (pmbootstrap) — initramfs chạy, USB networking OK
- `pmb/run.sh <args>`: pmbootstrap 3.12.1 trong container arm64 (`pmb/Dockerfile`, volume `a70q-pmbwork`); đồng bộ `pmaports/device/downstream/*` vào pmaports clone mỗi lần chạy
- `linux-samsung-a70q`: build từ source 699d9d7da + config k2 (k1 + ECM) + patch u_ether (ERRORS C29). Đã kiểm chứng tương đương k1: compatible strings 2757/2757, DTB == LOS
- `device-samsung-a70q`: deviceinfo header v1 + offset LOS, `ecm.usb0`
- boot.img của pmbootstrap có os_version 0 → đóng gói lại bằng `tools/bootimg.py pack --like <boot LOS>` (anti-rollback 15.0.0/2026-06)

```sh
pmos/pmb/run.sh build linux-samsung-a70q device-samsung-a70q
pmos/pmb/run.sh install --no-image --password 147147      # rootfs chroot + initramfs, không đụng máy
# lấy chroot_rootfs_samsung-a70q/boot/{vmlinuz,initramfs} → bootimg.py pack ... --cmdline "<deviceinfo cmdline> pmos.debug-shell"
```
Debug shell: serial `/dev/cu.usbmodempostmarketOS3` (115200), mạng ECM 172.16.42.1 (workaround C30), telnet :23.
Về Download Mode từ shell: upload `tools/src/reboot-mode.c` (static) → `reboot-mode download`.

## ✅ pmOS BOOT HOÀN CHỈNH (2026-11-14) — SSH `user@172.16.42.1` (mật khẩu tạm 147147 — đổi ngay)
- Rootfs OpenRC (`service_manager = openrc`; systemd cần kernel ≥5.4) trong USERDATA dạng subpartition (GPT sector 4096), `ui = none`
- Kernel r8 = config k3 (k2 + NLS_ASCII, SYSVIPC, VT, DRM_FBDEV_EMULATION, USER_NS, FRAMEBUFFER_CONSOLE) + patches 0001–0004
- cmdline phải có `loop.max_part=7`; boot.img đóng gói lại bằng `bootimg.py pack --like <boot LOS>`
- Flash: `flash.sh --BOOT <boot.img> --USERDATA samsung-a70q.sparse.img` (USERDATA phải sparse: `img2simg`)

## Tiếp theo
1. ~~Initramfs pmOS + USB networking~~ ✅
2. ~~Rootfs trên USERDATA → boot pmOS thật, SSH~~ ✅
3. Màn hình (fbcon/DRM), touch, WiFi (WCN3990), pin; đóng gói `reboot-mode` vào initramfs; sửa C30 (debug-shell mất IP)
