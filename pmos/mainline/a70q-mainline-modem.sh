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
( for i in $(seq 1 60); do [ -e /sys/class/bluetooth/hci0 ] && break; sleep 1; done
  timeout 8 btmgmt --index 0 public-addr "$(cat $A)" </dev/null >/dev/null 2>&1
  echo "bluetooth: public address set" ) &
