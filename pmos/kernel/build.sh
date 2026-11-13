#!/usr/bin/env bash
# Build a70q kernel (a70q-lineage lineage-22.2, 4.14.190) natively in arm64 container.
# Usage: pmos/kernel/build.sh <variant> [extra config fragment file]
#   variant: name for output dir (e.g. k0-stock, k1-devtmpfs)
# Output: pmos/kernel/out/<variant>/{Image.gz-dtb,config,build.log}
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VARIANT="${1:?variant}"
FRAG="${2:-}"
COMMIT="${KCOMMIT:-699d9d7da}"   # == source of LOS 22.2 blob (HEAD f6d944f0 r1q adds CONFIG_SEC_R1Q_PROJECT absent in blob)
OUT="$HERE/out/$VARIANT"; mkdir -p "$OUT"
[ -n "$FRAG" ] && cp "$FRAG" "$OUT/fragment.config" || : > "$OUT/fragment.config"

docker run --rm -v a70q-ksrc:/src -v "$OUT":/out a70q-kbuild bash -c "
set -euo pipefail
cd /src/sm6150
git checkout -q $COMMIT
git log -1 --format='commit %h %s' | tee /out/commit.txt
O=/src/build-$VARIANT
MK=(make -j\$(nproc) O=\$O ARCH=arm64 \
    CC=clang HOSTCC=clang HOSTCXX=clang++ LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
    CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-gnu- CROSS_COMPILE_ARM32=arm-linux-gnueabi- \
    KBUILD_BUILD_USER=a70q KBUILD_BUILD_HOST=pmos \
    DTC=\"/src/dtc-host/dtc -H epapr\")   # tree hardcodes tools/dtc = x86-64 prebuilt (DTC 1.4.4-Android); scripts/dtc/Makefile lacks 'always', so build in-tree dtc 1.4.4 by hand
\"\${MK[@]}\" a70q_defconfig
if [ -s /out/fragment.config ]; then
  scripts/kconfig/merge_config.sh -m -O \$O \$O/.config /out/fragment.config
  \"\${MK[@]}\" olddefconfig
fi
cp \$O/.config /out/config
if [ ! -x /src/dtc-host/dtc ]; then
  D=/src/dtc-host; mkdir -p \$D; cd scripts/dtc
  cp dtc-lexer.lex.c_shipped \$D/dtc-lexer.lex.c; cp dtc-parser.tab.c_shipped \$D/dtc-parser.tab.c; cp dtc-parser.tab.h_shipped \$D/dtc-parser.tab.h
  clang -O2 -w -I. -Ilibfdt -I\$D -o \$D/dtc dtc.c flattree.c fstree.c data.c livetree.c treesource.c srcpos.c checks.c util.c \$D/dtc-lexer.lex.c \$D/dtc-parser.tab.c
  cd /src/sm6150
fi
/src/dtc-host/dtc --version
set +e
\"\${MK[@]}\" Image.gz-dtb > /out/build.log 2>&1; rc=\$?
set -e
grep -E '\\berror\\b|Error [0-9]+' /out/build.log | head -20 || true
[ \$rc -eq 0 ] || { echo BUILD_FAILED rc=\$rc; exit \$rc; }
cp \$O/arch/arm64/boot/Image.gz-dtb \$O/arch/arm64/boot/Image /out/
echo BUILD_OK
"
ls -la "$OUT"
