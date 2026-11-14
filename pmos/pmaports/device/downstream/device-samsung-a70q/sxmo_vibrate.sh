#!/bin/sh
# a70q replacement for Sxmo's sxmo_vibrate (which needs a force-feedback input device):
# the PMI632/PM6150 vibrator is a timed LED device, /sys/class/leds/vibrator.
#   sxmo_vibrate duration_ms [strength]   (strength is ignored)
V=/sys/class/leds/vibrator
ms=${1:?usage: sxmo_vibrate duration_ms [strength]}
echo "$ms" > "$V/duration" && echo 1 > "$V/activate" || exit 1
# like the original, return when the vibration is over
sleep "$(awk "BEGIN { print $ms / 1000 }")"
