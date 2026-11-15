#!/usr/bin/env bash
# Pack the mainline kernel (out/Image.gz + out/sm6150-samsung-a70q.dtb) into a boot image for the
# RECOVERY partition (milestone-1 test: normal boot stays the downstream pmOS).
#   pmos/mainline/mkboot.sh [out.img]       default pmos/mainline/out/recovery-mainline.img
# Header (os_version/SPL, addresses) copied from the original recovery image (backups/full/
# recovery.img) for Samsung's anti-rollback check (ERRORS C12); the DTB is appended to the kernel
# (the downstream boot images use Image.gz-dtb the same way); recovery_dtbo is left empty.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/out/recovery-mainline.img}"
LIKE="${LIKE:-$REPO/backups/full/recovery.img}"
CMDLINE="${CMDLINE:-console=tty0 loglevel=8 ignore_loglevel printk.devkmsg=on panic=10 oops=panic softlockup_panic=1 printk.always_kmsg_dump=1 reboot=warm rdinit=/init}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
docker run --rm -v "$T":/o -v "$HERE":/a alpine:3.22 sh -ec '
apk add -q busybox-static cpio >/dev/null
mkdir -p /r/bin /r/proc /r/sys /r/dev
cp /bin/busybox.static /r/bin/busybox
cp /a/init-test /r/init; chmod 755 /r/init
cd /r && find . | cpio -o -H newc 2>/dev/null | gzip -9 > /o/initramfs.gz'
cat "$HERE/out/Image.gz" "$HERE/out/sm6150-samsung-a70q.dtb" > "$T/Image.gz-dtb"
python3 "$REPO/pmos/tools/bootimg.py" pack --kernel "$T/Image.gz-dtb" --ramdisk "$T/initramfs.gz" \
	--like "$LIKE" --cmdline "$CMDLINE" -o "$OUT"
python3 "$REPO/pmos/tools/bootimg.py" info "$OUT" | grep -E "os_version/spl|cmdline length|kernel_size|ramdisk_size"
