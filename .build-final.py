import struct, subprocess, os, shutil

K = "/work/out/target/product/a70q/obj/KERNEL_OBJ"
DTC = K + "/scripts/dtc/dtc"
DTB = K + "/arch/arm64/boot/dts/qcom/sm6150.dtb"
KERNEL_BLOB = "/work/out/target/product/a70q/kernel"
W = "/tmp/fin"
os.makedirs(W, exist_ok=True)

# 1) decompile dtb hien tai
r = subprocess.run(f"{DTC} -I dtb -O dts -o {W}/cur.dts {DTB} 2>/dev/null", shell=True)
assert r.returncode == 0, "decompile failed"
s = open(W + "/cur.dts").read()

# 2) inject memreserve + ramoops (idempotent)
if "/memreserve/" not in s:
    assert s.startswith("/dts-v1/;")
    s = s.replace("/dts-v1/;",
                  "/dts-v1/;\n/memreserve/ 0x00000000a1300000 0x0000000000100000;", 1)
if "ramoops@a1300000" not in s:
    lines = s.split("\n")
    start = next(i for i, ln in enumerate(lines) if ln.strip() == "reserved-memory {")
    depth = 0
    end = None
    for idx in range(start, len(lines)):
        depth += lines[idx].count("{") - lines[idx].count("}")
        if depth == 0:
            end = idx
            break
    ramoops = [
        "\t\tramoops@a1300000 {",
        "\t\t\tcompatible = \"ramoops\";",
        "\t\t\treg = <0x0 0xa1300000 0x0 0x100000>;",
        "\t\t\trecord-size = <0x40000>;",
        "\t\t\tconsole-size = <0x40000>;",
        "\t\t\tftrace-size = <0x40000>;",
        "\t\t\tpmsg-size = <0x40000>;",
        "\t\t};",
    ]
    lines = lines[:end] + ramoops + lines[end:]
    s = "\n".join(lines)
open(W + "/new.dts", "w").write(s)

# 3) compile lai dtb
r = subprocess.run(f"{DTC} -I dts -O dtb -o {W}/new.dtb {W}/new.dts 2>/dev/null", shell=True)
assert r.returncode == 0, "dtc compile failed"
newdtb = open(W + "/new.dtb", "rb").read()
assert newdtb.count(b"ramoops") >= 2, "ramoops lost in dtb"
print("new dtb:", len(newdtb), "bytes | ramoops refs:", newdtb.count(b"ramoops"))

# 4) kernel blob moi = gz-part (cat truoc dtb cu) + new dtb
old = open(KERNEL_BLOB, "rb").read()
i = old.rfind(struct.pack(">I", 0xD00DFFEED) if False else struct.pack(">I", 0xD00DFEED))
# dtb magic dung = D00DFEED; tim magic CUOI cung trong blob
mag = struct.pack(">I", 0xD00DFEED)
pos = old.rfind(mag)
assert pos > len(old) - 500000, "dtb magic not near tail"
gz_part = old[:pos]
newblob = gz_part + newdtb
open(KERNEL_BLOB, "wb").write(newblob)
print("kernel blob:", len(old), "->", len(newblob))

# 5) cmdline SACH: base (trim) + ramoops + panic=0 + console=tty0
cmd = ("console=null androidboot.hardware=qcom androidboot.memcg=1 "
       "lpm_levels.sleep_disabled=1 swiotlb=1 "
       "androidboot.usbcontroller=a600000.dwc3 "
       "firmware_class.path=/vendor/firmware_mnt/image nokaslr printk.devkmsg=on "
       "loop.max_part=7 console=tty0 "
       "androidboot.boot_devices=soc/1d84000.ufshc "
       "ramoops.mem_address=0xa1300000 ramoops.mem_size=0x100000 "
       "ramoops.console_size=0x40000 ramoops.record_size=0x40000 ramoops.dump_oops=1 "
       "panic=0")
assert len(cmd) <= 511, f"cmdline too long: {len(cmd)}"
print("cmdline:", len(cmd), "chars")

# 6) ramdisk hien tai tu halium-boot + mkbootimg final
r = subprocess.run("python3 /opt/mkbootimg/unpack_bootimg.py --boot_img /work/out/target/product/a70q/halium-boot.img",
                   shell=True, cwd=W, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
assert r.returncode == 0, "unpack halium-boot failed"
r = subprocess.run(
    f"python3 /opt/mkbootimg/mkbootimg.py --kernel {KERNEL_BLOB} "
    f"--ramdisk {W}/out/ramdisk "
    f"--base 0x00000000 --pagesize 4096 --kernel_offset 0x00008000 "
    f"--ramdisk_offset 0x02000000 --tags_offset 0x01e00000 "
    f"--second_offset 0x00f00000 --header_version 1 --board SRPRL06C005 "
    f"--cmdline '{cmd}' --os_version 15.0.0 --os_patch_level 2026-06 "
    f"--output /work/final-halium-boot.img",
    shell=True, capture_output=True)
assert r.returncode == 0, "mkbootimg failed: " + r.stderr.decode()
sz = os.path.getsize("/work/final-halium-boot.img")
print("FINAL halium-boot.img:", sz, "bytes")
