# Installing postmarketOS on the Samsung Galaxy A70 (a70q) from scratch

This guide takes a Galaxy A70 running LineageOS 22.2 to postmarketOS (Sxmo UI) using the packages in
this repository. Feature status: `README.en.md`; technical details: `pmos/README.md` (Vietnamese);
root causes and fixes of every problem: `ERRORS.md` (Vietnamese; the `Cxx` codes below refer to it).

> **Warning.** Flashing **wipes the whole USERDATA partition** (photos, apps, accounts...).
> Tested on exactly one phone, an SM-A705F/DS (Vietnamese XXV region). Unlocking the bootloader trips
> Knox (0x1) permanently. Use at your own risk.

Legend: ✅ done on the real phone, 🧪 re-run from an empty Docker volume (no flashing), ⚠️ not tested.

## 1. Requirements

**Phone**
- Galaxy A70 SM-A705F with an **unlocked bootloader**.
- Running the unofficial **LineageOS 22.2** by rtd1250
  ([rtd1250/a70q_OTA](https://github.com/rtd1250/a70q_OTA), build 20260607), installed on top of the
  last stock Android 11 firmware `A705FXXU5DXD2`, and booted at least once. Why:
  - postmarketOS reuses the Samsung/Qualcomm firmware in the `vendor`, `apnhlos`, `modem` and
    `persist` partitions (modem, WiFi, Bluetooth, GPU, sensors). This repository contains no firmware.
  - Samsung's bootloader rejects boot images whose `os_version`/security patch level is older than the
    last booted one (C12), so the postmarketOS boot.img copies its header from LineageOS' boot.img.
- Keep the LineageOS zip you installed (`lineage-22.2-a70q.zip`): its `boot.img` and `dtbo.img` are needed.
- Battery above 50%.

**Computer**
- ✅ macOS on Apple Silicon + Docker Desktop (everything runs natively as arm64 in containers).
  ⚠️ Linux arm64 + Docker should work as is; Linux x86_64 needs qemu-user/binfmt for arm64
  containers (very slow) — not tested.
- About 20 GB free for the Docker volume (pmbootstrap, kernel build cache).
- **heimdall** to flash in Download Mode:
  - macOS: [heimdall-apple-silicon](https://github.com/aljosasavic/heimdall-apple-silicon) v2.2.2
    (fixes libusb "pipe stalled" on Apple Silicon), at `~/bin/heimdall` or pointed to with
    `HEIMDALL=/path/to/heimdall`.
  - Linux: your distribution's `heimdall` package.
- A good USB-C cable. On a Mac mini use a **rear port**: the front ports once left a stuck "ghost"
  Download Mode device that made heimdall hang (C23).

## 2. Build the packages and the image

```sh
git clone https://github.com/h3nr1-d14z/a70q-pmos && cd a70q-pmos

# 1) the 6 device packages: kernel, firmware loader/modem userspace, turnip-kgsl, patched wlroots,
#    patched libssc, device package. The first run builds the a70q-pmb Docker image
#    (pmbootstrap 3.12.1) and clones pmaports.
pmos/pmb/run.sh build --arch aarch64 linux-samsung-a70q a70q-qcom-userspace \
    mesa-vulkan-freedreno-kgsl wlroots0.20 libssc device-samsung-a70q

# 2) rootfs + image with the Sxmo UI, plus the Epiphany browser and btop.
#    <password> is the password of the user "user" (SSH login, sudo).
PMB_UI=sxmo-de-sway pmos/pmb/run.sh install --password '<password>' --add epiphany,btop
```

🧪 Both commands were re-run from an empty Docker volume (2026-11-14, Mac mini M4): step 1 took about
23 minutes (mostly the kernel and mesa), step 2 a few minutes. Everything lives in the Docker volume
`a70q-pmbwork`; set `PMB_VOLUME=<name>` to use another one.

**If step 2 fails with `Connection aborted` / `unexpected end of file`** while downloading packages: the
default postmarketOS mirror (geo-redirected) can be very slow from inside the container (the test run
got ~15 KB/s from `mirror.nura.eco`). Pick another one from the
[mirror list](https://mirrors.postmarketos.org) and re-run, e.g.:

```sh
PMB_MIRROR=http://ftp.halifax.rwth-aachen.de/postmarketos/ PMB_UI=sxmo-de-sway \
    pmos/pmb/run.sh install --password '<password>' --add epiphany,btop
```

## 3. Prepare the images to flash

```sh
mkdir -p los && unzip -o lineage-22.2-a70q.zip boot.img dtbo.img -d los
pmos/tools/prepare-install.sh --like los/boot.img --dtbo los/dtbo.img
```

🧪 Run on the output of step 2 from an empty volume, with `boot.img`/`dtbo.img` from the LineageOS 22.2
20260607 zip: the boot header keeps `15.0.0 / 2026-06`, the cmdline is 430/511 bytes (512-byte limit,
C16), and `dtbo.img` is byte-identical to the DTBO running on the test phone. The script writes to
`pmos/out/install/`:

| File | Contents | Partition |
|---|---|---|
| `boot.img` | newest kernel + initramfs and partition UUIDs of the installed rootfs, header copied from LineageOS' boot | BOOT |
| `dtbo.img` | LineageOS dtbo without the writeback connector (without it sway gets no display, C49) | DTBO |
| `vbmeta.img` | empty vbmeta, flags=2 (verification disabled) | VBMETA |
| `userdata.img` | the postmarketOS image (boot + root partitions) as an Android sparse image; heimdall dies at 7% with a raw image (C33) | USERDATA |

## 4. Flash

1. Enter **Download Mode**: power off, hold **Volume Up + Volume Down** and plug the USB cable into the
   computer, then press **Volume Up** on the warning screen.
2. Flash all four partitions at once:

```sh
pmos/tools/flash.sh --BOOT pmos/out/install/boot.img --DTBO pmos/out/install/dtbo.img \
    --VBMETA pmos/out/install/vbmeta.img --USERDATA pmos/out/install/userdata.img
```

`flash.sh` waits for Download Mode through the IORegistry (never `heimdall detect`, which kicks the phone
out of Download Mode, C21), retries when the first handshake stalls (C24), and logs to
`/tmp/a70q-flash-*.log`. The phone reboots when done. (The IORegistry wait is macOS-specific; on Linux
check `flash.sh` before using it.)

✅ This procedure (BOOT, DTBO, VBMETA, USERDATA with heimdall and a sparse image) is how the test phone
was installed.

## 5. First boot

- About 15 seconds after the Samsung logo, Sxmo starts and logs in automatically. On the first boot the
  rootfs grows to fill USERDATA (~109 GB).
- **SSH over USB**: the phone exposes a USB network interface, `ssh user@172.16.42.1` (password from step 2).
- **WiFi**: Sxmo menu (right click on the status bar, or swipe down from the top edge) → Networks, or
  `sudo nmcli device wifi connect '<SSID>' password '<WiFi password>'`.
  The modem DSP and WiFi come up about 30 seconds after boot.
- **Vietnamese input** (optional): `sudo apk add fcitx5 fcitx5-unikey fcitx5-lua`, create
  `~/.config/fcitx5/profile` with a group containing `keyboard-us` + `unikey` (see `pmos/README.md`
  section 6), log in again; the device package starts fcitx5 when it is installed. `fcitx5-lua` makes the
  on-screen keyboard appear when a text field is focused.
- **Receiving files over Bluetooth (OBEX)**, enable once per user:
  `superctl enable a70q-obexd && superctl start a70q-obexd`.
- Gestures and buttons: Quick settings (tap the circle in the status bar) → "Gestures & buttons".

Quick checks over SSH:

```sh
uname -r                                  # 4.14.190
monitor-sensor                            # accelerometer, light, proximity, compass (Ctrl+C to quit)
nmcli device                              # wlan0 connected
bluetoothctl show | grep Powered          # yes
wpctl status | grep -A3 Sinks             # speaker/headphones
```

## 6. Updating later

- Regular packages (Sxmo, apps, libraries): `sudo apk update && sudo apk upgrade`, anywhere with a network.
- The device's own packages (kernel, device package...): rebuild as in step 2, run
  `pmos/tools/repo.sh start` on the computer (serves the repository over HTTP, port 8099), then
  `sudo a70q-upgrade` on the phone. It adds the computer's repository when reachable (USB `172.16.42.2`,
  or a LAN address added to `/etc/a70q/repos`), otherwise it only updates from the internet. ✅
- A kernel updated through apk does **not** write the BOOT partition: build a boot.img with
  `pmos/tools/mkboot.sh` (see `pmos/README.md` section 3) and `flash.sh --BOOT` it. The phone can be sent
  to Download Mode remotely: `sudo reboot-mode download` over SSH. ✅

## 7. Going back to LineageOS

⚠️ Not tested on the phone. Flash the original images from the LineageOS zip, then wipe data with the
LineageOS recovery (the RECOVERY partition is not touched by the postmarketOS install):

```sh
unzip -o lineage-22.2-a70q.zip vbmeta.img -d los
pmos/tools/flash.sh --BOOT los/boot.img --DTBO los/dtbo.img --VBMETA los/vbmeta.img
```

Then boot to recovery (power off, hold **Volume Up + Power**, with USB plugged in if needed) → Factory
reset → Format data.

## 8. Known limitations

- Not working: camera, fingerprint (need Android HALs), FM radio (not enabled by Samsung), NFC (no chip
  answers on the SM-A705F, C66), hardware video decoding (C62; the CPU decodes 1080p fast enough).
- Not done yet: calls/SMS/mobile data; GPS (the engine runs, not yet tested outdoors).
- Epiphany runs without the WebKit sandbox because the 4.14 kernel lacks `openat2` (C64).
