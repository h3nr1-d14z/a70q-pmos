# a70q-pmos — postmarketOS on the Samsung Galaxy A70 (a70q)

postmarketOS (Alpine, OpenRC) on the Galaxy A70 (SM-A705F, Snapdragon 675 / SM6150, Adreno 612) with
the downstream Samsung/LineageOS 4.14.190 kernel built from source, the rootfs on UFS (USERDATA
partition) and the Sxmo (sway) UI. Documentation is mostly Vietnamese; English: this file and
**[INSTALL.en.md](INSTALL.en.md)** (install from scratch).

## Status (2026-11-15)

**Hardware**

| Feature | | Notes |
|---|---|---|
| Boot from UFS | ✅ | kernel reaches userspace at 1.9 s; SSH over USB ~16 s, Sxmo ~15 s, WiFi ~31 s |
| Display (DRM/KMS) | ✅ | DTBO without the SDE writeback connector |
| GPU Adreno 612 | ✅ | turnip-kgsl (Vulkan 1.3) + Zink (GL/GLES); sway renders with Vulkan (patched wlroots) |
| Touch, buttons | ✅ | |
| Audio | ✅ | speaker, wired headphones (jack switching), main mic; PipeWire + UCM |
| WiFi (WCN3990 on the modem DSP) | ✅ | NetworkManager, auto-connect; hotspot from the Sxmo Networks menu |
| Bluetooth (WCN3988) | ✅ | pairing, A2DP, OBEX file receive; paired headphones auto-reconnect (not yet tested with real headphones) |
| Suspend / resume | ✅ | power key wakes; WiFi stays connected |
| CPU/bus frequency scaling | ✅ | schedutil + bw_hwmon/mem_latency with the vendor's values (kernel defaults kept everything at max clock) |
| Sensors | ✅ | accelerometer (auto-rotate), light (auto brightness), proximity, compass via the ADSP Sensor Core |
| Flashlight, vibration, brightness | ✅ | no notification LED on this phone |
| microSD, USB OTG | ✅ | hot-plug, automount to `/media/<label>`; OTG mouse/hub/USB LAN tested |
| GPS | 🟡 | QMI LOC engine runs, no satellites seen indoors yet |
| Calls / SMS / mobile data | ⏸ | not started (no SIM in the test phone) |
| Camera, fingerprint, FM radio, NFC | ❌ | need Android HALs / not wired on the SM-A705F |
| Hardware video decoding (venus) | ❌ | firmware loads, but the downstream driver does not work with ffmpeg/GStreamer; the CPU decodes 1080p at 7–9× real time |

**Usability (Sxmo additions in the device package)**
- Wider, touch-friendly menus with a **Back** entry in sub-menus; dark theme; notifications styled like the menus.
- Clickable status bar: WiFi %, battery %, Bluetooth, CPU temperature, VI/EN input method, quick settings.
- Quick settings (two-finger swipe down, or tap the status circle): WiFi, Bluetooth, flashlight,
  rotation, brightness, auto brightness, input method, screenshot, media controls.
- On-screen keyboard shows/hides with text-field focus (fcitx5 Lua addon); Vietnamese input (fcitx5-unikey).
- Power + Volume Down screenshot; automatic battery saver below 20%; UI scale in the Config menu;
  gesture/button cheat sheet.
- Epiphany browser (ad blocking on; WebKit sandbox unavailable on 4.14, see `ERRORS.md` C64).

## Layout

| Path | Contents |
|---|---|
| `INSTALL.en.md` / `INSTALL.md` | install from scratch (English / Vietnamese) |
| `pmos/pmaports/device/downstream/` | packages: `device-samsung-a70q`, `linux-samsung-a70q`, `a70q-qcom-userspace`, `mesa-vulkan-freedreno-kgsl`, `wlroots0.20`, `libssc` |
| `pmos/pmb/` | pmbootstrap in Docker |
| `pmos/tools/` | `prepare-install.sh`, `mkboot.sh`, `flash.sh` (safe heimdall wrapper for macOS), `repo.sh` (package repo), boot/dtbo image tools |
| `pmos/kernel/` | fast kernel builds in Docker + kernel patches |
| `ERRORS.md` | every problem met: symptom → cause → fix (Vietnamese) |
| `pmos/research/` | research notes (e.g. mainline kernel feasibility) |
