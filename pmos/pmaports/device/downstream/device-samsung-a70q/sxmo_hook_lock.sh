#!/bin/sh
# a70q: Sxmo's lock hook, then Bluetooth UART wake (a70q-bt-sleep, ERRORS C71).
/usr/share/sxmo/default_hooks/sxmo_hook_lock.sh "$@"
rc=$?
setsid -f /usr/libexec/a70q/a70q-bt-sleep wake >/dev/null 2>&1
exit $rc
