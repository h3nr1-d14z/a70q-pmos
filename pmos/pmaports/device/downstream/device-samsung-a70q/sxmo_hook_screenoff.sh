#!/bin/sh
# a70q: Sxmo's screenoff hook, then Bluetooth UART sleep (a70q-bt-sleep, ERRORS C71).
/usr/share/sxmo/default_hooks/sxmo_hook_screenoff.sh "$@"
rc=$?
setsid -f /usr/libexec/a70q/a70q-bt-sleep sleep >/dev/null 2>&1
exit $rc
