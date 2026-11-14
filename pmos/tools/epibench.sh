#!/bin/sh
# epibench.sh "<env assignments>": open a long page in Epiphany, scroll with keys, print CPU used
# during the scrolling per process group (centiseconds)
export WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/10000
SWAYSOCK=$(ls /run/user/10000/sway-ipc.* | head -1); export SWAYSOCK
cleanup() { for p in $(pgrep -x epiphany) $(pgrep -f /usr/libexec/webkitgtk-6.0/WebKit); do kill $p 2>/dev/null; done; sleep 2; }
cleanup
swaymsg -q exec "env $1 epiphany https://en.wikipedia.org/wiki/History_of_Vietnam"
sleep 18
grp() { t=0; for p in $2; do s=$(cut -d" " -f14,15 /proc/$p/stat 2>/dev/null); set -- $s; t=$((t + ${1:-0} + ${2:-0})); done; echo $t; }
E=$(pgrep -x epiphany); K=$(pgrep -f /usr/libexec/webkitgtk-6.0/WebKit); S=$(pgrep -x sway)
e0=$(grp x "$E"); k0=$(grp x "$K"); s0=$(grp x "$S")
for i in $(seq 1 15); do wtype -k Page_Down; sleep 0.15; done
for i in $(seq 1 15); do wtype -k Page_Up; sleep 0.15; done
for i in $(seq 1 15); do wtype -k Page_Down; sleep 0.15; done
e1=$(grp x "$E"); k1=$(grp x "$K"); s1=$(grp x "$S")
echo "[$1] epiphany=$((e1-e0)) webkit=$((k1-k0)) sway=$((s1-s0)) total=$((e1-e0+k1-k0+s1-s0))"
cleanup
