#!/bin/sh
# a70q: "Back" entry for Sxmo menus (installed as sxmo_dmenu.sh in a directory that comes
# before /usr/bin in Sxmo's PATH, so every Sxmo menu goes through it).
#
# Sxmo opens a submenu by running another script from the parent menu (the parent waits in
# its eval), but no menu knows its parent, so there is only "Close Menu". Here:
#  - every process that shows a menu is registered in $XDG_RUNTIME_DIR/a70q-menu-reg;
#  - the parent menus of the current one = registered processes among its ancestors, plus
#    the menus inherited in $A70Q_MENU_STACK (set when a menu was reopened by "Back");
#  - picking "Back" ends the current menu chain and reopens the previous menu.
REAL=/usr/bin/sxmo_dmenu.sh

case "$1" in
	isopen|close|--show-over-lockscreen) exec "$REAL" "$@" ;;
esac
[ -n "$WAYLAND_DISPLAY" ] || exec "$REAL" "$@"

. sxmo_hook_icons.sh
BACK="$icon_ret Back"
REG="${XDG_RUNTIME_DIR:-/tmp}/a70q-menu-reg"
NL='
'

cmdline() { tr '\0' ' ' < "/proc/$1/cmdline" 2>/dev/null | sed 's/ $//'; }
parent() { awk '/^PPid:/ { print $2 }' "/proc/$1/status" 2>/dev/null; }

# the script that called us: topmost of the processes with our parent's command line
# (menus are read inside $(...) subshells, which carry the script's command line)
owner=$PPID
ocmd="$(cmdline "$owner")"
while p="$(parent "$owner")" && [ -n "$p" ] && [ "$p" -gt 1 ] && [ "$(cmdline "$p")" = "$ocmd" ]; do
	owner=$p
done

# registry: keep live entries (same pid and command line), add the owner
reg=""
if [ -r "$REG" ]; then
	while IFS='	' read -r pid cmd; do
		[ -n "$pid" ] && [ "$pid" != "$owner" ] && [ "$(cmdline "$pid")" = "$cmd" ] &&
			reg="$reg$pid	$cmd$NL"
	done < "$REG"
fi
reg="$reg$owner	$ocmd$NL"
printf %s "$reg" > "$REG.$$" && mv "$REG.$$" "$REG"

# registered ancestors, oldest first; remember the processes up to the oldest one
live=""      # "cmd" lines, oldest first
chain=""     # pids from the owner up to the oldest registered ancestor
p=$owner
top=$owner
while p="$(parent "$p")" && [ -n "$p" ] && [ "$p" -gt 1 ]; do
	chain="$chain $p"
	c="$(printf %s "$reg" | awk -F '\t' -v p="$p" '$1 == p { print $2; exit }')"
	if [ -n "$c" ]; then
		live="$c$NL$live"
		top=$p
	fi
done
kill_pids="$owner"
for p in $chain; do
	kill_pids="$kill_pids $p"
	[ "$p" = "$top" ] && break
done
[ "$top" = "$owner" ] && kill_pids="$owner"

stack="${A70Q_MENU_STACK:+$A70Q_MENU_STACK$NL}$live"
stack="$(printf %s "$stack" | sed '/^$/d')"

items="$(cat)"
if [ -z "$stack" ]; then
	printf '%s\n' "$items" | "$REAL" "$@"
	exit
fi

# "Back" right after "Close Menu" (or at the end when the menu has none)
picked="$(printf '%s\n' "$items" | awk -v back="$BACK" '
	{ print } /Close Menu/ && !done { print back; done = 1 }
	END { if (!done) print back }' | "$REAL" "$@")"
ret=$?
if [ "$picked" != "$BACK" ]; then
	[ -n "$picked" ] && printf '%s\n' "$picked"
	exit "$ret"
fi

target="$(printf '%s\n' "$stack" | tail -n1)"
rest="$(printf '%s\n' "$stack" | sed '$d')"
# shellcheck disable=SC2086
kill $kill_pids 2>/dev/null
A70Q_MENU_STACK="$rest" setsid sh -c "exec $target" >/dev/null 2>&1 </dev/null &
exit 1
