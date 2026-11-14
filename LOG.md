# Nhật ký nghiên cứu (LOG.md)

Mục đích: ghi lại mọi phát hiện theo thời gian để không quên. Chi tiết tổng hợp nằm ở RESEARCH.md (ma trận tính năng) và PLAN.md (kế hoạch phase).

## 2026-11-10 — Nghiên cứu mở đầu
- Kết nối ADB wireless thành công (SM-A705F, a70q)
- Máy chạy LineageOS 22.2 (rtd1250), vendor A11 stock nguyên vẹn → Halium 11 khả thi
- Tìm thấy: port UT cũ Alpha (2022, halium-9, org ubports-a70q — 4 repo còn sống); Nura = postmarketOS, port mainline testing cho a70q; PureOS loại (Librem 5 only)
- ma trận 18 tính năng + lộ trình 7 phase → xem RESEARCH.md, PLAN.md
- OP cũ MacTavishAO: GitHub 0 hoạt động → coi như unreachable, tự thân
- Template cùng SoC tìm thấy: curtana (RN9S), joyeuse (RN9P), gram (Poco M2 Pro) — SM6150, kernel 4.14, UT chính thức halium-10 trên release **noble**
- UT đã chuyển release model sang 24.04 (noble); 24.04-2.1 RC4 (09/2026)
- OpenStore có: Quake II Touch, moonlight-qt (steam streaming); Waydroid chính thức trên UT
- Adreno 612 GPU mainline: patch v6 01/2026, enable trong kernel 6.19-rc3 (QCS615)
- WCD9370 codec đã upstream (2024); WSA881x upstream
- crDroid A14 (discontinued) + BlissROM official cho a70q (CustomRomBay)

## 2026-11-10 — Vòng nghiên cứu: Noble vs focal, Halium 10 vs 11

- **SỬA SAI**: curtana/joyeuse/gram là **Snapdragon 720G (SM7125)**, KHÔNG phải SM6150. Vẫn là tham chiếu gần nhất đang chạy được (kernel 4.14, Adreno 618 cùng họ a6xx, halium-10, noble stable 24.04-1.4, commit 17/09/2026)
- Tham chiếu cùng SoC đúng nghĩa: Motorola sm6150 (S-8506) — halium-10, Android 10 vendor
- **VoLTE UT = experimental toàn cục** cho mọi Halium devices — global issue gitlab ubports/ubuntu-touch#1335. Pixel 3a halium-12 VoLTE test thread (07/2026) → hướng VoLTE gắn với halium mới hơn
- Curtana noble: fingerprint (vân tay cạnh) **Working**; Wireless external monitor Working; Waydroid Working → cần xác minh lại "UT không có fingerprint"
- Port repo curtana: gitlab.com/ubports/porting/community-ports/android10/xiaomi-redmi-note-9-pro/xiaomi-miatoll (halium-10, cấu trúc port template chuẩn)

### KẾT LUẬN vòng này (2 quyết định mở đã chốt)
- **Noble (24.04), KHÔNG focal**: spacewar (Nothing Phone 1) = halium-11 + noble daily, commit 10/11/2026, camera/calls/NFC/Waydroid/NFC Working → halium-11 trên noble được chứng minh
- **Halium 11.0, KHÔNG halium-10**: vendor A70 là A11 → khớp tự nhiên; halium-10 templates chỉ dùng làm tham chiếu kernel patch
- **Template cấu trúc port**: gitlab.com/ubports/porting/community-ports/android11/nothing-phone-1/nothing-spacewar (+ kernel-nothing-sm7325) — copy layout này cho a70q
- **Vân tay lần 2**: UT noble CÓ biometryd/libfprint (curtana vân tay cạnh = Working). In-display quang học chưa có precedent chạy (spacewar in-display = Not Working). A70 FOD Samsung → tự bridge biometryd ↔ HAL Samsung 2.1 cũ, chưa chắc kết quả
- VoLTE: global issue ubports/ubuntu-touch#1335, experimental mọi Halium device; Pixel 3a halium-12 VoLTE test 07/2026 → nếu muốn VoLTE chắc hơn, cân nhắc thử halium-12 sau khi halium-11 chạy

### Phát hiện: port Samsung Halium 2026 với docs đầy đủ (note10lite-droidian)
- **note10lite-droidian/docs** (GitHub, tạo 07/2026): Droidian (Debian+Phosh) trên Note10 Lite — Halium port Samsung, "usable daily-driver-ish", docs STATUS/INSTALL/KNOWN-ISSUES/research porting-notes đầy đủ
- Chứng minh Droidian khả thi trên Samsung Halium (đọc vendor libs runtime từ vendor partition, không redistribute blobs)
- **Quirks Samsung áp dụng cho a70q** (nguồn porting-notes.md của họ):
  1. `/proc/config.gz` có thể NÓI DỐI (snapshot stale) — verify bằng runtime evidence (`/proc/net/protocols`, `nm vmlinux | grep`), không tin config.gz
  2. Samsung hay comment-out Makefile Bluetooth (`obj-$(CONFIG_BT_RFCOMM)` bị `#`) + rút ruột `hci_sock.c` → CONFIG_BT=y mà BT chết. **Phải check `net/bluetooth/Makefile` trong kernel a70q ngay Phase 2**
  3. Boot image Samsung: Exynos cần DT TABLE magic d7b7ab1e (a70q là Qualcomm — dtbo partition kiểu Android chuẩn, nhưng đọc reference/boot-image.md của họ khi tới Phase 3)
  4. **Heimdall: 1 lệnh flash / lần Download Mode**
  5. **Flash boot.img phải kèm vbmeta** (verified boot hash) — hoặc vbmeta với --disable-verification
  6. Ghi userdata qua heimdall gãy ~5% → dùng TWRP + netcat/telnet path
  7. Composer HAL Android = single client; restart HAL qua `lxc-attach -- setprop ctl.restart vendor.hwcomposer-*` — KHÔNG restart nguyên container lxc@android (chết Wi-Fi/radio HAL)
  8. Build kernel 4.x: dùng toolchain cùng era (clang 8/9; clang 14 → hang ở boot logo), các fix toolchain cụ thể trong notes của họ
  9. Check `.gitignore` kernel tree khi clone (pattern vmlinux* không anchor giết source files)
  10. Mali kbase PID namespace bug = Mali-only (a70q là Adreno — không dính, nhưng failure chain Waydroid đáng đọc)

### XÁC NHẬN BUG BLUETOOTH TRÊN KERNEL A70Q (check trực tiếp GitHub)
- `net/bluetooth/Makefile` nhánh **lineage-22.2** (kernel đang chạy trên máy): **RFCOMM/BNEP/HIDP bị comment out** — đúng bệnh Samsung mà note10lite báo
- LineageOS vẫn BT được vì fluoride (Android stack) làm RFCOMM userspace; **bluez trên UT cần kernel RFCOMM** → không sửa = HFP (audio cuộc gọi), PAN tethering, HID bàn phím chết trên UT
- TODO Phase 2: uncomment 3 dòng + verify `hci_sock.c` nguyên vẹn (đang check)
- **KẾT QUẢ: `hci_sock.c` NGUYÊN VẸN** (45KB, đủ hci_sock_bind/ioctl/create) — fix BT chỉ cần uncomment 3 dòng Makefile, 5 phút
- Kernel branch options: rtd1250/android_kernel_samsung_sm6150 có **lineage-18.1** (era A11, khớp halium-11); a70q-lineage chỉ có lineage-20/21/22.2/upstream — quyết định branch khi build (18.1 era đúng, 22.2 mới nhất đang chạy thật)

### PHÁT HIỆN LỚN: repo a70-root (h3nr1-d14z/a70-root, private) — nhật ký root thực chiến của user 2026-10-04
- Model: **SM-A705F/DS, vùng XXV (Việt Nam)**, serial [serial removed]; firmware stock cuối **A705FXXU5DXD2** (kernel 4.14.190-24363203)
- **Bootloader unlock + Knox 0x1 xác nhận** — hết câu hỏi mở về Knox từ RESEARCH.md
- **Rollback 100% đã có**: stock_boot.img + fac.zip đầy đủ 5 tar Odin (BL/AP/CP/CSC/HOME_CSC) tại /Volumes/External/Projects/a70-kernelsu/ + a70-backup/
- **EFS backup XONG**: modemst1/2, fsg, fsc, persist → Phase 0 gần như hoàn tất từ trước
- **Heimdall Apple Silicon v2.2.2** (fork aljosasavic) đã cài, 6 phiên flash thành công → toolchain flash Mac đã có
- **Kỹ năng repack boot.img v1 đã có**: page 4096, header_size=1648@1644, os_version 0x1600016C, footer SEANDROIDENFORCE, pad 64MB, magiskboot on-device (scripts/repack.sh) — dùng trực tiếp cho đóng gói halium-boot
- Bài học đã học (nhập vào plan): su -c dd qua adb pty hỏng binary → base64 pipe; a70q recovery partition riêng; CRC FUS thập phân; adb USB flap → wireless adb; verify ROM bằng digest GitHub API (trường id stale)
- KSU lessons: DerGoogler KSU kernel rel85 không chứa driver, anykernel.sh supported.versions sai era — bỏ route KSU, không ảnh hưởng halium
- Ý nghĩa cho PLAN: Phase 0 chỉ còn lshal + NFC variant + (optional) pull dtbo/vbmeta; Phase 5 flash đã có tool + rollback + kinh nghiệm

### Phase 0 EXECUTION (2026-11-10, qua wireless adb — chỉ đọc)
- **NFC variant XÁC ĐỊNH: CÓ NFC, chip NXP NQx** (`ro.hardware.nfc_nci = nqx.default`, features android.hardware.nfc+hce+hcef, vendor có libnfc-nci/nxp.conf) — bảng Nura wiki "A705F = N/A NFC" KHÔNG khớp biến thể XXV; port UT NFC khả thi
- **lshal dump FULL: 209 dòng** → backups/hals-lshal.txt. HAL chính thấy: audio@6.0, bluetooth@1.0, gnss 1.0/1.1/2.0/2.1, composer 2.1–2.4, allocator 3.0/4.0, keymaster 4.0, gatekeeper, widevine drm 1.0–1.3, cameraservice 2.0–2.2 — đủ bộ HIDL Android 11 cho halium-11 bridge
- **dtbo.img (10 MiB) + vbmeta.img (64 KiB) đã pull** về backups/ — dtbo xác nhận đúng 10 MiB (con số 8 MiB trên Nura wiki là partition_size ảnh của họ, không phải partition thật)
- **Backup FULL đang chạy**: script backups/full-backup.sh — 50 partition nhỏ/vừa (613 MB), skip system/vendor/product/userdata/cache/hidden/omr/pad; mỗi file verify size khớp blockdev + sha256; manifest backups/MANIFEST.md; lshal full kèm theo
- Ghi chú kỹ thuật: macOS bash 3.2 không có declare -A (script dùng sizes file); Bun shell của eval không hỗ trợ 2>&1 (dùng bash script file thay JS)
- **KẾT QUẢ BACKUP FULL: 50/50 partition OK, khớp size 100%, 108 giây** → backups/full/*.img + backups/MANIFEST.md (size + sha256 từng file)
- Cross-verify: `recovery` = 82,825,216 bytes = đúng size recovery.img LineageOS trong repo a70-root (chứng minh recovery partition đang là LineageOS recovery); `boot` = 67,108,864 (64 MiB) khớp convention repack đã dùng
- Phase 0 HOÀN THÀNH 100% — mọi thứ cần để port + rollback an toàn đã nằm trên máy tính (backups/ + /Volumes/External/Projects/a70-backup/ + fac.zip)

## 2026-11-10 — Handoff: push GitHub + máy build = Mac mini M4
- Repo handoff: **h3nr1-d14z/ubuntu-touch-a70** (private, main) — README.md handoff đầy đủ, 3 docs nghiên cứu, backup script + manifest + lshal dump + dtbo/vbmeta
- Ảnh partition 612MB KHÔNG vào git (GitHub chặn >100MB + tái tạo được bằng `backups/full-backup.sh` ~110 giây từ thiết bị)
- Commit dùng email noreply GitHub (GH007 email privacy) — git config trong repo đã set sẵn noreply
- **Quyết định build machine: Mac mini M4** (Apple Silicon, nhiều ổ) — build trong container Linux arm64 (host arch = target arch → kernel native, không qemu); Mac này giữ vai trò flash/debug host (heimdall-apple-silicon)
- **Quy tắc vàng ghi trong README + build kit**: source tree nằm TRONG named volume ext4 case-sensitive, KHÔNG bind mount macOS vào /work — APFS case-insensitive phá Android build system
- Build kit mới: `build/Dockerfile` (ubuntu 20.04 + deps Halium + repo tool + mkbootimg), `build/run.sh` (volumes halium-src/halium-ccache, memory 12g default, cp ra/vào), `build/manifests/samsung_a70q.xml` (kernel rtd1250/android_kernel_samsung_sm6150 lineage-18.1; device/vendor a70q-lineage lineage-20 làm điểm bắt đầu — template UNTESTED, chỉnh khi setup)
- Vendor blobs: trees proprietary_vendor_samsung_a70q + _sm6150-common của a70q-lineage có sẵn blobs trong git — không bắt buộc extract-files từ device (chỉ làm khi muốn khớp DXD2)

## 2026-11-10 — Phase 1 EXECUTION trên Mac mini M4
- Backup tái tạo trên máy mới: 50/50 partition OK (123s) qua wireless adb `100.82.206.67:40735`; sha256 khớp manifest cũ cho mọi partition ổn định, chỉ `persist` đổi hash (`cf4fe526`→`ec7a868b`) — dữ liệu runtime, bình thường. `full-backup.sh` giờ lấy thiết bị từ env `ADB=...`
- Máy xác nhận: **Mac mini M4** (Mac16,10, 16 GB RAM, 138 GB free)
- Runtime container: **OrbStack** (`brew install --cask orbstack`), docker server 29.8.2, CLI ở `~/.orbstack/bin` (thêm vào PATH). VM cấp **8 GB** mặc định — OrbStack cấp động, phải kiểm lại khi tới `mka` (PLAN muốn 12g)
- **Fix Dockerfile #1 (arm64)**: `gcc-multilib/g++-multilib/lib32ncurses-dev/lib32z1-dev` chỉ tồn tại trên x86_64 → thay `gcc g++ libncurses-dev`
- **Fix Dockerfile #2**: `pip install mkbootimg` KHÔNG tồn tại trên PyPI → clone AOSP `system/tools/mkbootimg` + `external/avb`, symlink `/usr/local/bin/{mkbootimg,avbtool}`
- Image `halium-a70q:latest` build OK. `repo init -b halium-11.0 --depth=1` + copy `build/manifests/samsung_a70q.xml` + `repo sync` đang chạy nền (25–40 GB)
- **Xác nhận sớm bug BT trên kernel `lineage-18.1`** (raw GitHub, chưa đợi repo sync xong): `net/bluetooth/Makefile` bị comment `rfcomm/`, `bnep/`, `hidp/` — giống hệt lineage-22.2; `hci_sock.c` 18.1 nguyên vẹn (hci_sock_bind/ioctl/create + proto_ops đầy đủ). Fix Phase 2 = uncomment 3 dòng, không cần sửa gì khác
- **Lỗi sync #1**: XML comment không được chứa `--` (dòng hướng dẫn `repo sync --current-branch...` trong khối comment của local manifest) → parser chết line 8. Sửa bằng cách bỏ dấu `--` trong comment
- **Lỗi sync #2 + sửa PLAN**: bảng PLAN ghi vendor `lineage-18.1` = LỖI THỜI; GitHub API 2026-11-10: 2 repo vendor a70q-lineage **chỉ còn `lineage-21` + `lineage-22.2`** (device còn lineage-20/21/22.2). Manifest lineage-20 → fetch fail `couldn't find remote ref`. Chốt: **device + cả 2 vendor = `lineage-22.2`** (thống nhất era, khớp ROM đang chạy); kernel giữ rtd1250 `lineage-18.1`. Cần cập nhật bảng PLAN mục Source trees
- chromium-webview prebuilt (arm/arm64/x86_64) checkout fail lần 1 — disk còn 54 GB (không full), nghi lỗi tạm thời; sync lại sẽ heal, nếu lặp thì sync riêng `-j1`
- **Root cause thật của chromium-webview (lần 3)**: KHÔNG phải lỗi tạm thời — các repo này dùng **Git LFS** (`webview.apk` là object LFS); image thiếu `git-lfs` → `git read-tree --reset -u` gọi smudge filter → `git-lfs: not found` → repo bọc thành "Cannot initialize work tree" (stderr git thật bị nuốt — phải chạy tay `GIT_WORK_TREE=... git --git-dir=... read-tree --reset -u -v HEAD` mới thấy). **Fix: thêm `git-lfs` vào Dockerfile + `git lfs install`**, rebuild image (git-lfs 2.9.2 arm64). Bài học: Halium docs liệt kê git-lfs trong deps — bộ Dockerfile cũ thiếu so với docs
- Sync lần 4 (2026-11-10 ~16:2x) đang chạy với image mới; fetch đã xong từ lần trước nên chỉ còn checkout + tải LFS APK (~100 MB × 3) + fetch vendor lineage-22.2
- **REPO SYNC HOÀN TẤT (exit 0)** — tree Halium 11 đầy đủ (~80 GB trong volume `halium-src`); kernel `lineage-18.1` commit `0b4c66a2b`; device/vendor a70q `lineage-22.2` đều checkout OK
- **Phase 2 khởi động**: (1) Fix BT XONG trong tree — Makefile uncomment 3 dòng + defconfig bật RFCOMM(+TTY)/BNEP(+MC+PROTO_FILTER)/HIDP (a70q_defconfig cũng đang `not set` — uncomment Makefile một mình không đủ); (2) `.gitignore` kernel 18.1 đã anchor `/vmlinux` — an toàn; (3) defconfig 18.1 halium-ready sẵn: ASHMEM=y, BINDER_IPC=y (3 devices), không có ANDROID_PARANOID_NETWORK
- **`hybris-patches/apply-patches.sh` chạy OK (exit 0)** từ gốc tree — điều kiện trước `mka halium-boot` theo docs Halium
- **Rủi ro toolchain xác nhận**: `prebuilts/clang/host/` chỉ `linux-x86`/`darwin-x86` — toolchain AOSP x86 binary không chạy native arm64. Phương án ghi trong PLAN Phase 2; kernel 4.14 build được bằng gcc-9/aarch64 của focal nếu chọn route (1). Quyết định sau lần build đầu

## 2026-11-11 — Build đầu tiên + adapt device tree (Phase 4 tới sớm)
- Build thử #1 (container arm64): fail đúng như dự đoán — OrbStack báo x86 loader thiếu, và `breakfast` thiếu product (`lineage_a70q`) vì local manifest THIẾU `device/samsung/sm6150-common` (template UNTESTED sót). Bài học quy trình: pipe vào `tail` nuốt exit code → lần sau dùng `set -o pipefail`
- **CHỐT ROUTE BUILD: container `linux/amd64` qua Rosetta của OrbStack** — image `halium-a70q:amd64` (same Dockerfile, `docker build --platform linux/amd64`); mọi toolchain x86 của AOSP chạy; `breakfast` xác nhận HOST_ARCH=x86_64 OK. Route native-arm64 bỏ (chỉ còn giá trị nếu sau này đứng riêng kernel)
- Sync thêm `device/samsung/sm6150-common` (lineage-22.2, commit `6fd8cb6`) vào local manifest — manifest giờ 5 project
- **Adapt device tree cho parse trong halium-11 (3 patch, giữ git upstream để export patch file sau)**:
  1. `sm6150-common/common.mk`: comment inherit `hardware/qcom-caf/common/common.mk`
  2. `sm6150-common/BoardConfigCommon.mk`: bỏ `hardware/qcom-caf/common/vendor_framework_compatibility_matrix.xml` + `hardware/samsung/vintf/samsung_framework_compatibility_matrix.xml` khỏi DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE (cả hai path không tồn tại trong tree 18.1), comment `DEVICE_MATRIX_FILE := hardware/qcom-caf/common/compatibility_matrix.xml`
  3. `device/qcom/sepolicy_vndr` đã có sẵn trong halium tree — không cần sửa
- Kết quả: **`breakfast a70q` PASS** trong amd64 container: TARGET_PRODUCT=lineage_a70q, Android 11, kernel vars chuẩn (`TARGET_KERNEL_SOURCE=kernel/samsung/sm6150`, `TARGET_KERNEL_CONFIG=a70q_defconfig`, BOOTIMAGE_PARTITION_SIZE=64MiB, SEPARATED_DTBO=true, cmdline/base/pagesize/offsets đầy đủ)
- `PRODUCT_SOONG_NAMESPACES` còn tham chiếu `hardware/qcom-caf/sm8150` — chỉ lỗi khi build module soong đó, `mka halium-boot` không đụng; để nguyên
- **`mka halium-boot` đang chạy nền** (amd64/Rosetta, 12 GB) — kernel 4.14 build lần đầu dự kiến 30–90 phút

- **BUILD THÀNH CÔNG 2026-11-11**: `mka halium-boot` exit 0 → `out/target/product/a70q/halium-boot.img` (20,168,704 bytes, magic `ANDROID!`, header v1, cmdline từ BoardConfigCommon, os_patch 2024-02-05, board SRPRL06C005) — copy ra host `out/halium-boot.img`, sha256 `b160a7b7017428c8e653de6459693d155137564b88bc178ed6c88aad0d9fc408`
- Kernel: `4.14.261-lineage-g0b4c66a2b87b-dirty`, 8 module, **fix BT đã nằm trong vmlinux** (CONFIG_BT_RFCOMM/_TTY/BNEP/MC_FILTER/PROTO_FILTER/HIDP =y; 317 rfcomm symbols) — bluez trên UT có kernel RFCOMM để dùng
- Tổng thời gian build sạch: kati ~4 phút + ninja đóng gói ~3.5 phút (kernel cached 18 phút lần đầu) — Rosetta amd64
- Còn 2 warning vô hại khi build kernel: `secgetspf: No such file or directory` + `cp: bad .../perflog.h` (glue Samsung kỳ vọng file từ system/core — chỉ ảnh hưởng logging perf, không phá build); depmod WARNING modules.order/builtin trong PACKAGING intermediates (không ảnh hưởng boot)
- **TIẾP THEO = Phase 5 flash test**: heimdall sẵn (`~/bin/heimdall`), rollback sẵn (`backups/full/{boot,vbmeta,dtbo,recovery}.img`). Cần người đưa máy về Download Mode → flash `halium-boot.img` vào `boot` (+ `vbmeta` disable-verification hoặc kèm vbmeta gốc); rollback tức thời bằng `backups/full/boot.img`

## 2026-11-11 (tiếp) — Vòng lặp fix parse soong: vendor bp lineage-22.2 vs soong A11
Soong parse TOÀN BỘ Android.bp trong tree (không liên quan PRODUCT_SOONG_NAMESPACES). Vendor trees 22.2 (A15) gãy hàng loạt trên soong A11; các fix đã thử theo thứ tự:
1. Bỏ namespace trong `PRODUCT_SOONG_NAMESPACES` (vendor .mk) — KHÔNG giải quyết (bp list do soong finder tự scan, không phải findleaves)
2. Codemod 17 block `prebuilt_rfsa` → `filegroup` — hết lỗi này nhưng lộ tiếp lỗi khác
3. `soong_namespace { imports: [...] }` trỏ dir không tồn tại (`hardware/qcom-caf/*`) → emptied — lộ tiếp class lỗi deps chéo namespace + deps tới module chỉ có ở lineage-22.2 (`libprotobuf-*-vendorcompat`...) — đây là đáy: không thể parse vendor bp A15 trên soong A11 bằng patch rời rạc
4. **CHỐT: `.find-ignore`** — soong finder (`build/soong/ui/build/finder.go`) tôn trọng file `.find-ignore` đặt trong thư mục = prune subtree khỏi scan. Đặt `vendor/samsung/.find-ignore` → vendor bp KHÔNG bị parse. Restore toàn bộ vendor trees về upstream (`git checkout -- .`) vì không còn bị parse. `halium-boot` không build vendor module nào; blobs dùng thẳng file cho rootfs ở Phase 4
- Patch tạm (đã siêu) trên `build/make/core/config.mk`: thêm `vendor/samsung` vào `FIND_LEAVES_EXCLUDES` — giữ nguyên, vô hại và nhất quán với .find-ignore
- Lỗi đã đi qua trước đó: `prebuilt_nfc_nci_nxp` (cc_prebuilt đè source module, chỉ có arm64 src) — hết khi vendor tree không bị scan
- Ghi chú RAM: container chỉ thấy 7.82 GB (OrbStack VM 8 GB mặc định; `--memory=12g` không vượt trần VM được). Warning của build system, chưa gãy; nếu OOM khi compile kernel → nâng VM memory OrbStack

## 2026-11-11 đêm — Trạng thái qua đêm + quy trình buổi sáng (cho người đọc tiếp)

### Tình trạng máy (khi ngủ)
- Máy đang **bootloop** với **testA** (kernel LOS 22.2 + initramfs NCM của ta + cmdline ta + dtbo LOS gốc): LOS kernel thiếu `f_ncm` → gadget fail → rescue `reboot -f` → loop. Máy cắm điện loop đến hết pin rồi tự tắt — vô hại phần cứng, userdata không hề bị flash

### Kết luận đêm (bằng chứng đầy đủ)
- misc RỖNG sau testA bootloop (m5/m6: 10 bytes non-zero = chỉ "roms" cũ) → **failure-path dump (dmesg >> sda10 — chạy sau mount /dev nếu /init đi đến panic path) KHÔNG xuất hiện → /init không đến panic path → /init chết giữa chừng HOẶC kernel chết trước /init**
- Stage marker đặt ở /init top viết TRƯỚC mount /dev → fail silent → misc rỗng không khử được 2 khả năng trên (lỗi instrumentation — vào ERRORS.md học)
- misc KHÔNG bị ghi đè từ testA đến giờ (LOS boot không ghi misc) → **dmesg testA vẫn nằm nguyên trong misc** → đọc nó là thấy mọi thứ: initramfs unpack ok/failed, Run /init có xảy ra, panic gì
- Kernel LOS 22.2 = kernel đã chứng minh boot trên máy — nghi vấn chính chuyển sang **ramdisk/initramfs của ta** (unpack fail trên kernel nào cũng vậy? format/size?) — testA chính là test đó (LOS kernel + ramdisk ta) và misc rỗng → ngả về ramdisk/unpack hoặc cmdline (đã sạch, ít nghi)

### Ảnh chờ flash buổi sáng (hash `8991b011`)
`out/halium-boot.img` = kernel **clang r383902b** (chưa từng được test cùng initramfs đã vá — các attempt cũ bootloop do C14 stale-initramfs và C17 cmdline-$0-corruption, KHÔNG chứng minh kernel hỏng) + **initramfs NCM** + **dump kmsg→misc(sda10)** + **stage marker đầu /init** + cmdline SẠCH 497 chars (panic=0 ✓, KHÔNG memmap/ramoops — tránh C17 + tránh chiếm vùng nhớ chưa reserve) + anti-rollback header 15.0.0/2026-06 + dtbo rỗng (base DTB)
Kernel dự phòng đã build sẵn trong volume `final-halium-boot.img` (gcc 9.4 + DTB LOS + cùng initramfs)

### Quy trình buổi sáng
1. Sạc pin nếu máy tắt → force tắt máy (giữ nguồn) → **giữ 2 phím Volume + cắm cáp** → Download Mode
2. Báo "dm rồi" → agent flash `out/halium-boot.img` + `out/vbmeta-disabled.img` + `out/dtbo-empty.img`
3. Kết quả 3 nhánh:
   - **Không loop + macOS thấy USB ethernet mới + telnet 192.168.2.15:23 được** → RESCUE SUCCESS: agent tự telnet đọc dmesg/pstore rồi báo
   - **Loop nhanh (~30–60s/vòng) mà misc có marker+dmesg** (đọc qua bước 4) → kernel+initramfs chạy, USB gadget fail → vá USB kernel config rồi lặp
   - **misc rỗng hoàn toàn** → kernel chết trước `/init` → điều tra DTB 18.1/toolchain (fallback kernel lai gcc+DTB-LOS)
4. Đọc misc: flash `out/losboot-ramoops.img` + vbmeta-disabled + `backups/full/dtbo.img` → boot LOS → adb → `su -c dd if=/dev/block/by-name/misc` → pull → strings
5. Rollback LOS bất cứ lúc nào: flash `backups/full/{boot,vbmeta,dtbo}.img` (đã làm 2 lần thành công trong phiên)

### Bài học bổ sung (đêm)
- **C17**: cmdline chứa `$0` bị expand HAI LỚP (kati → shell) → `/bin/bash` nhét vào giữa tham số; tránh `$` trong cmdline hoàn toàn
- **C16**: cmdline giới hạn 512 byte, mkbootimg truncation âm thầm — luôn verify bằng unpack_bootimg sau khi sửa
- Kernel lai (ghép gz/dtb thủ công) là công cụ bisect mạnh: tách biến kernel-blob vs ramdisk vs dtbo mà chỉ tốn 1 lần flash
- **fbcon test ÂM TÍNH (kernel clang)**: bật `DRM_FBDEV_EMULATION` + `FRAMEBUFFER_CONSOLE` + `console=tty0` → flash → KHÔNG có gì trên màn hình + bootloop tiếp → kernel clang chết TRƯỚC fbcon (~1-2s sau start) → củng cố nghi toolchain clang-13 cho kernel 4.14.261 SM6150 → **flash test kernel gcc-9.4 (đã build thành công 1h16m, 0 forbidden warning, chưa từng được flash)**
- testB (LOS kernel + initramfs ta): misc rỗng → LOS kernel cũng không chạy /init của ta → nghi ramdisk blob; NHƯNG kernel lai test (LOS kernel + ramdisk ta... chưa đọc misc sau test đó)

### Trạng thái build cuối (verify trong image)
`MKA_EXIT=0 | cmdline 497 chars SẠCH (panic=0 ✓, không memmap/ramoops) | ncm ✓ | sda10 ✓ | init-stage ✓` — đã push GitHub kèm `patches/` đầy đủ (commit `5721e91`, `b6db845`, + commit cuối)

### Sau khi flash gcc-kernel (đêm — DM session user để sẵn)
1. Máy boot kernel gcc-9.4 + fbcon + ramoops-DTB-reserved + panic=0
2. Kịch bản quan sát:
   - **fbcon hiện kernel log trên AMOLED** → nếu panic: màn hình ĐỨNG YÊN (panic=0) → user CHỤP ẢNH gửi → đọc nguyên nhân trực tiếp
   - **USB NCM ethernet xuất hiện + telnet 192.168.2.15:23** → rescue sống → agent telnet đọc dmesg/pstore trực tiếp
   - **Loop nhanh** → gcc kernel cũng chết siêu sớm (trước fbcon+pstore) → toolchain-independent → điều tra DTB/ABL (flash test gcc-kernel + DTB-LOS-append)
3. Đọc ramoops (nếu kernel sống ≥ pstore-attach ~0.16s): giữ 2 phím Volume tại vòng restart → warm DM → flash LOS reader (warm) → LOS attach cùng vùng → `/sys/fs/pstore/console-ramoops-0` = console log của kernel gcc
4. Rollback LOS: flash `backups/full/{boot,vbmeta,dtbo}.img` bất cứ lúc nào

### ⭐ ROOT CAUSE CHỐT (đêm 11/11): DEVTMPFS
- Mọi bootloop/initramfs-fail đêm nay giải thích bằng MỘT nguyên nhân: `a70q_defconfig` Samsung có `# CONFIG_DEVTMPFS is not set` — halium initramfs cần devtmpfs (/init mount nó; không có → /dev rỗng → không console/block-device/gadget qua... configfs vẫn /sys nhưng /init chết giữa) → panic → reboot-loop; misc empty vì sda10 dump viết vào /dev/block/sda10 không tồn tại (trước lẫn sau mount)
- Fix: CONFIG_DEVTMPFS=y + CONFIG_DEVTMPFS_MOUNT=y trong a70q_defconfig → clean rebuild (đang chạy, kbuild-devtmpfs)
- LOS reader boot OK với ramdisk LOS vì ramdisk LOS tự chứa /dev nodes (mknod sẵn trong cpio) — KHÔNG phụ thuộc devtmpfs
- Flash sau khi build: halium-boot (gcc kernel + initramfs NCM + devtmpfs) → nếu devtmpfs là root cause duy nhất: /init chạy → marker post-mount vào misc → NCM gadget (kernel clang/gcc đều có f_ncm builtin... gcc kernel ✓) → telnet 192.168.2.15 rescue → ĐỌC DMESG LIVE

### Bằng chứng bổ sung (khuya)
- Flash kernel **gcc-9.4 + fbcon** qua DM sạch (sau Mac restart, adb server chết, không watcher): bootloop + **KHÔNG có gì trên màn hình** → kernel gcc cũng chết trước fbcon (~1-2s) → **cả clang-13 lẫn gcc-9.4 đều chết siêu sớm** → toolchain-independent → nghi vấn chính: kernel tree 4.14.261 (lineage-18.1 + SM6150 vendor) có early-boot issue với DTB/tree-state hiện tại, hoặc cần era-correct toolchain (clang r3441b/9.0 — era LOS 18.1 mà rtd1250 dùng)
- LOS reader (LOS kernel gốc) + cmdline ramoops của ta: BOOT OK + ramoops attach 0.16s ✓ → cmdline vô hại với kernel proven
- C20 DEVTMPFS fix = điều kiện cần (initramfs chạy) nhưng CHƯA chắc đủ — kernel chết trước cả khi initramfs kịp chạy ở các attempt mới nhất (pstore rỗng sau warm read, fbcon silent)

### ⭐ Bài học LỚN NHẤT của phiên (cho mọi phiên sau): CHUỖI FLASH/DM KHÔNG ỔN ĐỊNH KHI PIN YẾU + ADB SERVER SỐNG + POLL DETECT
1. **heimdall detect poll = tự đá máy khỏi DM**: mỗi detect mở+đóng USB interface → sboot xử lý close = máy THOÁT DM + reboot → bootloop → user vào DM lại → poll kill tiếp → vòng lặp vô tận ("dm rồi rồi mất")
2. **adb fork-server ngâm**: giữ/quét USB, mở+đóng Samsung DM device khi có lệnh adb → heimdall bị đá khỏi IOService giữa transfer
3. **Pin cạn sau nhiều giờ loop/flash**: DM transfer cần dòng lớn → sụt áp → rớt ngay handshake/bulk ("pipe is stalled" / "no connection to an IOService")
4. **Quy tắc vàng cho lần flash tới**: (a) kill adb server TRƯỚC; (b) KHÔNG detect/poll gì cả; (c) pin ≥50% hoặc sạc wall trước; (d) user vào DM → chạy 1 lệnh flash duy nhất trong ~10 giây

## 2026-11-13 tối — Chuyển hướng pmOS: kernel tự build BOOT ĐƯỢC (k0, k1)
- Bỏ cây 4.14.261 (lineage-18.1); dùng a70q-lineage `lineage-22.2` @ 699d9d7da = source của blob LOS (config + DTB khớp 100%)
- k0 (tái tạo) và k1 (+DEVTMPFS) đều boot LOS; k1 xác nhận qua adb: `a70q@pmos`, Ubuntu clang 19.1.1, devtmpfs có
- Mất ~1h vì USB adb "không lên" → hoá ra node ma Download Mode kẹt ở hub cổng front Mac mini (C23), không phải kernel; heimdall stall lần đầu (C24) → `pmos/tools/flash.sh`
- Chi tiết + đính chính ghi chú cũ: ERRORS.md mục F; quy trình: pmos/README.md

## 2026-11-14 rạng sáng — pmOS initramfs CHẠY trên A70 + USB networking OK
- pmbootstrap 3.12.1 trong container arm64 (`pmos/pmb/`), pmaports `device-samsung-a70q` + `linux-samsung-a70q` (downstream, build từ source 699d9d7da, Alpine clang 23)
- Initramfs pmOS + `pmos.debug-shell` boot OK: shell qua USB ACM serial; kernel `4.14.190-perf #3-postmarketOS`
- Sửa 4 lỗi build (C26–C28) + BUG kernel u_ether RX (C29) → USB ECM: DHCP 172.16.42.2, ping, telnet, 8MB sha256 khớp
- Không có microSD (mmc0: card not present); UFS sda 128GB — chưa đụng userdata/rootfs

## 2026-11-14 sáng — ⭐ pmOS BOOT VÀO ROOTFS + SSH OK
- Userdata xoá (theo lựa chọn user), rootfs OpenRC sparse-flash vào USERDATA; `/` = loop0p2 ext4 (4K), resize 108GB
- Chuỗi lỗi: systemd≥5.4 (C31) → loop node Docker (C32) → heimdall cần sparse (C33) → thiếu loop.max_part (C34) → 2 bug kernel loop/4K (C35, patches 0002–0004)
- `ssh user@172.16.42.1` — Nura (pmOS) edge, kernel 4.14.190-perf #9-postmarketOS, RAM 5.5GB

## 2026-11-14 trưa — GPU Adreno 612 chạy (Vulkan + GL qua Zink)
- turnip build với backend kgsl (package `mesa-vulkan-freedreno-kgsl`), firmware GPU từ vendor, udev rule, env trong profile.d
- Sau reboot sạch: `zink Vulkan 1.3(Turnip Adreno (TM) 612)`, glmark2 73 (llvmpipe 34); chi tiết C47

## 2026-11-14 chiều — Âm thanh chạy (loa, tai nghe, mic) + DTBO bỏ Virtual-1
- ADSP boot từ service; UCM cho PipeWire; kernel r15 (patch 0008 jack kcontrol); device r11
- DTBO lần 1 hỏng màn hình (C49), lần 2 đúng; mất thời gian vì tự kill nhầm tiến trình (C50)

