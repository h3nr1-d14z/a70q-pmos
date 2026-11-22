#!/bin/sh
# a70q replacement for Sxmo's sxmo_flashtoggle.sh (which looks for a "white:flash"/"white:torch"
# LED): the rear flash is driven by the Qualcomm flash LED driver as led:torch_0 (current in mA)
# fired through led:switch_0.
T=/sys/class/leds/led:torch_0
# mainline kernel: leds-qcom-flash names it white:flash, which Sxmo's own script finds
[ -e "$T" ] || exec /usr/bin/sxmo_flashtoggle.sh "$@"
S=/sys/class/leds/led:switch_0
if [ "$(cat "$S/brightness")" -gt 0 ]; then
	echo 0 > "$S/brightness"
else
	echo 200 > "$T/brightness"
	echo 255 > "$S/brightness"
fi
