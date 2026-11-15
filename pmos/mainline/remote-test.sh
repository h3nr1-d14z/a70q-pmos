#!/bin/sh
# Unattended mainline test from a running pmOS (no Download Mode):
#   copy the image over SSH -> dd to RECOVERY (sha256 checked) -> reboot-mode recovery
#   -> the mainline kernel rewrites the restart reason to "normal" (patch 0010) and resets at the
#      end of the test (or via watchdog/panic) -> pmOS boots again -> fetch /sys/fs/pstore.
# Env: HOST (default 172.16.42.1), SSHPW (sudo/SSH password), IMG (default out/recovery-mainline.img),
#      WAIT (seconds to wait for pmOS to come back, default 420).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
W=$root/pmos/out/work
export KH=$W/known_hosts HOST=${HOST:-172.16.42.1}
: "${SSHPW:?set SSHPW}"
export SSHPW
IMG=${IMG:-$here/out/recovery-mainline.img}
WAIT=${WAIT:-420}
LOG=$here/out/pstore-$(date +%Y%m%d-%H%M%S)
sum=$(shasum -a 256 "$IMG" | cut -d' ' -f1)

ssh_() { "$W/sshh.exp" "$1" 2>&1 | sed '1,/password:/d'; }
up() { nc -z -G 2 "$HOST" 22 >/dev/null 2>&1; }

echo "== copy $(basename "$IMG") ($sum)"
"$W/scpw.exp" "$IMG" "user@$HOST:/tmp/recovery-test.img" >/dev/null
echo "== write RECOVERY"
ssh_ "echo \$SSHPW >/dev/null; echo $SSHPW | sudo -S -p '' sh -c '
  set -e
  [ \"\$(sha256sum /tmp/recovery-test.img | cut -d\" \" -f1)\" = $sum ]
  dd if=/tmp/recovery-test.img of=/dev/disk/by-partlabel/recovery bs=4M conv=fsync 2>/dev/null
  n=\$(stat -c %s /tmp/recovery-test.img)
  [ \"\$(head -c \$n /dev/disk/by-partlabel/recovery | sha256sum | cut -d\" \" -f1)\" = $sum ]
  rm -f /tmp/recovery-test.img; echo written-ok'" | grep -q written-ok || { echo "write failed"; exit 1; }
boot0=$(ssh_ "cat /proc/sys/kernel/random/boot_id")
echo "== reboot to recovery (pmOS boot $boot0)"
ssh_ "echo $SSHPW | sudo -S -p '' sh -c 'sync; (sleep 2; /usr/bin/reboot-mode recovery) >/dev/null 2>&1 &'" >/dev/null
t=0
while up && [ $t -lt 60 ]; do sleep 2; t=$((t + 2)); done
echo "== phone left pmOS; waiting up to ${WAIT}s for it to come back"
t=0
until up; do
	sleep 5; t=$((t + 5))
	[ $t -ge "$WAIT" ] && { echo "pmOS did not come back: still in RECOVERY loop?"; exit 2; }
done
sleep 10
echo "== back in pmOS after ~${t}s; fetching pstore"
mkdir -p "$LOG"
ssh_ "echo $SSHPW | sudo -S -p '' sh -c 'for f in /sys/fs/pstore/*; do [ -f \"\$f\" ] && { echo \"### \$f\"; cat \"\$f\"; }; done; grep -E \"^088f|^08c0|^08c5\" /sys/kernel/debug/regmap/spmi0-00/registers'" > "$LOG/pstore.txt"
echo "== saved $LOG/pstore.txt ($(wc -l < "$LOG/pstore.txt") lines)"
