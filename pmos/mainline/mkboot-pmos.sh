#!/usr/bin/env bash
# Daily-use boot image for the mainline kernel: out/Image.gz + out/sm6150-samsung-a70q.dtb (appended,
# like Image.gz-dtb) with the regular postmarketOS initramfs and the pmOS rootfs UUIDs, so it boots
# straight into the installed pmOS like the downstream image does (no test initramfs, no 15 min reset).
#   pmos/mainline/mkboot-pmos.sh [out.img]     default pmos/mainline/out/boot-mainline.img
# Environment:
#   INITRAMFS   pmOS initramfs (kernel independent; default pmos/out/mainline-pmos/initramfs, copied
#               from /boot/initramfs on the phone)
#   PMOS_UUIDS  as in the downstream BOOT cmdline (pmos_boot_uuid=... pmos_root_uuid=...)
#   LIKE        image whose header (os_version 15.0.0 / SPL) passes Samsung's anti-rollback check
#   EXTRA       appended to the cmdline
# Flash to BOOT (normal boot); keep the downstream image in RECOVERY as the fallback (Vol Up + Power,
# and the bootloader picks RECOVERY by itself after an abnormal reset, e.g. a panic).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$HERE/out/boot-mainline.img}"
INITRAMFS="${INITRAMFS:-$REPO/pmos/out/mainline-pmos/initramfs}"
LIKE="${LIKE:-$REPO/../a70-root/artifacts/boot/magisk_patched.img}"
PMOS_UUIDS="${PMOS_UUIDS:-pmos_boot_uuid=17828e27-32f3-47d0-949a-a13eb62f5c86 pmos_root_uuid=69378177-a3b7-4073-9983-eaf3fc5103d0 pmos_rootfsopts=defaults}"
CMDLINE="console=tty0 panic=10 log_buf_len=1M printk.always_kmsg_dump=1 reboot=warm"
# interconnect/clock sync_state: the RGMU node never binds a driver (see README)
CMDLINE="$CMDLINE fw_devlink.sync_state=timeout msm.separate_gpu_kms=1"
CMDLINE="$CMDLINE firmware_class.path=/vendor/firmware-modem/image loop.max_part=7"
CMDLINE="$CMDLINE $PMOS_UUIDS ${EXTRA:-}"
for f in "$INITRAMFS" "$LIKE" "$HERE/out/Image.gz" "$HERE/out/sm6150-samsung-a70q.dtb"; do
	[ -f "$f" ] || { echo "mkboot-pmos: $f not found" >&2; exit 1; }
done
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cat "$HERE/out/Image.gz" "$HERE/out/sm6150-samsung-a70q.dtb" > "$T/Image.gz-dtb"
python3 "$REPO/pmos/tools/bootimg.py" pack --kernel "$T/Image.gz-dtb" --ramdisk "$INITRAMFS" \
	--like "$LIKE" --cmdline "$CMDLINE" -o "$OUT"
python3 "$REPO/pmos/tools/bootimg.py" info "$OUT" | grep -E "os_version/spl|cmdline length|kernel_size|ramdisk_size"
