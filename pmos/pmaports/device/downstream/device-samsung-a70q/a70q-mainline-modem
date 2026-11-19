#!/bin/sh
# Bring up the modem (MPSS) and with it WiFi (WCN3990 firmware runs on the modem DSP) on the
# mainline kernel. Run as root once the firmware partitions are mounted (pmOS does it at boot).
#   /vendor/firmware-modem/image: modem.mdt (loaded by remoteproc: firmware_class.path)
#   /vendor/firmware_mnt/image:  wlanmdsp.mbn, *.jsn, bdwlan.bin
FW=/vendor/firmware_mnt/image
FWM=/vendor/firmware-modem/image
# firmware partitions (read-only), as a70q-modem-up does on the downstream kernel
mkdir -p /vendor/firmware_mnt /vendor/firmware-modem
grep -q " /vendor/firmware_mnt " /proc/mounts ||
	mount -o ro /dev/disk/by-partlabel/apnhlos /vendor/firmware_mnt
grep -q " /vendor/firmware-modem " /proc/mounts ||
	mount -o ro /dev/disk/by-partlabel/modem /vendor/firmware-modem
RP=$(grep -l '^modem$' /sys/class/remoteproc/*/name 2>/dev/null | head -1)
RP=${RP%/name}
[ -n "$RP" ] || { echo "no modem remoteproc"; exit 1; }

# the modem fetches wlanmdsp.mbn over TFTP; tqftpserv looks next to the remoteproc firmware and
# in /lib/firmware
ln -sf $FW/wlanmdsp.mbn /lib/firmware/wlanmdsp.mbn
# GPU (msm): device-signed zap shader and the RGMU next to the linux-firmware SQE in qcom/
mkdir -p /lib/firmware/qcom
for f in $FW/a612_zap.*; do [ -e "$f" ] && ln -sf "$f" /lib/firmware/qcom/; done; rm -f "/lib/firmware/qcom/a612_zap.*"
[ -e /lib/firmware/qcom/a612_rgmu.bin ] || cp /lib/firmware/a612_rgmu.bin /lib/firmware/qcom/ 2>/dev/null

pkill -x tqftpserv; pkill -x pd-mapper; pkill -x rmtfs
sleep 1
TQFTPSERV_FW_DIRS="$FW:$FWM" setsid /usr/libexec/a70q/tqftpserv >/var/log/tqftpserv.log 2>&1 &
PD_MAPPER_JSON_DIRS="$FW:$FWM" setsid /usr/libexec/a70q/pd-mapper >/var/log/pd-mapper.log 2>&1 &
setsid /usr/sbin/rmtfs -P -r -v >/var/log/rmtfs.log 2>&1 &
sleep 2

# Audio DSP (APR): firmware next to the other qcom/ blobs, then start it. Before the modem:
# with both starting at once (audio services probing while the modem boots) the modem asserted
# 40 s after its first start.
mkdir -p /lib/firmware/qcom/a70q
# IPA GSI firmware: the IPA driver loads it at probe, before this partition is mounted -> copy
for f in $FW/ipa_fws.*; do [ -e "$f" ] && cmp -s "$f" /lib/firmware/qcom/a70q/${f##*/} || cp "$f" /lib/firmware/qcom/a70q/ 2>/dev/null; done
for f in $FW/adsp.*; do [ -e "$f" ] && ln -sf "$f" /lib/firmware/qcom/a70q/; done
RA=$(grep -l '^adsp$' /sys/class/remoteproc/*/name 2>/dev/null | head -1)
RA=${RA%/name}
if [ -n "$RA" ] && [ "$(cat $RA/state)" != running ]; then
	echo start > $RA/state
	echo "adsp: $(cat $RA/state)"
fi
sleep 8

[ "$(cat $RP/state)" = running ] && { echo stop > $RP/state; sleep 2; }
echo start > $RP/state
echo "modem: $(cat $RP/state)"

# Bluetooth (WCN3988 on uart7, hci_qca): the NVM has no BD address, so the controller stays
# "unconfigured" until one is set. Use the address the downstream kernel's stack had
# (saved by a70q-modem-up to /var/lib/a70q/bt-addr) so paired devices still recognise the
# phone; otherwise make up a stable locally administered one.
A=/var/lib/a70q/bt-addr
if [ ! -s $A ]; then
	mkdir -p /var/lib/a70q
	printf '02:A7:0A:%02X:%02X:%02X\n' $(od -An -N3 -tu1 /dev/urandom) > $A
fi
# The controller drops back to "unconfigured" after every reset (hardware error event after
# resume, rfkill...), so keep watching and set the address again whenever it is missing.
( while :; do
	if [ -e /sys/class/bluetooth/hci0 ] &&
	   ! timeout 5 btmgmt info </dev/null 2>/dev/null | grep -qi "addr $(cat $A)"; then
		timeout 8 btmgmt --index 0 public-addr "$(cat $A)" </dev/null >/dev/null 2>&1
	fi
	sleep 15
  done ) >/dev/null 2>&1 &

# Sensors (accelerometer, light/proximity, compass): the ADSP sensor core reads its registry and
# config over FastRPC; hexagonrpcd serves them from /var/lib/a70q-sensors (filled by the
# downstream a70q-modem-up from the vendor/persist partitions). Upstream FastRPC node, no shim.
( R=/var/lib/a70q-sensors
  for i in $(seq 1 90); do
	/usr/libexec/a70q/qrtr-lookup 2>/dev/null | grep -qE "^ +400 " && [ -e /dev/fastrpc-adsp-secure ] && break
	sleep 1
  done
  [ -d $R/sensors ] && command -v hexagonrpcd >/dev/null || exit 0
  for try in 1 2 3 4 5; do
	setsid hexagonrpcd -s -f /dev/fastrpc-adsp-secure -d adsp -R $R >>/var/log/a70q-sensors.log 2>&1 &
	pid=$!
	sleep 10
	kill -0 $pid 2>/dev/null && break
  done
  rc-service -q iio-sensor-proxy restart 2>/dev/null ) >/dev/null 2>&1 &

# Speaker (WSA881x on the WSA SoundWire master): route MultiMedia1 -> WSA codec DMA -> speaker
# once the card is there (no UCM for this card yet), so plain ALSA/PipeWire output works.
( for i in $(seq 1 120); do grep -q "Samsung Galaxy A70" /proc/asound/cards 2>/dev/null && break; sleep 1; done
  C="amixer -q -c 0 cset"
  $C name="WSA_CODEC_DMA_RX_0 Audio Mixer MultiMedia1" 1
  $C name="WSA RX0 MUX" AIF1_PB
  $C name="WSA_RX0 INP0" RX0
  $C name="WSA_COMP1 Switch" 1
  $C name="SpkrLeft COMP Switch" 1
  $C name="SpkrLeft BOOST Switch" 1
  $C name="SpkrLeft DAC Switch" 1
  $C name="SpkrLeft VISENSE Switch" 0
  $C name="WSA_RX0 Digital Volume" 70
  $C name="SpkrLeft PA Volume" 10
  # main mic (AMIC1 -> WCD937x ADC1 -> TX SoundWire port 2 -> TX macro DEC0) on MultiMedia2
  $C name="MultiMedia2 Mixer TX_CODEC_DMA_TX_3" 1
  $C name="TX_AIF1_CAP Mixer DEC0" 1
  $C name="TX DEC0 MUX" SWR_MIC
  $C name="TX SMIC MUX0" ADC0
  $C name="ADC1 Switch" 1
  $C name="ADC1_MIXER Switch" 1
  $C name="ADC1 Volume" 16
  $C name="TX_DEC0 Volume" 84
  # speaker <-> 3.5 mm jack, headset mic, following the jack input events
  exec /usr/libexec/a70q/a70q-jack-switch ) >/var/log/a70q-jack-switch.log 2>&1 &
