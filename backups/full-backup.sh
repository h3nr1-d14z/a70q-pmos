#!/bin/bash
# Full partition backup for a70q — read-only dd via su, base64 pipe (PTY-safe)
# Verify: size must EXACTLY match blockdev --getsize64; sha256 recorded.
set -u
ADB="${ADB:-adb -s 100.82.206.67:40735}"
DIR="backups/full"
mkdir -p "$DIR"

PARTS="abl aop apdp apnhlos bksecapp bluetooth boot bota btd cmnlib cmnlib64 ddr debug devcfg devinfo dpo dqmdbg dsp dtbo efs fota fsc fsg hyp keymaster keystore limits logfs misc modem modemst1 modemst2 msadp param persist persistent pmic qupfw recovery sec_efs secdata ssd steady storsec tz uefisecapp vbmeta vk xbl xbl_config"

# 0) full lshal dump (read-only reference: Android HALs running on the vendor)
$ADB shell su -c 'lshal' > backups/hals-lshal.txt 2>/dev/null
echo "lshal lines: $(wc -l < backups/hals-lshal.txt | tr -d ' ')"

# 1) probe sizes -> sizes file (name<TAB>size)
: > backups/sizes.txt
FAILPROBE=0
for p in $PARTS; do
  n=$($ADB shell su -c "blockdev --getsize64 /dev/block/by-name/$p 2>/dev/null" | tr -d '\r[:space:]')
  if [ -n "$n" ] && [ "$n" = "$([ -n "$n" ] && echo "$n" | tr -d '\r[:space:]' && echo)" ] 2>/dev/null && printf '%s' "$n" | grep -qE '^[0-9]+$'; then
    printf '%s %s\n' "$p" "$n" >> backups/sizes.txt
  else
    echo "PROBE-FAIL: $p (got: '$n')"
    FAILPROBE=1
  fi
done
if [ $FAILPROBE -ne 0 ]; then echo "ABORT: size probe failed"; exit 1; fi
NROWS=$(wc -l < backups/sizes.txt | tr -d ' ')
TOTAL=$(awk '{s+=$2} END {print s}' backups/sizes.txt)
echo "sizes ok: $NROWS partitions, total $((TOTAL/1000000)) MB"

# 2) pull + verify
FAILS=""
: > backups/manifest-rows.txt
for p in $PARTS; do
  EXP=$(awk -v n="$p" '$1==n {print $2}' backups/sizes.txt)
  OUT="$DIR/$p.img"
  $ADB shell su -c "dd if=/dev/block/by-name/$p 2>/dev/null | base64 -w 0" | base64 -d > "$OUT"
  GOT=$(stat -f%z "$OUT")
  SHA=$(shasum -a 256 "$OUT" | cut -c1-64)
  if [ "$GOT" = "$EXP" ]; then
    echo "$p: $GOT OK"
    echo "| $p | $GOT | \`${SHA:0:8}\` |" >> backups/manifest-rows.txt
  else
    echo "$p: GOT $GOT EXPECT $EXP MISMATCH"
    echo "| $p | $GOT (EXPECT $EXP) | \`${SHA:0:8}\` |" >> backups/manifest-rows.txt
    FAILS="$FAILS $p"
  fi
done

# 3) manifest
{
  echo "# Manifest backup FULL a70q - $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo ""
  echo "Device: SM-A705F/DS XXV, LineageOS 22.2, vendor A11 DXD2"
  echo "Phuong thuc: dd | base64 qua su (adb wireless), verify size + sha256"
  echo "Skip (reinstall duoc): system, vendor, product, userdata, cache, hidden, omr, pad"
  echo ""
  echo "| partition | size (bytes) | sha256 (8 char dau) |"
  echo "|---|---|---|"
  cat backups/manifest-rows.txt
  echo ""
  echo "lshal full dump: backups/hals-lshal.txt"
  echo "MISMATCH: ${FAILS:-KHONG - tat ca khop 100%}"
} > backups/MANIFEST.md
rm -f backups/manifest-rows.txt backups/sizes.txt

echo ""
if [ -z "$FAILS" ]; then
  echo "DONE: tat ca khop 100%, manifest tai backups/MANIFEST.md"
else
  echo "DONE VOI MISMATCH:$FAILS — kiem tra lai cac partition nay"
  exit 2
fi