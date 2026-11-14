#!/bin/sh
# a70q: Sxmo's start hook, then the device services that Sxmo does not know about.
/usr/share/sxmo/default_hooks/sxmo_hook_start.sh "$@"
[ "$(/usr/libexec/a70q/a70q-autobrightness status)" = on ] && superctl start a70q-autobrightness
# input method, only when installed (apk add fcitx5 fcitx5-unikey; profile ~/.config/fcitx5/profile)
command -v fcitx5 >/dev/null && superctl start a70q-fcitx5
# reconnect paired Bluetooth headphones/speakers
setsid -f /usr/libexec/a70q/a70q-bt-reconnect >/dev/null 2>&1
exit 0
