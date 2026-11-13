#!/usr/bin/env bash
# Safe heimdall flash for a70q on macOS.
#   pmos/tools/flash.sh --BOOT boot.img [--VBMETA vbmeta.img] [--DTBO dtbo.img]
#   FROM_ADB=1 pmos/tools/flash.sh ...   # first `adb reboot download`
#
# Handles the issues observed on Mac mini M4 (see ERRORS.md C23/C24):
#  - adb server is killed before heimdall (no concurrent USB claim)
#  - waits for Download Mode by reading IORegistry only (never `heimdall detect`)
#  - refuses a stale "ghost" DM node on the front-port hub (XHCI@02000000)
#  - first handshake after entering DM often stalls ("pipe is stalled", nothing
#    written): retries exactly once, only if no partition upload had started
set -uo pipefail
HEIMDALL="${HEIMDALL:-$HOME/bin/heimdall}"
[ $# -gt 0 ] || { sed -n 2,4p "$0"; exit 2; }
for a in "$@"; do case "$a" in --*) ;; *) [ -f "$a" ] || { echo "missing file: $a"; exit 2; };; esac; done

if [ "${FROM_ADB:-0}" = 1 ]; then
  adb reboot download || { echo "adb reboot download failed"; exit 1; }
fi
adb kill-server >/dev/null 2>&1 || true

dm_node() {  # prints IORegistry node line of each Samsung DM device (04e8:685d)
  ioreg -p IOUSB -l -w0 | awk '
    function flush(){ if(v&&p) print node; v=0; p=0 }
    /\+-o /{flush(); node=$0}
    /"idVendor" = 1256$/{v=1} /"idProduct" = 26717$/{p=1}
    END{flush()}'
}
echo "waiting for Download Mode (IORegistry only, no detect)..."
for i in $(seq 1 90); do
  n="$(dm_node | grep -v '@021' | head -1)"   # @021xxxxx = front-port hub (ghost-prone)
  [ -n "$n" ] && break
  sleep 2
done
if [ -z "${n:-}" ]; then
  if dm_node | grep -q '@021'; then
    echo "only a DM node on the FRONT hub found — likely a stale ghost (C23)."
    echo "use a REAR USB-C port (or reboot the Mac to reset the front hub)."
  else
    echo "no Download Mode device after 180s"
  fi
  exit 1
fi
echo "DM device: $(echo "$n" | sed 's/^[ |+-o]*//; s/ <class.*//')"

for attempt in 1 2; do
  log="$(mktemp)"
  "$HEIMDALL" flash "$@" 2>&1 | tee "$log" | grep -vE '^Copyright|^This software|^$'
  if grep -q 'Releasing device interface' "$log" && ! grep -qE 'ERROR|FAILED|failed' "$log"; then
    echo "FLASH OK (attempt $attempt)"; rm -f "$log"; exit 0
  fi
  if [ $attempt = 1 ] && grep -q 'Failed to send handshake' "$log" && ! grep -q '^Uploading' "$log"; then
    echo "handshake stalled before any upload (C24) — retrying once"; rm -f "$log"; sleep 1; continue
  fi
  echo "FLASH FAILED (attempt $attempt) — not retrying automatically; log: $log"; exit 1
done
