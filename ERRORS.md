# ERRORS.md — Sổ lỗi & bài học (cập nhật khi gặp lỗi mới)

Mục đích: không mắc lại lỗi nào hai lần, và ai tiếp nhận repo sau này không phảiDebug lại từ đầu. Format: **Triệu chứng → Root cause → Fix → Bài học**.

Ngày lập: 2026-11-11. Nguồn: Phase 0–3 thực chiến trên Mac mini M4 (OrbStack, container amd64/Rosetta).

---

## A. Build environment (Dockerfile / container)

### A1. `gcc-multilib` không cài được trên arm64
- **Triệu chứng**: `apt` fail khi build image: `Package 'gcc-multilib' has no installation candidate`, `Unable to locate package lib32ncurses-dev`
- **Root cause**: các package multilib 32-bit chỉ tồn tại cho host x86_64. Dockerfile gốc chép deps cho Linux x86_64
- **Fix**: thay bằng `gcc g++ libncurses-dev` (arm64). Đã commit vào `build/Dockerfile`
- **Bài học**: mọi deps docs Halium/Android viết cho x86_64 — soi từng package khi port sang arm64

### A2. `pip install mkbootimg` — package không tồn tại trên PyPI
- **Triệu chứng**: `ERROR: No matching distribution found for mkbootimg`
- **Root cause**: mkbootimg/avbtool là code AOSP, không publish lên PyPI
- **Fix**: clone `android.googlesource.com/platform/system/tools/mkbootimg` + `platform/external/avb`, symlink vào `/usr/local/bin`. Đã commit vào Dockerfile

### A3. Thiếu `git-lfs` → sync chết ở chromium-webview
- **Triệu chứng**: `repo sync` fail: `Cannot initialize work tree for LineageOS/android_external_chromium-webview_prebuilt_arm` (×3 arch), lặp lại mọi lần
- **Root cause**: các repo webview prebuilt dùng **Git LFS** (`webview.apk` là LFS object). `git read-tree --reset -u` gọi smudge filter → `git-lfs: not found` → repo bọc lỗi thành "Cannot initialize work tree" và **nuốt stderr git thật**
- **Fix**: thêm `git-lfs` vào Dockerfile + `git lfs install`; rebuild image
- **Bài học**:
  - Halium docs liệt kê git-lfs trong deps — đừng bỏ qua deps "vizzie"
  - Khi repo nuốt lỗi: chạy tay lệnh git gốc với `GIT_WORK_TREE=... git --git-dir=... read-tree --reset -u -v HEAD` để thấy stderr thật

### A4. Container arm64 không chạy được toolchain AOSP
- **Triệu chứng**: `OrbStack ERROR: Dynamic loader not found: /lib64/ld-linux-x86-64.so.2` ngay trong breakfast/mka
- **Root cause**: `prebuilts/clang/host/` chỉ có `linux-x86` + `darwin-x86` — TOÀN BỘ host toolchain AOSP là binary x86
- **Fix/Quyết định**: **build trong container `linux/amd64` qua Rosetta của OrbStack** (`docker build/run --platform linux/amd64`). `breakfast` PASS với HOST_ARCH=x86_64
- **Bài học**: kế hoạch "host arm64 = native arm64 build" chỉ đứng vững cho kernel standalone; build system đầy đủ phải amd64. Hyết năng Rosetta ~1.5–2x chậm native

### A5. RAM container: `--memory=12g` không vượt trần VM OrbStack
- **Triệu chứng**: build warning `You are building on a machine with 7.82GB of RAM (minimum ~16GB)`
- **Root cause**: OrbStack VM mặc định 8 GB; limit container vượt VM thì bị chặn ở VM
- **Trạng thái**: chưa gãy; nếu OOM khi compile kernel → nâng VM memory trong OrbStack (Settings/`orb config`) rồi giữ `HALIUM_MEM=12g`
- (Ghi chú phụ: `Build sandboxing disabled due to nsjail error` là bình thường trong container — không user namespace)

---

## B. Manifest / repo sync

### B1. XML comment chứa `--` → manifest không parse được
- **Triệu chứng**: `error parsing manifest ...: not well-formed (invalid token): line 8, column 16` ngay khi `repo sync`
- **Root cause**: comment XML **không được chứa chuỗi `--`** — dòng hướng dẫn `repo sync --current-branch ...` nằm trong khối comment của local manifest
- **Fix**: viết lại comment không dùng `--`
- **Bài học**: validate manifest bằng `python3 -c "import xml.dom.minidom; xml.dom.minidom.parse(...)"` trước khi sync

### B2. Branch trong manifest không tồn tại
- **Triệu chứng**: `fatal: couldn't find remote ref refs/heads/lineage-20` cho 2 repo vendor
- **Root cause**: bảng PLAN cũ ghi vendor `lineage-18.1` (đã bị xoá trên GitHub), template ghi `lineage-20`. Thực tế (GitHub API 2026-11-10): 2 repo vendor a70q-lineage **chỉ còn `lineage-21` + `lineage-22.2`**
- **Fix**: device + cả 2 vendor a70q = `lineage-22.2` (thống nhất era, khớp ROM đang chạy); kernel giữ rtd1250 `lineage-18.1`
- **Bài học**: **check branch thật bằng GitHub API** (`api.github.com/repos/<org>/<repo>/branches`) trước khi ghi vào manifest — tài liệu cũ chết theo thời gian

### B3. Local manifest thiếu project mà device tree inherit
- **Triệu chứng**: `breakfast` → `** Don't have a product spec for: 'lineage_a70q' **`
- **Root cause**: `device/samsung/a70q/device.mk` inherit `device/samsung/sm6150-common/common.mk` — common device tree KHÔNG có trong local manifest (template UNTESTED sót)
- **Fix**: thêm `device/samsung/sm6150-common` (lineage-22.2) vào manifest
- **Bài học**: đọc inherit-product/include graph của device tree rồi mới viết manifest; template "UNTESTED" phải coi là sai đến khi chứng minh được

### B4. Thêm repo không tồn tại trên remote
- **Triệu chứng**: `Unable to remote fetch project android_external_python_pyyaml`; GitHub API trả 404 cho `LineageOS/android_external_python_pyyaml`
- **Root cause**: LineageOS không mirror repo AOSP này; Halium manifest cũng không kéo `external/python/pyyaml`
- **Fix**: bỏ project khỏi manifest; prune `vendor/apn` (người tiêu dùng duy nhất của pyyaml) bằng `.find-ignore` (xem C2)
- **Bài học**: kiểm tra repo tồn tại (`api.github.com/repos/...`) trước khi thêm vào manifest; chỉ thêm repo khi module thật sự cần cho target đang build

---

## C. Soong / device tree adaptation (lineage-22.2 tree trên build tree A11)

Bối cảnh: halium-11 = build tree A11/lineage-18.1; device+vendor trees a70q chỉ còn lineage-22.2 (A15-era). Soong parse **mọi** `Android.bp` trong tree (không liên quan `PRODUCT_SOONG_NAMESPACES` — đó chỉ là khai báo cross-namespace, không phải cơ chế discovery).

### C1. Hàng loạt `unrecognized module type "prebuilt_rfsa"`
- **Triệu chứng**: `soong bootstrap failed`: 17× `unrecognized module type "prebuilt_rfsa"` trong `vendor/samsung/sm6150-common/Android.bp`
- **Root cause**: module type mới của soong A15, không tồn tại ở soong A11
- **Fix (thử, rồi superseded)**: codemod 17 block → `filegroup`. Cuối cùng KHÔNG cần vì vendor tree bị prune toàn bộ (C3)

### C2. `soong_namespace` imports dir không tồn tại
- **Triệu chứng**: `module "soong_namespace": namespace hardware/qcom-caf/wlan does not exist`
- **Root cause**: vendor bp import namespace CAF chỉ có ở build tree lineage-22.2
- **Fix thử**: empty imports → lộ ra class lỗi kế (C3)

### C3. Đá: deps chéo namespace + deps tới module chỉ có ở lineage-22.2
- **Triệu chứng**: hàng chục `"X" depends on undefined module "Y"` (`libprotobuf-*-vendorcompat`, `libcrypto-v33`, `libcdsprpc`, `libqdMetaData`, `libcld80211`...)
- **Root cause**: không thể parse vendor/device bp A15 trên soong A11 bằng patch rời rạc — mỗi fix lộ ra lớp lỗi kế (deps tới build-tree modules lineage-22.2-only)
- **Fix CUỐI (đúng kiến trúc): `.find-ignore`**
  - Soong finder (`build/soong/ui/build/finder.go`) prune subtree khi gặp file `.find-ignore` trong dir đó (pruneFiles: `.out-dir`, `.find-ignore`; file ở ROOT thì fatal)
  - Đặt: `vendor/samsung/.find-ignore`, `vendor/apn/.find-ignore`, `device/samsung/sm6150-common/{audio,fingerprint,livedisplay,power,recovery,rro_overlays/TetheringOverlay,rro_overlays/WifiOverlay,sensors,touch}/.find-ignore`
  - GIỮ parse: `device/samsung/sm6150-common/init/` (`libinit_sm6150` — cần bởi `TARGET_INIT_VENDOR_LIB` trong BoardConfigCommon)
  - Restore toàn bộ vendor trees về upstream (`git checkout -- .`) — không còn bị parse
- **Bài học**:
  - `halium-boot` không build vendor modules — blobs dùng thẳng file cho rootfs (Phase 4). Prune bp khỏi soong là đúng, không phải workaround
  - `.find-ignore` = cơ chế chuẩn để bỏ dir khỏi soong; đừng codemod bp từng block
  - Khi prune device common: giữ subdir chứa soong module mà BoardConfig tham chiếu qua `//dir:module`

### C4. `hardware/qcom-caf/common/common.mk` does not exist (make side)
- **Triệu chứng**: `_nic.PRODUCTS.[[device/samsung/a70q/lineage_a70q.mk]]: "hardware/qcom-caf/common/common.mk" does not exist`
- **Root cause**: device tree lineage-22.2 inherit CAF repos của build tree 22.2 — halium-11 (A11) không có
- **Fix**: patch trong tree (git upstream giữ nguyên để export patch file cho port sau):
  - `sm6150-common/common.mk`: comment inherit `hardware/qcom-caf/common/common.mk`
  - `sm6150-common/BoardConfigCommon.mk`: bỏ `hardware/qcom-caf/common/vendor_framework_compatibility_matrix.xml` + `hardware/samsung/vintf/samsung_framework_compatibility_matrix.xml` (không tồn tại ở tree 18.1) khỏi `DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE`; comment `DEVICE_MATRIX_FILE := hardware/qcom-caf/common/compatibility_matrix.xml`
  - `device/qcom/sepolicy_vndr` CÓ sẵn trong halium tree — giữ nguyên

### C5. device common subdirs A13-era fail từng lớp
- **Triệu chứng lần lượt**:
  - `audio/impl/...`: depends on undefined `android.hardware.audio@7.1`, `libmediautils_vendor`, `libmediautils_headers` (HIDL 7.x chỉ có ở A13+)
  - `recovery/...`: source path `bootable/deprecated-ota/edify/include`, `system/libbase/include` không tồn tại (restructuring A13+)
- **Fix**: `.find-ignore` từng subdir (xem C3)

### C6. Patch make-side phải biết TRẬT TỰ load
- **Phát hiện**: muốn prune make-side scan (vendor/samsung) qua `SCAN_EXCLUDE_DIRS` trong BoardConfig — nhưng `FIND_LEAVES_EXCLUDES` trong `config.mk` được gán **trước** khi BoardConfig được include → giá trị đặt trong BoardConfig không có tác dụng
- **Fix đã làm**: patch thẳng `build/make/core/config.mk` thêm `vendor/samsung` vào dòng `FIND_LEAVES_EXCLUDES` (nhất quán với .find-ignore; vô hại)
- **Bài học**: biến cấu hình make có điểm load khác nhau — check thứ tự include trong config.mk/envsetup.mk trước khi đặt biến trong BoardConfig

### C7. Include path A13-era trong device tree (`system/libbase/include`)
- **Triệu chứng**: `module "libinit_sm6150" variant "android_arm64_armv8-a_static": source path "system/libbase/include" does not exist`
- **Root cause**: lineage-22.2 tách `system/core/base` thành repo riêng `system/libbase`; tree A11 vẫn là `system/core/base/include`
- **Fix**: `device/samsung/sm6150-common/init/Android.bp`: `system/libbase/include` → `system/core/base/include`
- **Bài học**: lỗi path "does not exist" trên device tree mới ↔ build tree cũ = repos restructuring giữa hai era; đối chiếu layout thực tế của build tree rồi sửa path tương ứng

### C8. kati/make: deps module ngoài phạm vi halium-boot
- **Triệu chứng** (sau khi soong PASS, ở bước including Android.mk):
  - `bootable/recovery/updater` missing `librecovery_updater_sm6150` (lib vừa bị .find-ignore)
  - `device/samsung/sm6150-common/power/Android.mk`: missing `android.hardware.power-V1-ndk` (AIDL NDK chỉ có A12+)
  - `hardware/ril/reference-ril` missing `libril` (cả arm64 lẫn arm)
- **Root cause**: kati validate deps của TOÀN BỘ product graph (lineage_a70q pulls power/ril/recovery glue lineage-22.2), dù `mka halium-boot` chỉ build subset
- **Fix**: `ALLOW_MISSING_DEPENDENCIES=true mka halium-boot` — chính build system đề xuất cho partial build; lỗi thật (nếu depsNeeded cho halium-boot bị thiếu) sẽ bộc lộ ở bước ninja
- **Bài học**: với tree device khác era build tree, luôn chuẩn bị chạy với ALLOW_MISSING_DEPENDENCIES=true; nếu ninja báo thiếu ở module CẦN cho boot thì mới patch module đó

### C9. `ninja: unknown target 'halium-boot'`
- **Triệu chứng**: kati xong 26k rules, ninja không biết target `halium-boot`
- **Root cause**: Halium manifest halium-11.0 KHÔNG chứa repo `halium-boot`; GitHub `Halium/halium-boot` chỉ có branch `halium-9.0`/`master` — bản halium-11.0 nằm trên GitLab fork của core dev: `gitlab.com/amartinz/halium-boot` (branch `halium-11.0`)
- **Fix**: thêm vào local manifest: `project path=bootable/halium-boot name=amartinz/halium-boot revision=halium-11.0`; kèm glue generic arm64 của `./halium/devices/setup` (`halium/devices/manifests/halium_halium_arm64.xml` → `.repo/local_manifests/device.xml`: repos `device/halium/halium_arm64` + `vendor/vndk` — device reference, vô hại cho a70q)
- **Bài học**: `mka halium-boot` KHÔNG tự có trong halium-11 tree — phải thêm repo boot glue vào manifest; docs docs.halium.org 404 ở trang /porting/build.html, đừng tin link docs cũ

### C10. halium-boot tải initramfs bằng curl → bị AOSP cấm
- **Triệu chứng**: `"curl" is not allowed to be used` (PATH_Tools restriction); gợi ý `BUILD_BROKEN_USES_NETWORK := true`
- **Root cause**: halium-boot mặc định download initramfs từ GitHub releases trong make shell — A11 build system cấm tool mạng trong PATH build
- **Fix (đề xuất của chính README halium-boot, sạch hơn BUILD_BROKEN)**: tải trước `initrd.img-touch-arm64` (continuous) → đặt thành `device/samsung/a70q/initramfs.gz` + `BOARD_USE_LOCAL_INITRD := true` trong BoardConfig
- **Bài học**: mọi build step cần network phải chuyển thành "tải trước + flag local"; initramfs pin theo ngày tải → build tái lập được

---

### C11. `Disallowed PATH tool "mkbootimg"` khi đóng gói boot image
- **Triệu chứng**: kernel build xong, bước "Making halium-boot.img" chết: `"mkbootimg" is not allowed to be used`
- **Root cause**: A11 build có **path_interposer**: mọi tool gọi từ PATH phải nằm trong bảng `Configuration` của `build/soong/ui/build/paths/config.go`; tên không có trong bảng → `Missing` config → **Error: true**. `mkbootimg` (và `curl` ở C10) không có trong bảng mặc định của AOSP
- **Fix**: thêm `"mkbootimg": Allowed,` vào map `Configuration` trong `build/soong/ui/build/paths/config.go` (patch build tree — sẽ export vào bộ patch port cùng hybris patches)
- **Bài học**: mọi tool PATH mà halium glue gọi thêm phải nằm trong bảng này; cách debug nhanh: tool nào bị "Disallowed" → grep bảng `Configuration` trong config.go, thêm entry `Allowed`
- Lưu ý: Dockerfile symlink mkbootimg → clone AOSP riêng (`/opt/mkbootimg`) hoạt động bình thường khi được Allowed — không cần trỏ về build/make/tools

### C12. Samsung sboot anti-rollback: header `os_version` + SPL là floor 2 chiều
- **Triệu chứng**: flash `halium-boot.img` xong (heimdall OK) → máy reboot → **rớt thẳng về Download Mode**, không tới kernel
- **Root cause**: sboot ghi nhận floor anti-rollback từ ROM cuối ĐÃ BOOT thành công. Boot.img LOS 22.2 khai `os version: 15.0.0` + `os patch level: 2026-06` (decode từ header bằng unpack_bootimg — SPL thật của LOS 22.2 là 2026-06, KHÔNG phải 2025-05 như suy từ build ID). halium-boot khai 11.0.0/2024-02 rồi 11.0.0/2025-05 → đều dưới floor → sboot chối ở bootloader, chưa kịp kernel
- **Fix**: patch `bootable/halium-boot/Android.mk` thêm override `BOARD_BOOTIMG_OS_VERSION`/`BOARD_BOOTIMG_OS_PATCH_LEVEL` (PLATFORM_VERSION/PLATFORM_SECURITY_PATCH readonly) → a70q set `15.0.0`/`2026-06` khớp floor. os_version/SPL trong header chỉ là metadata bootloader — UT boot qua initramfs không đụng tới
- **Bài học**:
  - Trên máy đã chạy ROM mới (A15 + SPL 2026), custom boot header phải khai `os_version`/`SPL` >= floor — **os_version cũng bị check, không riêng SPL**
  - **Quy tắc: chạy `unpack_bootimg.py --boot_img <boot gốc>` trước khi flash custom boot, khớp mọi field floor**
  - vbmeta: bản minimal `make_vbmeta_image --flags 2` (rollback 0) KHÔNG phải nguyên nhân (original rollback cũng 0 — check bằng avbtool info_image)
  - Heimdall fork không có `--vbmeta-disable-verification` (flag fastboot-only) — phải tự generate vbmeta flags=2

### C13. Bootloop sau khi qua sboot: rescue script tự `reboot -f`
- **Triệu chứng**: sau khi khớp header (C12) máy KHÔNG về DM nữa mà **bootloop** (logo → reboot)
- **Root cause**: KHÔNG phải kernel panic — halium initramfs chạy tới `scripts/panic/telnet`, dựng USB gadget qua configfs với `USB_FUNCTIONS=rndis` nhưng kernel a70q KHÔNG có `USB_F_RNDIS` (defconfig chỉ bật `USB_CONFIGFS_NCM=y`) → không có iface `rndis0`/`usb0` → script chạy `reboot -f` → vòng lặp. Kèm: `console=null` giết shell panic (không thấy lỗi), và macOS không nói được RNDIS native
- **Fix**: vá `scripts/panic/telnet` trong initramfs (bản local `device/samsung/a70q/initramfs.gz` — BOARD_USE_LOCAL_INITRD) thêm nhánh `ncm` (mkdir/ln `functions/ncm.usb0`) + `USB_FUNCTIONS=ncm`; NCM có SẴN trong kernel và macOS ≥12 hỗ trợ NCM native. Kernel giữ nguyên
- **Bài học**:
  - Đọc TRƯỚC script rescue trong initramfs (`zcat initramfs.gz | cpio -idm`) — nó tự reboot khi USB setup fail; IP rescue là `192.168.2.15` (telnet 23, udhcpd cấp host từ 192.168.2.20), KHÔNG phải 172.16.42.1
  - Khớp function class giữa initramfs script và kernel config trước khi flash
  - Bootloop vs về-DM = 2 loại chặn khác nhau: về-DM = sboot chối ở bootloader; bootloop = kernel+initramfs chạy rồi nhưng rescue/setup fail

### C14. Initramfs intermediate stale — patch bị bỏ qua khi package lại
- **Triệu chứng**: đã patch `USB_FUNCTIONS=ncm` vào `device/samsung/a70q/initramfs.gz`, nhưng image flash ra VẪN chứa rndis (verify bằng extract initramfs từ `halium-boot.img`)
- **Root cause**: rule `$(HALIUM_BOOT_RAMDISK)` trong `halium-boot/Android.mk` **không có prerequisite** tới source `.gz` trong device tree — intermediate `obj/ROOT/halium-boot_intermediates/halium-initramfs.gz` tồn tại từ lần build trước → make/ninja coi là up-to-date → không re-copy
- **Fix**: thêm dep `$(HALIUM_BOOT_RAMDISK): $(HALIUM_LOCAL_INITRD)` (wildcard `device/*/$(TARGET_DEVICE)/initramfs.gz`) + xoá intermediate cũ 1 lần
- **Bài học**: rule chỉ viết lệnh `cp` mà không khai dep nguồn = cached-mãi; **mọi thay đổi asset trong device tree phải verify bằng extract lại từ image cuối** (giờ đã thành reflex: unpack_bootimg + grep trong ramdisk)

### C15. Toolchain cho kernel 4.14.261: gcc 4.9 quá cũ, clang-11 nghi vấn panic
- **Triệu chứng 1**: `#error Sorry, your version of GCC is too old - please use 5.1 or newer` khi build bằng gcc 4.9 prebuilt
- **Root cause**: kernel 4.14.261 đã merge backport yêu cầu gcc >= 5.1; tree halium-11 chỉ có prebuilt `aarch64-linux-android-4.9` (không có gcc mới hơn); clang prebuilt là r383902b (clang 13) — 4.14 + clang-13 rủi ro codegen (kinh nghiệm note10lite: clang mới → hang boot)
- **Fix**: **gcc 9.4 của focal** — thêm `gcc-aarch64-linux-gnu` vào Dockerfile + `TARGET_KERNEL_CROSS_COMPILE_PREFIX := aarch64-linux-gnu-` (build trong container amd64 = cross chuẩn cho aarch64). Interposer cần whitelist cả họ `aarch64-linux-gnu-*`
- **KẾT LUẬN SỬA (sau khi đủ bằng chứng)**: attempt #4 bootloop giải thích TRỌN VẾN bằng C14 (initramfs stale rndis → reboot -f) — **kernel clang chưa từng được test cùng initramfs đã vá**, nghi toolchain clang là suy luận sớm. Quay về clang r383902b (toolchain Samsung/rtd1250 dùng cho kernel này qua gcc-wrapper) + giữ mọi fix khác. gcc 9.4 build được nhưng đòi vá nguồn 3 cụm driver Samsung (techpack/audio `const const` + `-Werror`; imagis touch `-Werror` + snprintf; panel S6E3FA7 dùng float với `-mgeneral-regs-only`) — chỉ quay lại nếu clang kernel bị chứng minh lỗi lúc boot. Phần dưới giữ làm fallback có tài liệu
- **Bài học (fallback gcc)**:
  - Chọn toolchain theo **bản kernel nhánh con**: 4.14 ổn với gcc 5.1–10, clang ≤9; branch mới = toolchain mới hơn được phép
  - Khi đổi toolchain: whitelist cả HỌ tool (gcc/ld/as/ar/nm/objcopy/objdump/readelf/strip/ranlib/size) cho TẤT CẢ prefix — mỗi vòng ninja chỉ lộ 1 tool đầu tiên
  - `ccache` gọi bằng path tuyệt đối → không cần whitelist
  - gcc-9 trên kernel Samsung cần vá: `techpack/audio/asoc/msm-pcm-routing-v2.c` (7× `static const int const` trùng), `drivers/input/touchscreen/imagis_40xx` (-Werror + snprintf sizeof(dst)==dst), panel `S6E3FA7_AMS628RF05` (float + `-mgeneral-regs-only`) — hoặc passthrough gcc-wrapper.py như đã làm

### C16. Boot header cmdline giới hạn 512 byte — truncation âm thầm
- **Triệu chứng**: `panic=0` thêm vào cuối cmdline KHÔNG xuất hiện trong boot image đã build (verify header = False), trong khi ramoops params (thêm trước đó) lại có
- **Root cause**: cmdline field trong boot header cố định **512 byte**; mkbootimg cắt bớt phần vượt mà không báo lỗi. Cmdline gốc 417 + ramoops 134 + panic=0 > 512 → đuôi bị cắt
- **Fix**: bỏ 3 tham số phụ không ảnh hưởng boot (`msm_rtb.filter=0x237`, `service_locator.enable=1`, `video=vfb:...`) → 472 chars, mọi param quan trọng nằm gọn
- **Bài học**: sau mỗi lần sửa cmdline PHẢI verify bằng `unpack_bootimg.py | grep` — mkbootimg không cảnh báo truncation; ưu tiên bỏ tham số debug/profiling (msm_rtb, vfb) thay vì tham số chức năng

---

### C17. `$0` trong cmdline bị EXPAND HAI LỚP (make/kati rồi shell)
- **Triệu chứng**: thêm `memmap=0x100000$0xa1300000` vào `BOARD_KERNEL_CMDLINE` → image build xong cmdline chứa `memmap=0x100000/bin/bashxa1300000` (raw bytes: backspace + `ashx` do hai vòng expand chồng nhau)
- **Root cause**: chuỗi đi qua 2 tầng expand: (1) kati/make expand `$0` → path shell (`/bin/bash`), (2) recipe shell expand tiếp. `$$0` chỉ sống sót tầng 1
- **Fix**: tránh ký tự `$` trong cmdline hoàn toàn — memmap dùng address hex đi kèm reservation bằng cách khác (DT node, hoặc memmap với cú pháp không $), hoặc sinh cmdline bằng script thay vì qua biến make
- **Bài học**: mọi giá trị cmdline chứa `$` đều chết trên đường make→kati→shell của AOSP; kiểm bằng unpack_bootimg + grep raw

---

## D. Quy trình (meta)

### D1. Pipe vào `tail` nuốt exit code
- **Triệu chứng**: job báo "exited 0" trong khi build FAILED
- **Root cause**: `mka ... | tail -60` — exit code của pipeline là exit code của `tail`
- **Fix**: luôn `set -o pipefail` trước khi chạy build có pipe

### D2. Tin tài liệu cũ về branch/tài nguyên
- Tài liệu (PLAN cũ) nói vendor có lineage-18.1 — thực tế đã bị xoá. **Mọi con số branch/repo phải verify bằng API/GitHub tại thời điểm dùng** (đã thành quy tắc trong README)

### D3. repo nuốt lỗi git
- `repo sync`/checkout bọc mọi lỗi git thành thông báo chung, không kèm stderr. Debug bằng cách chạy tay lệnh git tương đương trong worktree/gitdir (xem A3)

### D4. Brew cask `heimdall-suite` đã bị disable (2026-03)
- **Triệu chứng**: `brew install --cask heimdall-suite` → disabled by Homebrew
- **Fix**: dùng fork proven — `heimdall-apple-silicon` (aljosasavic) v2.2.2: tải `heimdall-macos-arm64` từ GitHub releases vào `~/bin/heimdall`, chmod +x
- **Bài học**: công cụ flash đã proven ở máy cũ (6 lần flash) — tái dùng đúng binary, đừng thử cask mới chưa test

---

## E. Trạng thái hiện tại (2026-11-11)

- Soong parse sạch: vendor/samsung, vendor/apn pruned; device common chỉ giữ `init/`
- `breakfast a70q` PASS: `TARGET_PRODUCT=lineage_a70q`, Android 11, HOST_ARCH=x86_64 (Rosetta)
- Kernel vars chuẩn từ BoardConfigCommon: `TARGET_KERNEL_SOURCE=kernel/samsung/sm6150`, `TARGET_KERNEL_CONFIG=a70q_defconfig`, `BOARD_BOOTIMAGE_PARTITION_SIZE=67108864`, `BOARD_KERNEL_SEPARATED_DTBO=true`, pagesize 4096
- `mka halium-boot` đang lặp: đã qua mọi lỗi parse; chờ compile kernel 4.14 bằng clang prebuilt (x86 qua Rosetta)
- Fix BT đã nằm sẵn trong kernel tree: Makefile uncomment 3 dòng + `a70q_defconfig` bật RFCOMM(+TTY)/BNEP(+MC+PROTO_FILTER)/HIDP
- `hybris-patches/apply-patches.sh` đã chạy OK (điều kiện trước `mka halium-boot`)

### C19. Debug mù trên thiết bị không có UART/console: bật fbcon lên màn hình
- **Bối cảnh**: kernel chết/panic siêu sớm, không log nào thu được (pstore attach ~0.16s có thể quá muộn; misc-dump cần /dev/block đã mount)
- **Fix cấu hình kernel (a70q_defconfig)**: `CONFIG_DRM_FBDEV_EMULATION=y` (trước đó =n!) + `CONFIG_FRAMEBUFFER_CONSOLE=y` + `CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY=y` + `CONFIG_FONT_SUPPORT=y` + `CONFIG_FONT_8x16=y`
- **Fix cmdline** (BoardConfigCommon): thêm `console=tty0` (fbcon trên vt0) — kernel log + panic hiển thị TRỰC TIẾP trên AMOLED (panel sống từ splash ABL), user chụp ảnh gửi lại = đọc được panic
- **Kèm theo**: `panic=0` giữ máy đứng yên hiển thị panic thay vì reboot-loop
- **Bài học**: với port không có UART, fbcon là kênh debug chính từ giai đoạn sớm nhất (sau khi DRM panel probe ~1-2s); chi phí: kernel rebuild + vài trăm KB

### C20. ⭐ ROOT CAUSE chính của mọi bootloop/initramfs-fail: Samsung kernel TẮT DEVTMPFS
- **Triệu chứng tổng hợp**: MỌI halium-boot đều bootloop nhanh; misc rỗng; không marker; không USB; không rescue — kể cả khi kernel LOS (proven) + initramfs halium
- **Root cause**: `a70q_defconfig` gốc Samsung: `# CONFIG_DEVTMPFS is not set` — kernel KHÔNG có devtmpfs. Halium initramfs (và mọi initramfs chuẩn Linux) YÊU CẦU devtmpfs: `/init` mount devtmpfs → nếu fail → /dev rỗng → không /dev/console, không /dev/block/sda10 (misc-dump fail), không anything → init chết giữa chừng → "attempted to kill init" → panic_timeout 5s → reboot → loop
- **Fix**: `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y` trong a70q_defconfig → rebuild
- **Bài học**:
  - Samsung SEC kernels TẮT devtmpfs (dùng ueventd path) — MỌI kernel port cho device Samsung phải bật DEVTMPFS+y trước khi debug bất cứ thứ gì khác
  - Instrumentation đặt TRƯỚC mount /dev (marker đầu /init) vô hiệu — đặt SAU; tốt hơn: dùng device node path đúng qua devtmpfs
  - pstore-ram qua cmdline ghi console từ ~0.16s — nhưng đọc lại bằng boot sau chỉ đúng nếu chuỗi reboot/DM là WARM (không ngắt nguồn); force-tắt = mất records
  - LOS reader boot ok vì LOS kernel của rtd1250 có thể cũng devtmpfs=n nhưng ramdisk LOS tự chứa /dev nodes (mknod trong cpio) — halium initramfs KHÔNG chứa dev nodes (dựa vào devtmpfs)

### C21. ⭐ heimdall detect làm ĐIỆN THOẠI THOÁT Download Mode (poll = tự đá máy)
- **Triệu chứng**: user vào DM thành công → vài chục giây sau máy TỰ thoát DM + reboot về bootloop — lặp vô tận, tưởng DM timeout
- **Root cause**: mỗi lệnh `heimdall detect` mở USB interface → handshake → **close/release** → Samsung DM (sboot) xử lý close = KẾT THÚC phiên Download Mode và REBOOT máy ra boot partition. Poll detect liên tục (của agent hoặc user) = tự đá máy khỏi DM liên tục
- **Fix/quy tắc**: user vào DM MỘT LẦN → chạy `heimdall flash` LIỀN (flash tự detect+handshake atomically) — **TUYỆT ĐỐI KHÔNG chạy detect/watcher-poll trước hoặc trong khi flash**; watcher (nếu cần) chỉ kiểm và flash trong CÙNG process khi thấy, không poll thả
- **Bài học**: các lần "dm rồi rồi mất" đêm nay đều do detect-poll của agent (và detect loop user tự chạy theo hướng dẫn của agent)

### C22. Hai heimdall process đồng thời trên cùng DM = một thắng một rớt IOService
- **Triệu chứng**: watcher auto-flash + manual flash chạy cùng lúc → manual flash Thành công, watcher rớt `bulk transfer failed: no connection to an IOService` (hoặc ngược lại)
- **Root cause**: hai process claim cùng USB interface của DM — một cái giữ, cái kia bị đá khỏi IOService
- **Quy tắc**: kill mọi watcher/heimdall khác TRƯỚC khi chạy flash; một flash = một process duy nhất
