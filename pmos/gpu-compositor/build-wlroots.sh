#!/bin/sh
# Build libwlroots-0.20.so (same version/options as Alpine wlroots0.20-0.20.2) with
# wlroots-0001-WLR_DISPLAY_ONLY_DRM.patch, for quick tests on the phone. Output: out/
set -e
cd "$(dirname "$0")"
mkdir -p out
docker run --rm -v "$PWD:/w" alpine:edge sh -euc '
apk add -q build-base meson ninja curl patch eudev-dev glslang-dev hwdata-dev libcap-dev libdisplay-info-dev \
  libinput-dev libseat-dev libliftoff-dev libxcb-dev libxkbcommon-dev lcms2-dev mesa-dev pixman-dev \
  vulkan-loader-dev wayland-dev wayland-protocols xcb-util-image-dev xcb-util-renderutil-dev \
  xcb-util-wm-dev xkeyboard-config-dev xwayland-dev
cd /tmp && curl -sL https://gitlab.freedesktop.org/wlroots/wlroots/-/archive/0.20.2/wlroots-0.20.2.tar.gz | tar xz
cd wlroots-0.20.2
patch -p1 < /w/alpine-e8c983808d8b98a71f991c16c24367494386958b.patch
patch -p1 < /w/wlroots-0001-WLR_DISPLAY_ONLY_DRM.patch
meson setup b --prefix=/usr --buildtype=release -Dexamples=false -Dcolor-management=enabled -Ddefault_library=shared >/dev/null
ninja -C b >/dev/null
cp b/libwlroots-0.20.so /w/out/ && strip /w/out/libwlroots-0.20.so
grep -c WLR_DISPLAY_ONLY_DRM render/allocator/allocator.c render/vulkan/vulkan.c render/vulkan/renderer.c
'
ls -l out/libwlroots-0.20.so
