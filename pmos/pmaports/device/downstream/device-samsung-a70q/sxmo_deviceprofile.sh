# Sxmo device profile for Samsung Galaxy A70 (a70q). The downstream device tree
# reports the Qualcomm reference board compatible "qcom,sm6150p-idp".
# qpnp_pon: power key (KEY_POWER) + RESIN (KEY_VOLUMEDOWN); gpio-keys: KEY_VOLUMEUP
export SXMO_POWER_BUTTON="0:0:qpnp_pon"
export SXMO_VOLUME_BUTTON="1:1:gpio-keys 0:0:qpnp_pon"
export SXMO_SWAY_SCALE="2.5"
# the modem is not handled by ModemManager (no QMI/rmnet over the downstream IPA path yet);
# without this Sxmo reports "Modem crashed!" and holds a 30 s wakelock after every resume
export SXMO_NO_MODEM=1
# device hooks/wrappers (wlopm retry, ERRORS C52): Sxmo builds its PATH from
# $XDG_DATA_DIRS/sxmo/default_hooks after sourcing this file
case ":${XDG_DATA_DIRS:-}:" in
	*:/usr/share/a70q:*) ;;
	*) export XDG_DATA_DIRS="/usr/share/a70q:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" ;;
esac
