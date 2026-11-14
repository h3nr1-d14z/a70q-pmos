#!/bin/sh
# default brightnessctl to the a70q AMOLED panel backlight unless a device was given
case " $* " in
	*" -d "*|*" --device"*|*" -l "*|*" --list "*|*" -m "*) exec /usr/bin/brightnessctl "$@" ;;
esac
exec /usr/bin/brightnessctl -d panel0-backlight "$@"
