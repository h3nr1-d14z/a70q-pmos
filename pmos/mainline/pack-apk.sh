#!/usr/bin/env bash
# Package the current mainline build (out/modules.tar.gz + the daily image from mkboot-pmos.sh) as
# linux-samsung-a70q-mainline (pkgrel bumped) and build it into the local repo.
#   pmos/mainline/pack-apk.sh          then on the phone: apk upgrade (or apk add linux-samsung-a70q-mainline)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
P="$REPO/pmos/pmaports/device/downstream/linux-samsung-a70q-mainline"
"$HERE/mkboot-pmos.sh" "$P/boot-mainline.img"
cp "$HERE/out/modules.tar.gz" "$P/modules.tar.gz"
python3 - "$P" <<'PY'
import sys, re, hashlib, os
p = sys.argv[1] + '/APKBUILD'; a = open(p).read()
a = re.sub(r'^pkgrel=(\d+)$', lambda m: 'pkgrel=%d' % (int(m.group(1)) + 1), a, count=1, flags=re.M)
src = re.search(r'^source="(.*?)"', a, re.S | re.M).group(1).split()
sums = ''.join('%s  %s\n' % (hashlib.sha512(open(os.path.join(sys.argv[1], f), 'rb').read()).hexdigest(), f) for f in src)
a = re.sub(r'^sha512sums=".*?"', lambda m: 'sha512sums="\n' + sums + '"', a, count=1, flags=re.S | re.M)
open(p, 'w').write(a); print(re.search(r'^pkgrel=\d+', a, re.M).group(0))
PY
"$REPO/pmos/pmb/run.sh" build --arch aarch64 linux-samsung-a70q-mainline
