#!/bin/sh
# a70q replacement for `wlopm` in Sxmo's PATH (ERRORS C52).
# - After the panel was powered off, the first DRM modeset that turns it back on fails in the
#   downstream SDE driver (sway: "Backend commit failed"); the next one works.
# - After such a failure wlroots drops the output's wlr-output-power object, so wlopm no longer
#   lists the output and `wlopm --on` silently does nothing -> the screen stayed dark.
# So: switch power through sway IPC with retries, and report the kernel's DPMS state.
DPMS=/sys/class/drm/card0-DSI-1/dpms
is_on() { grep -q "^On" "$DPMS"; }
if [ -z "$SWAYSOCK" ] || ! command -v swaymsg >/dev/null; then
	exec /usr/bin/wlopm "$@"
fi
case "$1" in
	--on)
		for i in $(seq 1 20); do
			swaymsg -q output '*' power on
			sleep 0.25
			is_on && exit 0
		done
		exit 1 ;;
	--off)
		swaymsg -q output '*' power off ;;
	"")
		if is_on; then echo "DSI-1 on"; else echo "DSI-1 off"; fi ;;
	*)
		exec /usr/bin/wlopm "$@" ;;
esac
