#!/usr/bin/env bash
# Quản lý build container Halium a70q trên Mac Apple Silicon (Docker Desktop / OrbStack).
#
# QUY TẮC VÀNG: source tree nằm TRONG named volume ext4 (case-sensitive).
#   KHÔNG BAO GIỜ bind mount thư mục macOS vào /work — APFS case-insensitive
#   làm Android build system gãy theo cách cực khó debug.
#
# Dùng:
#   ./build/run.sh build   # build image (chạy 1 lần, ~5 phút)
#   ./build/run.sh shell   # vào build shell (repo sync / mka ở đây)
#   ./build/run.sh cp X Y  # copy file qua lại: cp /work/out/halium-boot.img out/
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=halium-a70q
VOL_SRC=halium-src        # source tree (ext4 case-sensitive trong Docker VM)
VOL_CACHE=halium-ccache   # ccache

cmd="${1:-shell}"
case "$cmd" in
  build)
    docker build -t "$IMAGE:latest" build/
    ;;
  shell)
    docker volume create "$VOL_SRC" >/dev/null
    docker volume create "$VOL_CACHE" >/dev/null
    mkdir -p out
    exec docker run -it --rm --name halium-a70q \
      --memory="${HALIUM_MEM:-12g}" \
      --cpus="$(sysctl -n hw.ncpu)" \
      -v "$VOL_SRC":/work \
      -v "$VOL_CACHE":/ccache \
      -v "$(pwd)":/handoff:ro \
      -w /work \
      "$IMAGE:latest"
    ;;
  cp)
    # ./build/run.sh cp /work/out/halium-boot.img out/   (copy ra)
    # ./build/run.sh cp backups/full/boot.img /work/     (copy vào)
    shift
    docker volume create "$VOL_SRC" >/dev/null
    docker run --rm \
      -v "$VOL_SRC":/work \
      -v "$(pwd)":/host \
      -e CP_ARGS="$*" \
      "$IMAGE:latest" /bin/bash -c 'cp -v $CP_ARGS'
    ;;
  *)
    echo "dùng: $0 [build|shell|cp SRC DST]" >&2
    exit 1
    ;;
esac
