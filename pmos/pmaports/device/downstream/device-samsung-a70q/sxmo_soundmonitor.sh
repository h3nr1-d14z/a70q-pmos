#!/bin/sh
# a70q copy of Sxmo's sxmo_soundmonitor.sh (ERRORS C55): upstream only refreshes the status bar
# on "change" events of a sink/source when the volume changed. Plugging/unplugging headphones
# makes PipeWire switch the card profile, which creates/removes sinks ("new"/"remove") and
# changes the default sink ("change" on server) -> the headphone icon never updated.
# shellcheck source=/dev/null
. sxmo_common.sh

notify_volume_change() {
	vol="$(sxmo_audio.sh vol get)"
	if [ "$vol" != "$lastvol" ]; then
		sxmo_hook_statusbar.sh volume
		sxmo_audio.sh notify
	fi
	lastvol="$vol"
}

notify_mic_volume_change() {
	micvol="$(sxmo_audio.sh mic volget)"
	if [ "$micvol" != "$lastmicvol" ]; then
		sxmo_hook_statusbar.sh volume
		sxmo_audio.sh micnotify
	fi
	lastmicvol="$micvol"
}

pactl subscribe | while read -r line; do
	case "$line" in
		"Event 'change' on sink "*)
			notify_volume_change
			;;
		"Event 'change' on source "*)
			notify_mic_volume_change
			;;
		"Event 'new' on sink "*|"Event 'remove' on sink "*|\
		"Event 'new' on source "*|"Event 'remove' on source "*|\
		"Event 'change' on server"*)
			sxmo_hook_statusbar.sh volume
			;;
	esac
done
