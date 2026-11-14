#!/bin/sh
# a70q: Sxmo's start hook, then the device services that Sxmo does not know about.
/usr/share/sxmo/default_hooks/sxmo_hook_start.sh "$@"
[ "$(/usr/libexec/a70q/a70q-autobrightness status)" = on ] && superctl start a70q-autobrightness
exit 0
