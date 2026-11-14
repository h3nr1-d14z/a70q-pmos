#!/bin/sh
# Build turnip (Vulkan freedreno) voi backend kgsl cho kernel downstream 4.14 (Adreno 612).
# Alpine chi build -Dfreedreno-kmds=msm,virtio nen khong dung duoc /dev/kgsl-3d0.
# Chay tren host arm64 (Docker aarch64 native). Ket qua: out/libvulkan_freedreno.so
set -e
cd "$(dirname "$0")"
VER=${MESA_VER:-26.2.6}
mkdir -p out
docker run --rm -v "$PWD:/w" -e VER=$VER -e BTYPE -e NOSTRIP alpine:edge sh -euxc '
apk add -q build-base meson ninja python3 py3-mako py3-yaml py3-packaging bison flex \
  glslang libdrm-dev zlib-dev zstd-dev expat-dev wayland-dev wayland-protocols \
  libxshmfence-dev libx11-dev libxext-dev libxrandr-dev libxcb-dev elfutils-dev curl xz
cd /tmp
curl -sL https://archive.mesa3d.org/mesa-$VER.tar.xz | tar xJ
cd mesa-$VER
meson setup b --prefix=/usr --buildtype=${BTYPE:-release} -Db_ndebug=true \
  -Dplatforms=wayland,x11 -Dgallium-drivers= -Dvulkan-drivers=freedreno \
  -Dfreedreno-kmds=msm,kgsl -Dglx=disabled -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false \
  -Dgbm=disabled -Dllvm=disabled -Dvideo-codecs= -Dvulkan-layers=
ninja -C b src/freedreno/vulkan/libvulkan_freedreno.so
cp b/src/freedreno/vulkan/libvulkan_freedreno.so /w/out/
[ -n "${NOSTRIP:-}" ] || strip /w/out/libvulkan_freedreno.so
'
ls -l out
