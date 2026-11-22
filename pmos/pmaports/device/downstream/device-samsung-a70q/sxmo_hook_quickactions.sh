#!/bin/sh
# a70q: quick settings (two-finger swipe down from the top edge), replacing Sxmo's quickactions hook:
# - Sxmo's WiFi/Bluetooth toggles run doas, which asks for a password here (silently fails);
#   use nmcli (polkit allows the active session) and bluetoothctl instead
# - Sxmo looks for a "white:torch"/"white:flash" LED; the A70 torch is led:torch_0 + led:switch_0
# - added: auto-rotate, rotate, brightness, adaptive brightness, Vietnamese input, screenshot
# The menu stays open and shows the new state after each toggle, like Sxmo's.
. sxmo_common.sh
. sxmo_hook_icons.sh

onoff() { if "$@" >/dev/null 2>&1; then printf %s "$icon_ton"; else printf %s "$icon_tof"; fi; }
wifi_on() { [ "$(nmcli radio wifi 2>/dev/null)" = enabled ]; }
bt_on() { bluetoothctl show 2>/dev/null | grep -q "Powered: yes"; }
torch_on() {	# downstream led:switch_0, mainline white:flash
	for l in led:switch_0 white:flash; do
		[ "$(cat /sys/class/leds/$l/brightness 2>/dev/null || echo 0)" -gt 0 ] && return 0
	done
	return 1
}
rotate_on() { sxmo_jobs.sh running autorotate -q; }
adaptive_on() { [ "$(/usr/libexec/a70q/a70q-autobrightness status 2>/dev/null)" = on ]; }
vi_on() { [ "$(fcitx5-remote 2>/dev/null)" = 2 ]; }

player_options() {
	case "$(playerctl status 2>/dev/null)" in
		"") ;;
		*Playing*) printf '%s\n' "$icon_arl Previous" "$icon_pau Pause" "$icon_arr Next" ;;
		*) printf '%s\n' "$icon_itm Play" ;;
	esac
}

while :; do
	PICKED="$(grep . <<-EOF | sxmo_dmenu.sh --show-over-lockscreen -p "Quick"
	$icon_cls Close Menu
	$icon_wifi_signal_4 WiFi $(onoff wifi_on)
	$icon_bth Bluetooth $(onoff bt_on)
	$icon_fll Flashlight $(onoff torch_on)
	$icon_ror Auto-rotate $(onoff rotate_on)
	$icon_ror Rotate now
	$icon_aru Brightness up
	$icon_ard Brightness down
	$icon_brightness Adaptive brightness $(onoff adaptive_on)
	$(pgrep -x fcitx5 >/dev/null && printf '%s\n' "$icon_kbd Vietnamese input $(onoff vi_on)")
	$icon_cam Screenshot
	$(player_options)
	$icon_inf Gestures & buttons
	$icon_pwr Screen Off
	EOF
	)" || exit 0
	case "$PICKED" in
		*'Close Menu'|'') exit 0 ;;
		*WiFi*) if wifi_on; then nmcli radio wifi off; else nmcli radio wifi on; fi ;;
		*Bluetooth*) if bt_on; then bluetoothctl power off; else bluetoothctl power on; fi >/dev/null ;;
		*Flashlight*) sxmo_flashtoggle.sh ;;
		*Auto-rotate*)
			if rotate_on; then sxmo_jobs.sh stop autorotate
			else sxmo_jobs.sh start autorotate sxmo_autorotate.sh & fi ;;
		*'Rotate now') sxmo_rotate.sh ;;
		*'Brightness up') sxmo_brightness.sh up ;;
		*'Brightness down') sxmo_brightness.sh down ;;
		*'Adaptive brightness'*) /usr/libexec/a70q/a70q-autobrightness toggle ;;
		*'Vietnamese input'*) fcitx5-remote -t ;;
		*Screenshot) (sleep 0.8; sxmo_screenshot.sh >/dev/null) & exit 0 ;;
		*Previous) playerctl previous ;;
		*Pause) playerctl pause ;;
		*Play) playerctl play ;;
		*Next) playerctl next ;;
		*'Gestures & buttons') /usr/libexec/a70q/a70q-help; exit 0 ;;
		*'Screen Off') sxmo_state.sh set screenoff; exit 0 ;;
	esac
done
