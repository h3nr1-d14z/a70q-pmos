#!/bin/sh
# Initcalls in call order (index as painted by patch 0003): __initcall_* entries of System.map
# sorted by address. `initcalls.sh 123` prints the entry with that index and its neighbours.
M="$(dirname "$0")/out/System.map"
list() { grep -E " __initcall_" "$M" | grep -v -E " __initcall_(start|end)$" | sort | awk '{ sub(/^__initcall_+(kmod_)?/, "", $3); print NR-1, $3 }'; }
if [ -n "$1" ]; then list | awk -v i="$1" '$1 >= i-3 && $1 <= i+3 { print ($1 == i ? "=>" : "  "), $0 }'; else list; fi
