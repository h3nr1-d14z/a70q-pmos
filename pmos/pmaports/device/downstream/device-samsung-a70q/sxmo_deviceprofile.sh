# Sxmo device profile for Samsung Galaxy A70 (a70q). The downstream device tree
# reports the Qualcomm reference board compatible "qcom,sm6150p-idp".
# qpnp_pon: power key (KEY_POWER) + RESIN (KEY_VOLUMEDOWN); gpio-keys: KEY_VOLUMEUP
export SXMO_POWER_BUTTON="0:0:qpnp_pon"
export SXMO_VOLUME_BUTTON="1:1:gpio-keys 0:0:qpnp_pon"
# UI scale: Config menu "Display scale" (a70q-scale) remembers the choice
export SXMO_SWAY_SCALE="$(cat "${XDG_CONFIG_HOME:-$HOME/.config}/a70q/scale" 2>/dev/null || echo 2.5)"
# the modem is not handled by ModemManager (no QMI/rmnet over the downstream IPA path yet);
# without this Sxmo reports "Modem crashed!" and holds a 30 s wakelock after every resume
export SXMO_NO_MODEM=1
# no notification LED on the A70 (the red/green/blue LED class devices are unconnected PMIC channels)
export SXMO_DISABLE_LEDS=1
# battery percentage next to the battery icon in the status bar
export SXMO_BAR_SHOW_BAT_PER=1
# device hooks/wrappers (wlopm retry, ERRORS C52): Sxmo builds its PATH from
# $XDG_DATA_DIRS/sxmo/default_hooks after sourcing this file
case ":${XDG_DATA_DIRS:-}:" in
	*:/usr/share/a70q:*) ;;
	*) export XDG_DATA_DIRS="/usr/share/a70q:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" ;;
esac
# menus (bemenu): Sxmo's default is a narrow box with 20 px lines (labels cut off, small touch
# targets); wider, taller lines, bigger font, rounded border
export BEMENU_OPTS="--center --width-factor 0.94 --line-height 34 --fn 'Sxmo 15' --border 2 --border-radius 14 --no-spacing --fixed-height --wrap --scrollbar autohide --tb #2b5f9e --tf #ffffff --fb #20242b --ff #e6e9ef --cb #20242b --cf #e6e9ef --nb #20242b --nf #dce1e8 --ab #252a32 --af #dce1e8 --hb #2b5f9e --hf #ffffff --fbb #e6e9ef --fbf #20242b --sb #2b5f9e --sf #ffffff --scb #2b5f9e --scf #e6e9ef --bdr #2b5f9e"
export SXMO_BEMENU_PORTRAIT_LINES=18
export SXMO_BEMENU_LANDSCAPE_LINES=8
# default browser (Epiphany: light WebKitGTK browser with a phone UI; needs a70q-webkit.sh)
export BROWSER=epiphany
# screenshots (Power + Volume Down, a70q-bonsai_tree.json) next to the other pictures
export SXMO_SCREENSHOT_DIR="$HOME/Pictures/Screenshots"
