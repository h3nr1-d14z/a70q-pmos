#!/usr/bin/env bash
# Build mainline Linux for the Galaxy A70 (a70q) in Docker (native arm64 on Apple Silicon).
#   pmos/mainline/build.sh            -> pmos/mainline/out/{Image.gz,sm6150-samsung-a70q.dtb,config}
# Source: linux-$KVER from kernel.org in the docker volume a70q-mainline; our DTS is copied into
# arch/arm64/boot/dts/qcom/ on every build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
KVER="${KVER:-7.3.4}"
MODULES="${MODULES:-}"   # MODULES=1: also build modules -> out/modules.tar.gz (for the pmOS rootfs)
mkdir -p "$HERE/out"
docker run --rm -v a70q-mainline:/m -v "$HERE":/a alpine:3.22 sh -ec "
apk add -q build-base bc flex bison openssl-dev perl linux-headers elfutils-dev xz findutils python3 curl tar gzip coreutils diffutils patch >/dev/null
cd /m
[ -d linux-$KVER ] || curl -sL https://cdn.kernel.org/pub/linux/kernel/v${KVER%%.*}.x/linux-$KVER.tar.xz | tar xJ
cd linux-$KVER
for p in /a/patches/*.patch; do
	[ -e \"\$p\" ] || continue
	patch -p1 -N -s --dry-run < \"\$p\" >/dev/null 2>&1 && patch -p1 -N -s < \"\$p\" && echo \"applied \$(basename \$p)\" || true
done
cp /a/sm6150-samsung-a70q.dts arch/arm64/boot/dts/qcom/
grep -q sm6150-samsung-a70q arch/arm64/boot/dts/qcom/Makefile ||
	echo 'dtb-\$(CONFIG_ARCH_QCOM) += sm6150-samsung-a70q.dtb' >> arch/arm64/boot/dts/qcom/Makefile
O=/m/build-$KVER
make -s O=\$O ARCH=arm64 defconfig
scripts/kconfig/merge_config.sh -m -O \$O \$O/.config /a/a70q.config >/dev/null
make -s O=\$O ARCH=arm64 olddefconfig
make -s O=\$O ARCH=arm64 -j\$(nproc) Image.gz qcom/sm6150-samsung-a70q.dtb
cp \$O/arch/arm64/boot/Image.gz \$O/arch/arm64/boot/dts/qcom/sm6150-samsung-a70q.dtb /a/out/
cp \$O/.config /a/out/config
cp \$O/System.map /a/out/System.map
if [ -n \"$MODULES\" ]; then
	make -s O=\$O ARCH=arm64 -j\$(nproc) modules
	rm -rf /tmp/mr
	make -s O=\$O ARCH=arm64 INSTALL_MOD_PATH=/tmp/mr INSTALL_MOD_STRIP=1 modules_install
	rel=\$(cat \$O/include/config/kernel.release)
	rm -f /tmp/mr/lib/modules/\$rel/build
	tar -C /tmp/mr -czf /a/out/modules.tar.gz lib/modules/\$rel
	echo modules: \$rel \$(du -sh /tmp/mr | cut -f1)
fi
"
ls -l "$HERE/out"
