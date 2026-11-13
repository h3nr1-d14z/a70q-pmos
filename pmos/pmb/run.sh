#!/usr/bin/env bash
# Run pmbootstrap in the a70q-pmb container. Syncs pmos/pmaports/* into the work pmaports clone.
#   pmos/pmb/run.sh <pmbootstrap args...>     e.g. run.sh build linux-samsung-a70q
#   pmos/pmb/run.sh --shell                   interactive shell
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
W=/home/pmos/.local/var/pmbootstrap
TTY=(); [ -t 0 ] && TTY=(-it)
if [ "${1:-}" = "--shell" ]; then CMD='exec bash'; else CMD='pmbootstrap -c $W/pmbootstrap_v3.cfg -y "$@"'; fi
docker run --rm ${TTY[@]+"${TTY[@]}"} --privileged \
  -v a70q-pmbwork:$W -v "$REPO/pmos":/repo-pmos -e W=$W \
  a70q-pmb bash -c '
set -e
sudo chown pmos:pmos $W
[ -f $W/version ] || echo 8 > $W/version   # fresh work dir created without init
# config: device samsung-a70q, no UI (initramfs/console bring-up)
cat > $W/pmbootstrap_v3.cfg <<CFG
[pmbootstrap]
work = $W
device = samsung-a70q
ui = none
user = user
jobs = 10
CFG
# sync our device packages into pmaports
for d in /repo-pmos/pmaports/device/downstream/*/; do
  n=$(basename $d); rm -rf $W/cache_git/pmaports/device/downstream/$n
  cp -r $d $W/cache_git/pmaports/device/downstream/$n
done
'"$CMD"'
' bash "$@"
