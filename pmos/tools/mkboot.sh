#!/bin/sh
# Build a flashable boot.img for a70q from a linux-samsung-a70q package built by pmbootstrap.
#   pmos/tools/mkboot.sh <kernel pkgrel> <out.img>        e.g. mkboot.sh 15 pmos/out/boot-r15.img
# - kernel: boot/vmlinuz from the apk in the pmbootstrap work volume (a70q-pmbwork)
# - ramdisk: $INITRAMFS (pmOS initramfs; default: the one extracted from the first rootfs install,
#   regenerate with `pmos/pmb/run.sh install ...` -> chroot_rootfs_samsung-a70q/boot/initramfs)
# - cmdline: deviceinfo_kernel_cmdline + the pmOS partition UUIDs of the installed rootfs
#   (PMOS_UUIDS; read them from /proc/cmdline on the phone if the rootfs is reinstalled)
# - header: copied from a working LOS boot image (--like) so Samsung's anti-rollback check
#   (os_version 15.0.0 / SPL 2026-06) passes; pmbootstrap's own boot.img has os_version 0
set -e
REL=$1; OUT=$2
[ -n "$REL" ] && [ -n "$OUT" ] || { sed -n '2,11p' "$0"; exit 2; }
REPO=$(cd "$(dirname "$0")/../.." && pwd)
INITRAMFS=${INITRAMFS:-$REPO/pmos/out/pmos-r1/initramfs}
LIKE=${LIKE:-$REPO/../a70-root/artifacts/boot/magisk_patched.img}
PMOS_UUIDS=${PMOS_UUIDS:-"pmos_boot_uuid=17828e27-32f3-47d0-949a-a13eb62f5c86 pmos_root_uuid=69378177-a3b7-4073-9983-eaf3fc5103d0 pmos_rootfsopts=defaults"}
. "$REPO/pmos/pmaports/device/downstream/device-samsung-a70q/deviceinfo"
T=$(mktemp -d)
docker run --rm -v a70q-pmbwork:/w -v "$T":/o alpine sh -c \
  "tar -xzf /w/packages/edge/aarch64/linux-samsung-a70q-4.14.190-r$REL.apk -C /o boot/vmlinuz 2>/dev/null; test -s /o/boot/vmlinuz"
python3 "$REPO/pmos/tools/bootimg.py" pack --kernel "$T/boot/vmlinuz" --ramdisk "$INITRAMFS" \
  --like "$LIKE" --cmdline "$deviceinfo_kernel_cmdline  $PMOS_UUIDS" -o "$OUT"
rm -rf "$T"
