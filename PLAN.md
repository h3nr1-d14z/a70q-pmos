# Kế hoạch port Halium 11 + Ubuntu Touch cho Galaxy A70 (a70q)

Chế độ: **tự thân** — không phụ thuộc liên hệ ai, mọi tài nguyên đều public và đã xác minh sống.
Ngày lập: 2026-11-10. Xem `RESEARCH.md` cho toàn bộ nền tảng nghiên cứu + ma trận tính năng.

## Nguyên tắc

- Mọi thứ phải tự làm được từ docs + source trees public
- Cộng đồng (Telegram @A70download, UBports forum, Matrix #postmarketos) chỉ là **backup**, không phải dependency
- Thứ tự xử lý rủi ro: backup trước → build trên PC → test flash sau cùng (test boot không hỏng gì: fastboot boot/heimdall download sẽ không overwrite nếu chỉ boot image tạm)

## Bảng tài liệu gốc (đã xác minh còn sống 2026-11-10)

| Tài liệu | URL | Vai trò |
|---|---|---|
| Halium: Get sources | https://docs.halium.org/en/latest/porting/get-sources.html | `repo init -b halium-11.0`, local manifest, vendor blobs |
| Halium: Build | https://docs.halium.org/en/latest/porting/build.html | `mka halium-boot` |
| Halium: Install/Debug | https://docs.halium.org/en/latest/porting/install.html | Đẩy boot image, log qua SSH/adb |
| UBports: Porting intro | https://docs.ubports.com/en/latest/porting/introduction/Intro.html | Tổng quan kiến trúc |
| UBports: Build env | https://docs.ubports.com/en/latest/porting/introduction/Setting_up.html | Môi trường build |
| UBports: Kernel build | https://docs.ubports.com/en/latest/porting/build_and_boot/standalone_kernel_build.html | Build kernel standalone + halium patches |
| UBports: Update ports for 20.04 | https://docs.ubports.com/en/latest/porting/UpdatePortsFor2004.html | Checklist halium-9 → focal (usb-moded, overlay, systemd) |
| UBports: Configure/test/fix | https://docs.ubports.com/en/latest/porting/configure_test_fix/index.html | Fix từng thành phần sau khi boot |
| UBports: Finalize | https://docs.ubports.com/en/latest/porting/finalize/UBports_recovery.html | OTA + UBports installer |
| Shared build tools (GitLab) | https://gitlab.com/ubports/community-ports/halium-generic-adaptation-build-tools | build.sh + CI cho device tarball focal |

## Source trees (đã xác minh branch)

**Reference port gần nhất đang chạy (kernel 4.14, Adreno 618 a6xx, halium-10, noble) — LƯU Ý: là SM7125/720G, KHÔNG phải cùng SoC:**

| Repo | Branch | Ghi chú |
|---|---|---|
| https://devices.ubuntu-touch.io/device/curtana | Halium 10.0, release **noble** | Redmi Note 9S (SM7125) — kernel 4.14.288, fingerprint cạnh Working, Waydroid Working |
| https://gitlab.com/ubports/porting/community-ports/android10/xiaomi-redmi-note-9-pro/xiaomi-miatoll | halium-10 | Cấu trúc port halium-10 |
| **https://gitlab.com/ubports/porting/community-ports/android11/nothing-phone-1/nothing-spacewar** | **halium-11, noble** | **Template cấu trúc chính** — halium-11 + noble được chứng minh; kèm kernel-nothing-sm7325 |
| https://github.com/S-8506/halium_device_motorola_sm6150-common | halium-10 | Tham chiếu cùng SoC SM6150 (Android 10 vendor) |

**Halium manifest + a70q trees:**

| Repo | Branch | Ghi chú |
|---|---|---|
| https://github.com/Halium/android | `halium-11.0` (hoặc `halium-10.0` — quyết sau khi xem curtana) | Manifest gốc Halium |
| https://github.com/a70q-lineage/android_device_samsung_a70q | `lineage-18.1` | Device tree Android 11 — nền chính |
| https://github.com/a70q-lineage/android_kernel_samsung_sm6150 | `lineage-18.1` (kiểm tra khi clone) | Kernel 4.14 — cộng halium patches từ curtana kernel |
| https://github.com/a70q-lineage/proprietary_vendor_samsung_a70q | `lineage-18.1` | Vendor blobs |
| https://github.com/a70q-lineage/android_device_samsung_sm6150-common | `lineage-18.1` | Common tree |
| https://github.com/a70q-lineage/proprietary_vendor_samsung_sm6150-common | `lineage-18.1` | Blobs common |
| https://github.com/ubports-a70q/android_device_samsung_a70q | `halium-9` | **Bản đồ tham khảo** — HAL glue đã viết cho máy này |
| https://github.com/ubports-a70q/android_kernel_samsung_a70q | `lineage-16.0` | Kernel cũ đã patch cho UT — tham khảo patches |

## Quyết định kiến trúc (đÃ CHỐT sau nghiên cứu 2026-11-10)

1. **Release: noble (24.04)** — không focal. Bằng chứng: spacewar (Nothing Phone 1) halium-11 + noble đang chạy daily, full camera/calls/Waydroid
2. **Halium: 11.0** — không halium-10. Vendor A70 là Android 11 → khớp; halium-10 chỉ dùng làm tham chiếu kernel patch (curtana/miatoll + Motorola sm6150)
3. **Cấu trúc port**: copy layout từ gitlab.com/ubports/porting/community-ports/android11/nothing-phone-1 (device tarball + kernel repo riêng trên UBports GitLab)

## Các phase

### Phase 0 — Backup & thu thập dữ liệu — ✅ HOÀN THÀNH 100% (2026-11-10)
- [x] Backup EFS: modemst1/2, fsg, fsc, persist → /Volumes/External/Projects/a70-backup/efs/ (2026-10-04, qua base64 pipe)
- [x] Firmware stock đầy đủ: fac.zip 5 tar Odin + stock_boot.img (DXD2) → rollback 100%
- [x] Partition map đầy đủ → artifacts-docs/partition-map.txt
- [x] Bootloader: unlock + Knox 0x1 xác nhận
- [x] **Backup FULL 50 partition** (613 MB, size-verified 100% + sha256) → backups/full/ + backups/MANIFEST.md (gồm boot, recovery, dtbo, vbmeta, param, modem, apnhlos, efs, sec_efs, persist, xbl...)
- [x] **lshal dump đầy đủ 209 dòng** → backups/hals-lshal.txt (audio@6.0, bluetooth@1.0, gnss 2.x, composer 2.x, keymaster 4.0 — bộ HIDL A11 chuẩn cho halium-11)
- [x] **NFC variant: CÓ NFC, chip NXP NQx** (nqx.default) — Nura wiki sai với biến thể XXV
- [x] `adb pull` dtbo (10 MiB) + vbmeta (64 KiB) → backups/

### Phase 1 — Môi trường build — ✅ CHỐT: **Mac mini M4** (Apple Silicon) build trong container Linux arm64; Mac này = flash/debug host
- [x] Repo handoff đã push GitHub (`h3nr1-d14z/ubuntu-touch-a70`, private): README.md handoff + PLAN/RESEARCH/LOG + backup script + lshal dump + manifest; ảnh partition 612MB KHÔNG vào git (tái tạo bằng backups/full-backup.sh)
- [x] **Build kit trong repo**: `build/Dockerfile` (ubuntu 20.04 arm64 + deps Halium), `build/run.sh` (volumes ext4 case-sensitive + ccache + cp ra/vào), `build/manifests/samsung_a70q.xml` (kernel rtd1250 lineage-18.1 + device/vendor a70q-lineage lineage-20 — template, UNTESTED)
- [x] Kiểm tra Mac này: Docker Desktop ✅, Parallels ✅, heimdall-apple-silicon ✅, adb wireless ✅ — giữ làm flash host + fallback build
- [ ] Mac mini: cài Docker Desktop/OrbStack, clone repo, `./build/run.sh build && ./build/run.sh shell` — làm theo README.md mục "Tiếp tục ở máy mới"
- [ ] Trong shell: repo init halium-11.0 → copy local manifest → `repo sync --current-branch --no-clone-bundle --no-tags` (~25–40 GB)
- [ ] Vendor blobs: dùng trees `proprietary_vendor_samsung_a70q` + `_sm6150-common` có sẵn trong git; extract-files từ device qua adb chỉ khi muốn khớp DXD2 mới hơn
- Lưu ý Mac mini M4 RAM 16GB: `--memory=12g` cho container (override qua HALIUM_MEM); disk image Docker ≥ 60GB

### Phase 2 — Kernel
- [ ] Clone kernel: chọn branch `lineage-18.1` (rtd1250 repo, era A11 khớp halium-11) hoặc `lineage-22.2` (a70q-lineage, mới nhất — đang chạy thật trên máy)
- [ ] Áp halium patches (binder/ashmem/vendor cmdlines) — tham khảo patch trong `ubports-a70q/android_kernel_samsung_a70q` cũ + docs standalone_kernel_build
- [ ] **Fix Bluetooth Samsung (đã verify 2026-11-10): uncomment trong `net/bluetooth/Makefile`: `rfcomm/`, `bnep/`, `hidp/`** — hiện bị comment; bluez trên UT cần kernel RFCOMM (HFP call audio/tethering/HID). `hci_sock.c` đã kiểm tra nguyên vẹn, không cần sửa
- [ ] Check `.gitignore` (pattern `vmlinux*` không anchor) + không tin `/proc/config.gz` (verify bằng runtime)
- [ ] Toolchain cùng era kernel (clang 8/9, không clang mới — clang 14 gây hang boot theo kinh nghiệm note10lite)
- [ ] Chọn defconfig, build `hybris-mer-kernel` (hoặc standalone)
- [ ] Sản phẩm: `boot.img` halium boot được

### Phase 3 — Halium build
- [ ] `source build/envsetup.sh && breakfast a70q && mka halium-boot`
- [ ] Fix lỗi build theo docs; sản phẩm: `halium-boot.img`

### Phase 4 — Adapt device tree lên Halium 11
- [ ] So sánh tree `lineage-18.1` với tree `halium-9` cũ: chuyển lại các glue (init scripts, fstab, sepolicy, overlays) sang cấu trúc halium-11
- [ ] Đổi overlay theo docs UpdatePortsFor2004 (usb-moded, `.halium-overlay-dir`, systemd units thay Upstart)
- [ ] Xây device tarball bằng `halium-generic-adaptation-build-tools` (build.sh + cấu trúc GitLab community-ports)

### Phase 5 — Boot test (đã có rollback 100% từ repo a70-root)
- [ ] Flash qua **heimdall-apple-silicon v2.2.2** (đã cài, đã flash 6 phiên thành công) — lưu ý quirk: **1 lệnh flash / lần Download Mode**
- [ ] Đẩy `halium-boot.img` vào `boot` partition (a70q có RECOVERY riêng — flash cả 2 nếu cần); **KHÔNG quên vbmeta** (flash kèm hoặc `--disable-verification`) — verified boot sẽ fail `Hash of data does not match` nếu thiếu
- [ ] Rollback tức thời nếu hỏng: `heimdall flash --BOOT lineage-boot-orig.img` (về LineageOS) hoặc `--BOOT stock_boot.img` (về stock Samsung) — file đã có tại /Volumes/External/Projects/
- [ ] Boot halium rootfs reference → SSH/adb (wireless adb ổn định hơn USB trên Mac này) → kiểm log
- [ ] Tiêu chí qua phase: kernel boot + Android container chạy (logcat không crash loop)

### Phase 6 — Bring-up từng tính năng (theo thứ tự rủi ro tăng dần)
Làm theo ma trận trong `RESEARCH.md`, thứ tự khuyến nghị:
1. Màn hình + touch + backlight
2. GPU render (confirm hwcomposer bridge — nếu software-render thì tìm drm/gralloc path)
3. Wi-Fi → Bluetooth
4. Audio (loa/mic)
5. RIL: data → SMS → calls
6. Cảm biến + haptics + lights
7. GPS (geoclue-provider-hybris + izat configs từ tree cũ)
8. NFC (nfcd + libnfc configs)
9. Camera (droid-camres, kỳ vọng 2/3 lens)
10. VoLTE (toggle trong Settings > Cellular — nếu có toggle thì IMS A11 sống)
11. Offline charging, MMS, battery %

### Phase 7 — Hoàn thiện
- [ ] UBports recovery + installer config
- [ ] OTA channel riêng hoặc đóng PR vào UBports community-ports GitLab
- [ ] Ghi lại toàn bộ vào repo này (mỗi phase 1 commit + log)

## Rủi ro chính & đối sách tự thân

| Rủi ro | Đối sách |
|---|---|
| Không có ai đã làm halium-11 cho máy này | Tree `halium-9` cũ + tree `lineage-18.1` mới + docs đủ; tham khảo port halium-11 Samsung QCOM khác trên GitLab community-ports (tìm `samsung halium` khi tới Phase 4) |
| Kernel 4.14 thiếu patch cho halium-11 | Patch set trong kernel cũ + diff với kernel halium-11 khác (Motorola sm6150 của S-8506, halium-10 — gần nhất) |
| VoLTE không chạy trên vendor A11 | Fallback: 3G calls (mạng VN còn 2G/3G tùy nhà mạng); theo dõi UBports mở rộng IMS |
| Vân tay/desktop-out không thể có | Chấp nhận trước (blocked nền tảng/phần cứng) — đã ghi trong ma trận |
| Build env trên macOS | Không build trên Mac — cần Linux x86_64 |

## Trạng thái hiện tại

- [x] Nghiên cứu toàn cảnh (RESEARCH.md)
- [x] Xác minh docs + source trees
- [ ] Phase 0 backup
- [ ] Phase 1+ build
