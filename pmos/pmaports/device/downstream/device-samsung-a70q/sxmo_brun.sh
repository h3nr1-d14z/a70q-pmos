#!/bin/sh
# a70q: Sxmo's "Binaries" menu. Sxmo runs bemenu-run directly there: no "Close Menu" entry, so on a
# touch screen (no Esc key) the menu could not be closed, and each tap opened another one.
# List the programs in $PATH through sxmo_dmenu.sh instead (same look, Close Menu, Back).
. sxmo_hook_icons.sh
pick="$({
	echo "$icon_cls Close Menu"
	echo "$PATH" | tr ':' '\n' | while read -r d; do
		[ -d "$d" ] && find "$d" -maxdepth 1 \( -type f -o -type l \) -perm -u+x -exec basename {} \; 2>/dev/null
	done | sort -u
} | sxmo_dmenu.sh -i -p "Run")" || exit 0
case "$pick" in
	""|*"Close Menu") exit 0 ;;
esac
exec setsid -f sh -c "$pick" >/dev/null 2>&1
