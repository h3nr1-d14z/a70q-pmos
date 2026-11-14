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
docker run --rm -v "$T":/t alpine:3.22 sh -ec '
apk add -q dtc python3
cd /t/e; for f in e*.dtbo; do
  dtc -q -I dtb -O dts -o ${f%.dtbo}.dts $f
  python3 - ${f%.dtbo}.dts <<PY
import re, sys
p=sys.argv[1]; s=open(p).read()
# number formatting differs between dtc versions (1.4.4: 0x4, 1.7: 0x04), hence the regexes
for a,b in ((r"connectors = <0xffffffff 0x17 0xffffffff 0x18>;","connectors = <0xffffffff 0xffffffff 0x18>;"),
            (r"\"/fragment@15/__overlay__:connectors:8\"","\"/fragment@15/__overlay__:connectors:4\""),
            (r"connectors = <0x0?4 0x0?c>;","connectors = <0x08>;")):
    s,n=re.subn(a,b,s); assert n==1,(p,a,n)
open(p,"w").write(s)
PY
  dtc -q -I dts -O dtb -o $f ${f%.dtbo}.dts
done'
python3 "$HERE/dtboimg.py" pack "$IN" "$T/e" "$OUT"
rm -rf "$T"
