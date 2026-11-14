#!/bin/sh
# a70q: Sxmo's battery hook (status bar icon, low battery warning), then the automatic battery saver.
/usr/share/sxmo/default_hooks/sxmo_hook_battery.sh "$@"
/usr/libexec/a70q/a70q-battery-saver update
