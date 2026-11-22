#!/usr/bin/env bash
# Install the daily-use mainline image (mkboot-pmos.sh: mainline kernel + regular pmOS initramfs)
# into RECOVERY. BOOT keeps the downstream kernel; which one boots by default is chosen on the phone
# with `a70q-boot-default mainline|downstream` (device package r74+).
#   pmos/mainline/deploy-daily.sh            build out/boot-mainline.img and write it to RECOVERY
# Never write a mainline image to BOOT: the bootloader applies the dtbo partition overlays there
# and the phone hangs at the Samsung logo (needs Download Mode to recover).
# Env: HOST (default 172.16.42.1), SSHPW; the kernel modules must already be on the phone
# (/lib/modules/7.3.4-a70q, from out/modules.tar.gz).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
W="$REPO/pmos/out/work"
export KH="$W/known_hosts" HOST="${HOST:-172.16.42.1}" SSHPW="${SSHPW:?set SSHPW}"
IMG="$HERE/out/boot-mainline.img"
"$HERE/mkboot-pmos.sh" "$IMG"
sum=$(shasum -a 256 "$IMG" | cut -d' ' -f1)
"$W/scpw.exp" "$IMG" "user@$HOST:/tmp/boot-mainline.img" >/dev/null
"$W/sshh.exp" "echo $SSHPW | sudo -S -p '' sh -c '
  set -e
  [ \"\$(sha256sum /tmp/boot-mainline.img | cut -d\" \" -f1)\" = $sum ]
  dd if=/tmp/boot-mainline.img of=/dev/disk/by-partlabel/recovery bs=4M conv=fsync 2>/dev/null
  n=\$(stat -c %s /tmp/boot-mainline.img)
  [ \"\$(head -c \$n /dev/disk/by-partlabel/recovery | sha256sum | cut -d\" \" -f1)\" = $sum ]
  echo RECOVERY=daily-mainline-ok'" 2>&1 | grep -E "RECOVERY=|rror"
