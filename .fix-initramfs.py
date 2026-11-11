import gzip, subprocess, os, shutil, sys

WORK = "/work/device/samsung/a70q/initramfs.gz"
ORIG = "/host/.tmp-initrd-orig.gz"
W = "/tmp/ifx"

def sh(cmd, cwd=None, inp=None):
    r = subprocess.run(cmd, shell=True, cwd=cwd, input=inp,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert r.returncode == 0, f"cmd failed: {cmd}\n{r.stderr.decode(errors='replace')}"
    return r

# 1) extract orig
sh("rm -rf /tmp/ifx && mkdir -p /tmp/ifx")
sh("cpio -idm", cwd=W.replace(W, "/tmp/ifx"), inp=gzip.open(ORIG, "rb").read())
n1 = sum(len(f) for _, _, f in os.walk("/tmp/ifx"))
assert os.path.exists("/tmp/ifx/init"), "no /init"
assert os.path.exists("/tmp/ifx/bin/busybox"), "no busybox"
assert os.path.exists("/tmp/ifx/scripts/panic/telnet"), "no telnet script"
print(f"extract ok: {sum(len(f) for _,_,f in os.walk('/tmp/ifx'))} files")

# 2) patch telnet: ncm + sda10 dumps
t = "/tmp/ifx/scripts/panic/telnet"
s = open(t).read()
assert "USB_FUNCTIONS=rndis" in s, "rndis anchor missing"
s = s.replace("USB_FUNCTIONS=rndis", "USB_FUNCTIONS=ncm")
b1 = """    if echo $USB_FUNCTIONS | grep -q "rndis"; then
        mkdir $GADGET_DIR/g1/functions/rndis.usb0
        mkdir $GADGET_DIR/g1/functions/rndis_bam.rndis
    fi"""
assert b1 in s, "mkdir block missing"
s = s.replace(b1, b1 + """
    if echo $USB_FUNCTIONS | grep -q "ncm"; then
        mkdir $GADGET_DIR/g1/functions/ncm.usb0
    fi""")
b2 = """    if echo $USB_FUNCTIONS | grep -q "rndis"; then
        ln -s $GADGET_DIR/g1/functions/rndis.usb0 $GADGET_DIR/g1/configs/c.1
        ln -s $GADGET_DIR/g1/functions/rndis_bam.rndis $GADGET_DIR/g1/configs/c.1
    fi"""
assert b2 in s, "ln block missing"
s = s.replace(b2, b2 + """
    if echo $USB_FUNCTIONS | grep -q "ncm"; then
        ln -s $GADGET_DIR/g1/functions/ncm.usb0 $GADGET_DIR/g1/configs/c.1
    fi""")
oldfail = """	dmesg
	sleep 60 # plenty long enough to check usb on host
	reboot -f"""
assert oldfail in s, "fail path missing"
s = s.replace(oldfail, """	dmesg >> /dev/block/sda10 2>/dev/null
	echo "==== USB SETUP FAILED, rebooting" >> /dev/block/sda10 2>/dev/null
	sleep 5
	reboot -f""")
anchor = "TELNET_DEBUG_PORT=23"
assert anchor in s, "anchor missing"
s = s.replace(anchor, anchor + """

# halium-log: dump pstore + kmsg vao MISC (sda10)
cat /sys/fs/pstore/* /dev/null > /dev/block/sda10 2>/dev/null
dmesg > /dev/block/sda10 2>/dev/null""")
open(t, "w").write(s)
print("telnet patched: ncm + sda10 dumps")

# 3) patch init: stage marker ngay sau export PATH
i = "/tmp/ifx/init"
s = open(i).read()
anchor = "export PATH=/sbin:/usr/sbin:/bin:/usr/bin"
assert anchor in s, "init anchor missing"
s = s.replace(anchor, anchor + """
# halium-log: stage marker + early kernel log vao MISC (sda10)
echo "init-stage: /init reached" > /dev/block/sda10 2>/dev/null
dmesg > /dev/block/sda10 2>/dev/null""", 1)
open(i, "w").write(s)
print("init patched: stage marker")

# 4) repack
r = subprocess.run("find . | cpio -o -H newc | gzip -6", shell=True,
                   cwd="/tmp/ifx", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert r.returncode == 0, "repack failed"
newgz = r.stdout
shutil.copyfile(ORIG, WORK)
open(WORK, "wb").write(newgz)
print(f"repacked: {len(newgz)} bytes -> {WORK}")

# 5) verify 2 chieu: re-extract tu file da ghi
sh("rm -rf /tmp/ifv && mkdir -p /tmp/ifv")
sh("cpio -idm", cwd="/tmp/ifv", inp=gzip.open(WORK, "rb").read())
assert os.path.exists("/tmp/ifv/init"), "VERIFY: no /init"
assert os.path.exists("/tmp/ifv/bin/busybox"), "VERIFY: no busybox"
v = open("/tmp/ifv/scripts/panic/telnet").read()
assert "USB_FUNCTIONS=ncm" in v, "VERIFY: no ncm"
assert v.count("sda10") >= 4, "VERIFY: no sda10 dumps"
vi = open("/tmp/ifv/init").read()
assert vi.count("init-stage") >= 1 and "dmesg > /dev/block/sda10" in vi, "VERIFY: no marker"
assert "rndis" not in v.replace("rndis_bam","").replace("rndis0",""), "VERIFY: rndis still present"
print("VERIFY OK: ncm + sda10 + marker + busybox + init ton tai trong gz moi")
