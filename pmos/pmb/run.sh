#!/usr/bin/env bash
# Run pmbootstrap in the a70q-pmb container. Syncs pmos/pmaports/* into the work pmaports clone.
#   pmos/pmb/run.sh <pmbootstrap args...>     e.g. run.sh build linux-samsung-a70q
#   pmos/pmb/run.sh --shell                   interactive shell
# Environment:
#   PMB_VOLUME   docker volume holding the pmbootstrap work dir (default a70q-pmbwork)
#   PMB_UI       pmbootstrap ui for `install` (default none; e.g. sxmo-de-sway)
#   PMB_MIRROR   postmarketOS mirror, e.g. http://ftp.halifax.rwth-aachen.de/postmarketos/ when the
#                default mirror redirect is slow from the container (INSTALL.md)
# The docker image a70q-pmb (pmos/pmb/Dockerfile) is built on first use; the pmaports clone is
# created on first use too.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
W=/home/pmos/.local/var/pmbootstrap
VOL="${PMB_VOLUME:-a70q-pmbwork}"
UI="${PMB_UI:-none}"
MIRROR="${PMB_MIRROR:-}"
TTY=(); [ -t 0 ] && TTY=(-it)
docker image inspect a70q-pmb >/dev/null 2>&1 || docker build -t a70q-pmb "$HERE"
if [ "${1:-}" = "--shell" ]; then CMD='exec bash'; else CMD='pmbootstrap -c $W/pmbootstrap_v3.cfg -y "$@"'; fi
docker run --rm ${TTY[@]+"${TTY[@]}"} --privileged \
  -v "$VOL":$W -v "$REPO/pmos":/repo-pmos -e W=$W -e UI="$UI" -e MIRROR="$MIRROR" \
  a70q-pmb bash -c '
set -e
sudo chown pmos:pmos $W
[ -f $W/version ] || echo 8 > $W/version   # fresh work dir created without init
# config: device samsung-a70q; UI from $PMB_UI (none = console only)
cat > $W/pmbootstrap_v3.cfg <<CFG
[pmbootstrap]
work = $W
device = samsung-a70q
ui = $UI
service_manager = openrc
user = user
jobs = 10
CFG
# pmbootstrap 3.12 has no --mirror-pmOS any more: mirrors live in the config ([mirrors] pmaports)
[ -z "$MIRROR" ] || printf "\n[mirrors]\npmaports = %s\n" "$MIRROR" >> $W/pmbootstrap_v3.cfg
# no udev in the container: create /dev/loopNpM nodes for partitioned loop devices
( while true; do for d in /sys/block/loop*/loop*p*; do [ -e "$d/dev" ] || continue
    n=/dev/$(basename $d); [ -b $n ] || sudo mknod $n b $(tr : " " < $d/dev); done; sleep 0.3; done ) &
# pmaports clone (pmbootstrap would clone it itself, but we copy our packages into it first)
[ -d $W/cache_git/pmaports/.git ] || git clone -q https://gitlab.postmarketos.org/postmarketOS/pmaports.git $W/cache_git/pmaports
# sync our device packages into pmaports
for d in /repo-pmos/pmaports/device/downstream/*/; do
  n=$(basename $d); rm -rf $W/cache_git/pmaports/device/downstream/$n
  cp -r $d $W/cache_git/pmaports/device/downstream/$n
done
# forked temp/ packages (e.g. libcamera with extra sensor support) replace the pmaports ones
for d in /repo-pmos/pmaports/temp/*/; do
  [ -d "$d" ] || continue
  n=$(basename $d); rm -rf $W/cache_git/pmaports/temp/$n
  cp -r $d $W/cache_git/pmaports/temp/$n
done
'"$CMD"'
' bash "$@"
