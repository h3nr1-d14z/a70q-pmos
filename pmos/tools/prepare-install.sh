#!/usr/bin/env bash
# Turn a fresh `pmbootstrap install` into the four images flashed with heimdall (INSTALL.md).
#   pmos/tools/prepare-install.sh --like <LineageOS boot.img> --dtbo <LineageOS dtbo.img> [--out DIR]
#
#   --like   boot image the phone currently boots (LineageOS 22.2 boot.img from the same zip that is
#            installed). Its header (os_version / security patch level) is copied: Samsung's
#            bootloader refuses boot images older than the last one it booted (ERRORS C12).
#   --dtbo   dtbo.img of that LineageOS build; rebuilt without the SDE writeback connector (C49).
#
# Output (default pmos/out/install/):
#   boot.img      kernel from the newest linux-samsung-a70q apk + initramfs of the installed rootfs
#                 + cmdline with that rootfs' partition UUIDs
#   dtbo.img      dtbo without the writeback connector
#   vbmeta.img    empty vbmeta with flags=2 (verification disabled)
#   userdata.img  the pmOS image (boot + root partitions) as an Android sparse image for USERDATA
# Environment: PMB_VOLUME (default a70q-pmbwork), as for pmos/pmb/run.sh.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
VOL="${PMB_VOLUME:-a70q-pmbwork}"
OUT="$REPO/pmos/out/install"
LIKE=""; DTBO=""
while [ $# -gt 0 ]; do
	case "$1" in
	--like) LIKE="$2"; shift 2 ;;
	--dtbo) DTBO="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	*) sed -n '2,18p' "$0"; exit 2 ;;
	esac
done
[ -f "$LIKE" ] && [ -f "$DTBO" ] || { sed -n '2,18p' "$0"; exit 2; }
LIKE="$(cd "$(dirname "$LIKE")" && pwd)/$(basename "$LIKE")"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

echo ">> reading the installed rootfs from volume $VOL"
docker run --rm -v "$VOL":/w:ro -v "$T":/o alpine:3.22 sh -ec '
R=/w/chroot_rootfs_samsung-a70q
test -s $R/boot/initramfs || { echo "no rootfs: run pmos/pmb/run.sh install first" >&2; exit 1; }
cp $R/boot/initramfs /o/initramfs
awk "\$2==\"/\" {sub(\"UUID=\",\"\",\$1); print \"root=\" \$1} \$2==\"/boot\" {sub(\"UUID=\",\"\",\$1); print \"boot=\" \$1}" $R/etc/fstab > /o/uuids
# image written by pmbootstrap install (pmbootstrap 3: chroot_native/home/pmos/rootfs/);
# busybox find -size knows only b/c/k suffixes
img=/w/chroot_native/home/pmos/rootfs/samsung-a70q.img
[ -s "$img" ] || img=$(find /w -maxdepth 6 -name "samsung-a70q.img" -size +100000k 2>/dev/null | head -1)
test -n "$img" || { echo "no samsung-a70q.img in the volume (pmbootstrap install output)" >&2; exit 1; }
echo "$img" > /o/imgpath
apk="$(ls /w/packages/edge/aarch64/linux-samsung-a70q-*.apk | sort -V | tail -1)"
basename "$apk" > /o/kernel
'
. "$T/uuids"
[ -n "${root:-}" ] && [ -n "${boot:-}" ] || { echo "could not read UUIDs from the rootfs fstab" >&2; exit 1; }
KREL="$(sed -E 's/.*-r([0-9]+)\.apk/\1/' "$T/kernel")"
echo "   kernel $(cat "$T/kernel"), root UUID $root, boot UUID $boot"
echo "   image $(cat "$T/imgpath")"

echo ">> boot.img"
INITRAMFS="$T/initramfs" LIKE="$LIKE" PMB_VOLUME="$VOL" \
	PMOS_UUIDS="pmos_boot_uuid=$boot pmos_root_uuid=$root pmos_rootfsopts=defaults" \
	"$REPO/pmos/tools/mkboot.sh" "$KREL" "$OUT/boot.img"

echo ">> dtbo.img"
"$REPO/pmos/tools/dtbo-drop-wb.sh" "$DTBO" "$OUT/dtbo.img"

echo ">> vbmeta.img + userdata.img (sparse)"
# android-tools-avbtool exists only in Alpine edge
docker run --rm -v "$VOL":/w:ro -v "$OUT":/out alpine:edge sh -ec "
apk add -q android-tools android-tools-avbtool
avbtool make_vbmeta_image --flags 2 --padding_size 4096 --output /out/vbmeta.img
img2simg \"$(cat "$T/imgpath")\" /out/userdata.img
"
ls -l "$OUT"
cat <<EOF

Flash (phone in Download Mode, see INSTALL.md):
  pmos/tools/flash.sh --BOOT $OUT/boot.img --DTBO $OUT/dtbo.img --VBMETA $OUT/vbmeta.img --USERDATA $OUT/userdata.img
EOF
