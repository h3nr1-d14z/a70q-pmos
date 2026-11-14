#!/bin/sh
# a70q: three backlight class devices exist and brightnessctl picks "backlight" (the unused WLED);
# the AMOLED panel is "panel0-backlight". Run Sxmo's own script with a brightnessctl shim first
# in PATH that defaults to that device (ERRORS C56).
PATH=/usr/share/a70q/brightnessctl-shim:$PATH exec /usr/bin/sxmo_brightness.sh "$@"
