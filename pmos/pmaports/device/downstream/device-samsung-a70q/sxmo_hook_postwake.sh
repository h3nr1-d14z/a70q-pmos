#!/bin/sh
# a70q override of Sxmo's postwake hook (ERRORS C52).
# The power key press that wakes the phone happens while the kernel resumes and never reaches
# sway, so Sxmo stayed in "screenoff" and a second press was needed to turn the screen on.
# If the last resume was caused by the PMIC power key IRQ, do what that press would have done.
# RTC (cron/alarm) and WiFi wakes keep the screen off.
# shellcheck source=/dev/null
. sxmo_common.sh
if dmesg | awk '/PM: suspend entry/ { b = "" } { b = b $0 "\n" } END { printf "%s", b }' |
	grep -q "Resume caused by IRQ [0-9]*, pon_kpdpwr"; then
	sxmo_state.sh set lock &
fi
exec /usr/share/sxmo/default_hooks/sxmo_hook_postwake.sh
