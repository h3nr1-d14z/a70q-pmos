import gzip, subprocess, os
gz = "/work/device/samsung/a70q/initramfs.gz"
raw = gzip.open(gz, "rb").read()
os.makedirs("/tmp/iq2", exist_ok=True)
r = subprocess.run("cpio -idm", shell=True, cwd="/tmp/iq2", input=raw,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
assert r.returncode == 0, "extract failed"
init = "/tmp/iq2/init"
s = open(init).read()
block = '''
# halium-log: stage marker + early kernel log vao MISC (sda10)
echo "init-stage: /init reached" > /dev/block/sda10 2>/dev/null
dmesg > /dev/block/sda10 2>/dev/null
'''
assert block in s, "pre-mount marker block not found"
s = s.replace(block, "\n", 1)
mnt = "mount -t devtmpfs -o nosuid,mode=0755 udev /dev"
assert mnt in s, "devtmpfs mount not found"
newblock = mnt + """
# halium-log: SAU mount /dev - marker + kernel log vao MISC (sda10)
echo "init-stage: /init reached, dev mounted" > /dev/block/sda10 2>/dev/null
dmesg > /dev/block/sda10 2>/dev/null"""
s = s.replace(mnt, newblock, 1)
open(init, "w").write(s)
r = subprocess.run("find . | cpio -o -H newc | gzip -6", shell=True,
                   cwd="/tmp/iq2", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert r.returncode == 0, "repack failed"
open(gz, "wb").write(r.stdout)
print("initramfs: marker moved post-devtmpfs-mount,", os.path.getsize(gz), "bytes")
