import gzip, subprocess, os, shutil, sys

HOST = "/host"
W = "/tmp/mk2"
os.makedirs(W + "/ifx", exist_ok=True)

def sh(cmd, cwd=None, inp=None):
    r = subprocess.run(cmd, shell=True, cwd=cwd, input=inp,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert r.returncode == 0, f"FAIL: {cmd}\n{r.stderr.decode(errors='replace')}"
    return r

# 1) extract halium initramfs goc (347 files da biet truoc)
sh("rm -rf /tmp/mk2/ifx")
os.makedirs(W + "/ifx", exist_ok=True)
sh("cpio -idm", cwd=W + "/ifx", inp=gzip.open(HOST + "/.tmp-initrd-orig.gz", "rb").read())
assert os.path.exists(W + "/ifx/bin/busybox"), "no busybox in orig initramfs"
assert os.path.exists(W + "/ifx/init"), "no init"
print("orig initramfs extracted:", sum(len(f) for _, _, f in os.walk(W + "/ifx")), "files")

# 2) libs can cho busybox (dynamic hay static?)
r = subprocess.run("strings bin/busybox | grep -m1 ld-linux", shell=True,
                   cwd=W + "/ifx", stdout=subprocess.PIPE)
needs_ld = b"ld-linux" in r.stdout
print("busybox needs ld-linux:", needs_ld)

# 3) dung minimal initramfs
base = W + "/mini"
sh("rm -rf /tmp/mk2/mini && mkdir -p /tmp/mk2/mini/bin /tmp/mk2/mini/dev /tmp/mk2/mini/proc /tmp/mk2/mini/sys /tmp/mk2/mini/config")
shutil.copyfile(W + "/ifx/bin/busybox", base + "/bin/busybox")
os.chmod(base + "/bin/busybox", 0o755)
if needs_ld:
    import glob
    for lib in glob.glob(W + "/ifx/lib/aarch64-linux-gnu/ld-*"):
        shutil.copyfile(lib, base + "/lib/" + os.path.basename(lib))
        os.makedirs(base + "/lib/aarch64-linux-gnu", exist_ok=True)
        shutil.copyfile(lib, base + "/lib/aarch64-linux-gnu/" + os.path.basename(lib))
    for lib in glob.glob(W + "/ifx/lib/aarch64-linux-gnu/libc.so*"):
        shutil.copyfile(lib, base + "/lib/aarch64-linux-gnu/" + os.path.basename(lib))
    for lib in glob.glob(W + "/ifx/lib/ld-linux-aarch64*"):
        shutil.copyfile(lib, base + "/lib/" + os.path.basename(lib))
for app in ["sh", "mount", "mkdir", "cat", "dd", "echo", "grep", "ifconfig", "sleep",
            "sync", "dmesg", "reboot", "ls", "sed", "switch_root", "pivot_root",
            "telnetd", "mdev", "umount"]:
    os.symlink("busybox", base + "/bin/" + app)

init_script = """#!/bin/busybox sh
/bin/busybox mkdir -p /proc /sys /dev /config
/bin/busybox mount -t proc none /proc
/bin/busybox mount -t sysfs none /sys
/bin/busybox mount -t devtmpfs none /dev 2>/dev/null
/bin/busybox mount -t configfs none /config 2>/dev/null
M=/bin/busybox
$M echo "minitest: /init reached" > /dev/block/sda10 2>/dev/null
$M dmesg > /dev/block/sda10 2>/dev/null
$M echo "minitest: kernel live, building gadget" > /dev/block/sda10 2>/dev/null
mkdir -p /config/usb_gadget/g1 2>/dev/null
echo 0x18D1 > /config/usb_gadget/g1/idVendor 2>/dev/null
echo 0xD001 > /config/usb_gadget/g1/idProduct 2>/dev/null
mkdir -p /config/usb_gadget/g1/strings/0x409 2>/dev/null
echo "minitest" > /config/usb_gadget/g1/strings/0x409/product 2>/dev/null
mkdir /config/usb_gadget/g1/functions/ncm.usb0 2>/dev/null
mkdir /config/usb_gadget/g1/configs/c.1 2>/dev/null
ln -sf /config/usb_gadget/g1/functions/ncm.usb0 /config/usb_gadget/g1/configs/c.1/f1 2>/dev/null
U=$(ls /sys/class/udc 2>/dev/null | head -1)
echo "$U" > /config/usb_gadget/g1/UDC 2>/dev/null
$M sleep 2
$M ifconfig usb0 192.168.2.15 netmask 255.255.255.0 up 2>/dev/null
$M telnetd -b 192.168.2.15:23 -l /bin/busybox 2>/dev/null
$M echo "minitest: telnetd up $U 192.168.2.15:23" > /dev/block/sda10 2>/dev/null
exec /bin/busybox sh
"""
open(base + "/init", "w").write(init_script)
os.chmod(base + "/init", 0o755)

# 4) repack minimal initramfs
r = sh("find . | cpio -o -H newc | gzip -9", cwd=base)
mini = r.stdout
open(HOST + "/out/minimal-initramfs.gz", "wb").write(mini)
print("minimal initramfs:", len(mini), "bytes")

# 5) testE: kernel halium hien tai + minimal initramfs + cmdline hien tai
r = sh("python3 /opt/mkbootimg/unpack_bootimg.py --boot_img /work/out/target/product/a70q/halium-boot.img",
       cwd=W)
kernel = open(W + "/out/kernel", "rb").read()
r = sh(f"python3 /opt/mkbootimg/mkbootimg.py --kernel {W}/out/kernel "
       f"--ramdisk /tmp/mk2/minimal.gz "
       f"--base 0x00000000 --pagesize 4096 --kernel_offset 0x00008000 "
       f"--ramdisk_offset 0x02000000 --tags_offset 0x01e00000 "
       f"--second_offset 0x00f00000 --header_version 1 --board SRPRL06C005 "
       f"--cmdline \"$(cat /work/device/samsung/sm6150-common/BoardConfigCommon.mk | grep -m1 '^BOARD_KERNEL_CMDLINE :=' | cut -d= -f2- ) androidboot.boot_devices=soc/1d84000.ufshc ramoops.mem_address=0xa1300000 ramoops.mem_size=0x100000 ramoops.console_size=0x40000 ramoops.record_size=0x40000 ramoops.dump_oops=1 panic=0 \" "
       f"--os_version 15.0.0 --os_patch_level 2026-06 --output /host/out/testE.img")
print("testE.img:", os.path.getsize(HOST + "/out/testE.img"))
