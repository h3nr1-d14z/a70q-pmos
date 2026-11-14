#!/bin/sh
# Rebuild dtbo.img without the SDE writeback connector (DRM "Virtual-1"), which steals a CRTC
# from the panel in wlroots. Only the `connectors` list of mdss_mdp is edited (+ its fixup
# offsets); the qcom,wb-display node itself stays enabled -- disabling it makes the SDE
# component master wait forever and NO DRM card appears (ERRORS C49).
#   pmos/tools/dtbo-drop-wb.sh <orig dtbo.img> <out.img>
set -e
IN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); OUT=$2
T=$(mktemp -d); HERE=$(cd "$(dirname "$0")" && pwd)
python3 "$HERE/dtboimg.py" unpack "$IN" "$T/e" >/dev/null
docker run --rm -v a70q-ksrc:/src -v "$T":/t a70q-kbuild bash -ec '
cd /t/e; for f in e*.dtbo; do
  /src/dtc-host/dtc -q -I dtb -O dts -o ${f%.dtbo}.dts $f
  python3 - ${f%.dtbo}.dts <<PY
import sys
p=sys.argv[1]; s=open(p).read()
for a,b in (("connectors = <0xffffffff 0x17 0xffffffff 0x18>;","connectors = <0xffffffff 0xffffffff 0x18>;"),
            ("\"/fragment@15/__overlay__:connectors:8\"","\"/fragment@15/__overlay__:connectors:4\""),
            ("connectors = <0x4 0xc>;","connectors = <0x8>;")):
    assert s.count(a)==1,(p,a); s=s.replace(a,b)
open(p,"w").write(s)
PY
  /src/dtc-host/dtc -q -I dts -O dtb -o $f ${f%.dtbo}.dts
done'
python3 "$HERE/dtboimg.py" pack "$IN" "$T/e" "$OUT"
rm -rf "$T"
