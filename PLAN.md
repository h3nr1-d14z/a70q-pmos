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
| https://github.com/a70q-lineage/android_device_samsung_a70q | `lineage-22.2` (không có 18.1; 20/21/22.2) | Device tree — nền chính, adapt lên halium-11 ở Phase 4 |
| https://github.com/rtd1250/android_kernel_samsung_sm6150 | `lineage-18.1` (đã có trên repo rtd1250; a70q-lineage chỉ có 20/21/22.2/upstream) | Kernel 4.14 — cộng halium patches từ curtana kernel |
| https://github.com/a70q-lineage/proprietary_vendor_samsung_a70q | `lineage-22.2` (chỉ còn 21/22.2 — bảng cũ ghi 18.1 lỗi thời, check API 2026-11-10) | Vendor blobs |
| https://github.com/a70q-lineage/android_device_samsung_sm6150-common | `lineage-22.2` | Common tree |
| https://github.com/a70q-lineage/proprietary_vendor_samsung_sm6150-common | `lineage-22.2` (chỉ còn 21/22.2) | Blobs common |
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

### Phase 1 — Môi trường build — ✅ CHỐT: **Mac mini M4** build trong container **linux/amd64 qua Rosetta (OrbStack)**; Mac này = flash/debug host
- [x] Repo handoff đã push GitHub (`h3nr1-d14z/ubuntu-touch-a70`, private): README.md handoff + PLAN/RESEARCH/LOG + backup script + lshal dump + manifest; ảnh partition 612MB KHÔNG vào git (tái tạo bằng backups/full-backup.sh)
- [x] **Build kit trong repo**: `build/Dockerfile` (ubuntu 20.04 arm64 + deps Halium), `build/run.sh` (volumes ext4 case-sensitive + ccache + cp ra/vào), `build/manifests/samsung_a70q.xml` (kernel rtd1250 lineage-18.1 + device/vendor a70q-lineage lineage-20 — template, UNTESTED)
- [x] Kiểm tra Mac này: Docker Desktop ✅, Parallels ✅, heimdall-apple-silicon ✅, adb wireless ✅ — giữ làm flash host + fallback build
- [x] Mac mini: cài **OrbStack** (brew cask; docker CLI ở `~/.orbstack/bin`), `./build/run.sh build` OK 2026-11-10 — **2 fix arm64 đã commit vào Dockerfile** (bỏ multilib x86_64-only; mkbootimg/avbtool clone từ AOSP thay vì PyPI). Lưu ý VM OrbStack mặc định 8 GB RAM — kiểm lại khi `mka`
- [ ] repo init halium-11.0 + local manifest **ĐANG CHẠY nền** (container `halium-sync`, volume `halium-src`) → theo dõi tới khi xong (~25–40 GB)
- [ ] Vendor blobs: dùng trees `proprietary_vendor_samsung_a70q` + `_sm6150-common` có sẵn trong git; extract-files từ device qua adb chỉ khi muốn khớp DXD2 mới hơn
- Lưu ý Mac mini M4 RAM 16GB: `--memory=12g` cho container (override qua HALIUM_MEM); disk image Docker ≥ 60GB

### Phase 2 — Kernel
- [x] Clone kernel: **rtd1250 `lineage-18.1`** (sync xong 2026-11-10, commit `0b4c66a2b` — đã fix cảnh báo clang mới)
- [ ] Áp halium kernel patches: **đã thẩm định — defconfig 18.1 sẵn sàng**: `CONFIG_ASHMEM=y`, `CONFIG_ANDROID_BINDER_IPC=y` (binder,hwbinder,vndbinder), KHÔNG có `CONFIG_ANDROID_PARANOID_NETWORK` → không cần patch mer-style thêm; đánh giá lại sau khi `mka halium-boot` chạy scripts/check-kernel-config
- [x] **Fix Bluetooth Samsung — XONG trong tree (2026-11-10)**: `net/bluetooth/Makefile` uncomment `rfcomm/`, `bnep/`, `hidp/` + `a70q_defconfig`: `CONFIG_BT_RFCOMM=y`, `CONFIG_BT_RFCOMM_TTY=y`, `CONFIG_BT_BNEP=y`, `CONFIG_BT_BNEP_MC_FILTER=y`, `CONFIG_BT_BNEP_PROTO_FILTER=y`, `CONFIG_BT_HIDP=y` (defconfig cũng đang tắt — uncomment Makefile một mình chưa đủ)
- [x] Check `.gitignore`: 18.1 đã anchor (`/vmlinux`, `/vmlinux.32`) — cảnh báo note10lite KHÔNG áp dụng cho tree này
- [x] Toolchain: **CHỐT 2026-11-11 — container `linux/amd64` qua Rosetta (image `halium-a70q:amd64`)**. Lý do: `prebuilts/clang/host/` chỉ có `linux-x86`/`darwin-x86` — route native-arm64 chết ở tầng build system, không chỉ kernel. `breakfast` đã PASS với HOST_ARCH=x86_64. Hiệu năng: 1.5–2x chậm hơn native, chấp nhận cho correctness; nếu sau này muốn nhanh, route (1) gcc-aarch64 native chỉ đứng riêng được kernel
- [x] Chọn defconfig `a70q_defconfig` (BoardConfig đã trỏ đúng `TARGET_KERNEL_SOURCE`/`CONFIG`), build kernel
- [x] Sản phẩm: `boot.img` halium boot được — **KERNEL BUILD XONG 2026-11-11**: `4.14.261-lineage-g0b4c66a2b87b-dirty`, 8 modules, RFCOMM/BNEP/HIDP built-in (317 symbols trong vmlinux), ~18 phút qua Rosetta

### Phase 3 — Halium build — ✅ HOÀN THÀNH 2026-11-11
- [x] `source build/envsetup.sh && breakfast a70q && ALLOW_MISSING_DEPENDENCIES=true mka halium-boot`
- [x] Sản phẩm: **`out/target/product/a70q/halium-boot.img`** (20,168,704 bytes ≈ 19.2 MiB, magic `ANDROID!`, header v1, sha256 `b160a7b7…fc408`) — đã copy ra host `out/halium-boot.img`. Toàn bộ lỗi build trong quá trình nằm ở `ERRORS.md` C1–C11/D1–D4

### Phase 4 — Adapt device tree lên Halium 11
- [ ] So sánh tree `lineage-18.1` với tree `halium-9` cũ: chuyển lại các glue (init scripts, fstab, sepolicy, overlays) sang cấu trúc halium-11
- [ ] Đổi overlay theo docs UpdatePortsFor2004 (usb-moded, `.halium-overlay-dir`, systemd units thay Upstart)
- [ ] Xây device tarball bằng `halium-generic-adaptation-build-tools` (build.sh + cấu trúc GitLab community-ports)

### Phase 5 — Boot test (đã có rollback 100% từ repo a70-root)
- [x] **heimdall-apple-silicon v2.2.2 đã cài trên MÁY NÀY** (`~/bin/heimdall`, binary arm64 từ GitHub aljosasavic; brew cask heimdall-suite bị disable — xem ERRORS.md D4). Quirk proven: **1 lệnh flash / lần Download Mode**
- [ ] Đẩy `halium-boot.img` vào `boot` partition (a70q có RECOVERY riêng — flash cả 2 nếu cần); **KHÔNG quên vbmeta** (flash kèm hoặc `--disable-verification`) — verified boot sẽ fail `Hash of data does not match` nếu thiếu
- [x] **Rollback tức thời có sẵn TRÊN MÁY NÀY** (backup 2026-11-10, sha256-verified): `backups/full/boot.img` (LineageOS 22.2 đang chạy), `backups/full/vbmeta.img`, `backups/full/dtbo.img`, `backups/full/recovery.img` → `heimdall flash --BOOT backups/full/boot.img --VBMETA backups/full/vbmeta.img` là đủ về trạng thái hiện tại. **Stock Samsung (fac.zip 5 tar Odin) chỉ có ở máy cũ** — chỉ cần nếu rollback về stock hoàn toàn
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
