# Sxmo device profile for Samsung Galaxy A70 (a70q). The downstream device tree
# reports the Qualcomm reference board compatible "qcom,sm6150p-idp".
# qpnp_pon: power key (KEY_POWER) + RESIN (KEY_VOLUMEDOWN); gpio-keys: KEY_VOLUMEUP
export SXMO_POWER_BUTTON="0:0:qpnp_pon"
export SXMO_VOLUME_BUTTON="1:1:gpio-keys 0:0:qpnp_pon"
export SXMO_SWAY_SCALE="2.5"
