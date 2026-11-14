#!/bin/sh
# a70q (SM6150, downstream 4.14): start Qualcomm remote-proc userspace and boot the modem
# so WLAN (WCN3990, firmware on the modem DSP) comes up. EFS is accessed READ-ONLY (rmtfs -r).
# Order matters: qrtr-ns must own node address 1 before any service publishes.
FW=/vendor/firmware_mnt/image
FWM=/vendor/firmware-modem/image
log() { echo "[a70q-modem] $*"; }
part() { blkid -t PARTLABEL="$1" -o device; }

for s in /sys/bus/msm_subsys/devices/*; do echo RELATED > "$s/restart_level"; done
mkdir -p /vendor/firmware_mnt /vendor/firmware-modem
grep -q " /vendor/firmware_mnt " /proc/mounts || mount -o ro "$(part apnhlos)" /vendor/firmware_mnt
grep -q " /vendor/firmware-modem " /proc/mounts || mount -o ro "$(part modem)" /vendor/firmware-modem
mkdir -p /dev/disk/by-partlabel /lib/firmware
for p in modemst1 modemst2 fsc fsg; do ln -sf "$(part $p)" /dev/disk/by-partlabel/$p; done
for f in "$FWM"/*; do ln -sf "$f" /lib/firmware/; done        # modem.mdt etc. for PIL
[ -e /dev/qcom_rmtfs_uio1 ] || ln -sf /dev/uio0 /dev/qcom_rmtfs_uio1   # uio0 name=rmtfs

/usr/local/sbin/qrtr-ns -f 1 >/var/log/qrtr-ns.log 2>&1 &
# wait until the name service answers (kernel memshare server 52 is always there);
# publishing before qrtr-ns is bound loses the NEW_SERVER packet silently
for i in $(seq 1 50); do /usr/local/sbin/qrtr-lookup 2>/dev/null | grep -qE "^ +52 " && break; sleep 0.2; done

start_services() {
	TQFTPSERV_FW_DIRS="$FW:$FWM" /usr/local/sbin/tqftpserv >>/var/log/tqftpserv.log 2>&1 &
	/usr/sbin/rmtfs -P -r -v >>/var/log/rmtfs.log 2>&1 &
	PD_MAPPER_JSON_DIRS="$FW:$FWM" /usr/local/sbin/pd-mapper >>/var/log/pd-mapper.log 2>&1 &
}
services_ok() {
	l=$(/usr/local/sbin/qrtr-lookup 2>/dev/null)
	for svc in 14 64 4096; do echo "$l" | grep -qE "^ +$svc " || return 1; done
}
start_services
for try in 1 2 3; do
	sleep 2
	services_ok && break
	log "services not registered (try $try), restarting them"
	pkill -x tqftpserv; pkill -x rmtfs; pkill -x pd-mapper; sleep 1
	start_services
done
services_ok && log "qrtr services: rmtfs(14) pd-mapper(64) tftp(4096) OK" || { log "ERROR: qrtr services missing"; exit 1; }

# boot modem: subsystem stays up while /dev/subsys_modem is held open
( exec 3</dev/subsys_modem; exec sleep 2147483647 ) &
echo $! > /run/a70q-subsys-modem.pid
log "modem requested"
