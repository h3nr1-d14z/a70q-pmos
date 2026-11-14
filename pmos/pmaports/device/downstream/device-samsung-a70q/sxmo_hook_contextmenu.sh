#!/bin/sh
# a70q: Sxmo's context menus, with the Config menu's "Adaptive Brightness" toggle pointed at
# a70q-autobrightness. Sxmo's own sxmo_adaptivebrightness.sh does not work here: it sources
# sxmo_brightness.sh (our wrapper exec's the real script), its brightnessctl picks the unused
# WLED device (ERRORS C56) and it compares float lux values with test -lt; it also overrides
# manual brightness changes every 10 s.
# The lines are evaluated by sxmo_appmenu.sh: "label ^ loop ^ command".
. sxmo_hook_icons.sh
[ "$(/usr/libexec/a70q/a70q-autobrightness status)" = on ] && toggle="$icon_ton" || toggle="$icon_tof"
/usr/share/sxmo/default_hooks/sxmo_hook_contextmenu.sh "$@" | awk -v scale="			$icon_ror Display scale ($(/usr/libexec/a70q/a70q-scale get)) ^ 0 ^ /usr/libexec/a70q/a70q-scale" -v line="			$icon_brightness Adaptive Brightness $toggle ^ 1 ^ /usr/libexec/a70q/a70q-autobrightness toggle" '
	/supertoggle_daemon sxmo_adaptivebrightness/ { print line; print scale; next }
	{ print }'
