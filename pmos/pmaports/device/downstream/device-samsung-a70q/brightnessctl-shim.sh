#!/bin/sh
# default brightnessctl to the a70q AMOLED panel backlight unless a device was given
case " $* " in
	*" -d "*|*" --device"*|*" -l "*|*" --list "*|*" -m "*) exec /usr/bin/brightnessctl "$@" ;;
esac
# downstream: panel0-backlight; mainline: the DSI panel's backlight (ae94000.dsi.0)
BL=panel0-backlight
[ -e /sys/class/backlight/$BL ] || BL=$(ls /sys/class/backlight/ 2>/dev/null | head -1)
exec /usr/bin/brightnessctl -d "$BL" "$@"
