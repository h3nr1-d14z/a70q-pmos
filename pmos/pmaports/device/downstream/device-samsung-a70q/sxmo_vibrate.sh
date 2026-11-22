#!/bin/sh
# a70q replacement for Sxmo's sxmo_vibrate (which needs a force-feedback input device):
# the PMI632/PM6150 vibrator is a timed LED device, /sys/class/leds/vibrator.
#   sxmo_vibrate duration_ms [strength]   (strength is ignored)
V=/sys/class/leds/vibrator
# mainline kernel: the vibrator is a force-feedback input device (pm8xxx_vib_ffmemless), which
# Sxmo's own sxmo_vibrate drives; it only looks at platform-vibrator-event by default
if [ ! -d "$V" ]; then
	for d in /dev/input/by-path/*vibrator*-event; do
		[ -e "$d" ] && export SXMO_VIBRATE_DEV="$d" && break
	done
	# pm8xxx-vibrator uses magnitude >> 8: Sxmo's default strength (1) or none means off;
	# 0xe000 = 3304 mV, the downstream vibration voltage
	s=${2:-0}
	[ "$s" -ge 256 ] 2>/dev/null || s=57344
	exec /usr/bin/sxmo_vibrate "$1" "$s"
fi
ms=${1:?usage: sxmo_vibrate duration_ms [strength]}
echo "$ms" > "$V/duration" && echo 1 > "$V/activate" || exit 1
# like the original, return when the vibration is over
sleep "$(awk "BEGIN { print $ms / 1000 }")"
