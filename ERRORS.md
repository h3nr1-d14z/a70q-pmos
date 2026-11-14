# ERRORS.md — Sổ lỗi & bài học (cập nhật khi gặp lỗi mới)

Mục đích: không mắc lại lỗi nào hai lần, và ai tiếp nhận repo sau này không phảiDebug lại từ đầu. Format: **Triệu chứng → Root cause → Fix → Bài học**.

Ngày lập: 2026-11-11. Mục A–E: giai đoạn Ubuntu Touch/Halium (đã dừng; file liên quan ở `archive/ubuntu-touch/`). Từ mục F: postmarketOS.

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

## E. Trạng thái giai đoạn Ubuntu Touch (2026-11-11, đã dừng)

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

## F. Chuyển hướng pmOS (2026-11-13) — lỗi mới + đính chính ghi chú cũ

> Mọi mục dưới đây đã được **tự kiểm chứng** (đọc binary/header/dmesg/ioreg trực tiếp), không chép từ ghi chú cũ.

### C23. ⭐ Cổng USB front của Mac mini M4 giữ "node ma" Download Mode → máy chỉ báo sạc, không adb
- **Triệu chứng**: sau một phiên heimdall, LOS boot xong cắm cáp chỉ báo "đang sạc", không có USB debugging prompt, `adb devices` rỗng; Wireless debugging cũng tự tắt (lỗi riêng, xem dưới)
- **Bằng chứng**:
  - Mac: `ioreg -p IOUSB` vẫn thấy `SM6150` 04e8:**685D** (CDC-ACM = Download Mode) dưới `AppleT8132USBXHCI@02000000 → USB2 Hub` với **cùng `sessionID`** cả sau khi rút cáp vật lý → host không xử lý attach mới
  - Điện thoại (dmesg lúc cắm lại): CCIC `Turn On Device(UFP)`, APSD=CDP, `dwc3_otg_start_peripheral: turn on gadget`, `peripheral state` → **phía máy hoàn toàn đúng**, host không enumerate
  - Phía Android: adbd running, gadget g1 `ffs.adb` idProduct 0x6860, UDC bound — không có lỗi
- **Fix**: cắm vào **cổng USB-C phía SAU** (XHCI riêng, không qua hub) → `SAMSUNG_Android` enumerate, USB adb lên ngay. Node ma tự biến mất sau ~1h (reboot Mac cũng xoá được)
- **Bài học**: khi USB "không làm gì", kiểm `ioreg -p IOUSB` tìm node SM6150 cũ TRƯỚC khi nghi kernel/userspace. A/B test (k0 vs kernel LOS gốc) đều "không adb" là dấu hiệu lỗi ngoài thiết bị

### C24. ⭐ heimdall: lần flash ĐẦU TIÊN sau khi vào DM luôn chết ở handshake — retry ngay là được
- **Triệu chứng**: `libusb ... pipe is stalled` → `no connection to an IOService` → `ERROR: Failed to send handshake!` — **chưa ghi byte nào**, máy vẫn ở DM (ioreg còn 685D)
- **Tần suất**: 3/3 lần (2 lần cổng front, 1 lần cổng sau) → KHÔNG phải do hub; heimdall v2.2.2 apple-silicon trên macOS 26
- **Fix**: chạy lại đúng lệnh flash một lần ngay lập tức → thành công 3/3. Đã tự động hoá trong `pmos/tools/flash.sh`: kill adb server → chờ DM bằng **ioreg (không `heimdall detect`)** → flash → nếu stall *trước khi bắt đầu Uploading* thì retry đúng 1 lần; từ chối node DM nằm trên hub front (nghi node ma C23)
- Nguyên nhân gốc chưa xác định (nghi libusb claim interface lần đầu trên macOS)
- **Cập nhật**: từ khi dùng `flash.sh` (chờ DM qua ioreg vài giây rồi mới flash) đã 4/4 lần flash thành công ngay lần đầu → nghi stall do chạy heimdall quá sớm khi DM vừa enumerate

### C25. Wireless debugging tự tắt + không có hộp thoại "Allow USB debugging"
- `/data/misc/adb/adb_keys` **rỗng** (mtime 2026-11-13 19:51) và SystemUI không hiện prompt → mọi kết nối adb đều `unauthorized`
- **Fix (root, qua SSH Termux)**: append `~/.android/adbkey.pub` của Mac vào `/data/misc/adb/adb_keys`, `chown system:shell`, `chmod 640`, `restorecon` → `adb connect <ip>:5555` OK (adbd có `persist.adb.tcp.port=5555`)

### Đính chính ghi chú cũ (đã kiểm chứng sai)
- **`backups/full/boot.img` = `magisk_patched.img`** (cùng SHA1), KHÔNG phải boot LOS gốc. Boot LOS gốc: `a70-root/artifacts/boot/lineage-boot-orig.img`. Magisk không vá kernel (kernel 2 bản giống từng byte)
- **Boot LOS không có footer `SEANDROIDENFORCE`** — sau payload là **AVB hash footer** (`AVB0`…`AVBf`). Boot tự đóng gói không có footer vẫn boot được khi flash kèm vbmeta flags=2. vbmeta trong zip LOS 22.2 có flags=3 (LOS vốn chạy với verification tắt)
- **C20 sửa phần giải thích**: ramdisk LOS có thư mục `dev/` **rỗng** (không có node mknod sẵn) — Android first-stage init tự mount tmpfs lên /dev. Kết luận "halium/pmOS initramfs cần DEVTMPFS" vẫn đúng; lý do "LOS ramdisk chứa sẵn /dev nodes" là sai
- **Blob LOS CÓ IKCONFIG** (`/proc/config.gz` + `IKCFG_ST` trong Image); config nhúng = `a70q_defconfig` của a70q-lineage `lineage-22.2` @ `699d9d7da` (trừ `CONFIG_SEC_R1Q_PROJECT` do commit r1q sau đó thêm)
- **Toolchain không phải nguyên nhân chết sớm**: blob LOS boot được build bằng clang **19** (r536225); kernel tự build bằng Ubuntu clang 19.1.1 từ cây 4.14.190 boot OK (k0, k1). Các kernel chết sớm đều từ cây **4.14.261** (rtd1250 lineage-18.1) → nghi phạm là cây kernel, không phải clang-13/gcc
- `WINDOWS.md`: `pip install mkbootimg` mâu thuẫn A2; dùng `pmos/tools/bootimg.py` (không phụ thuộc gì, đã kiểm chứng tái tạo từng byte header+payload boot LOS)

### C26. pmbootstrap build kernel: musl không có `__attribute_format_arg__`
- `scripts/kconfig/lkc.h:15: expected declaration specifiers ... before numeric constant` khi `oldconfig` trong chroot Alpine (musl). Macro glibc-only trong nhánh `KBUILD_NO_NLS`
- Fix (APKBUILD prepare): `sed -i '/^__attribute_format_arg__(1)$/d' scripts/kconfig/lkc.h`

### C27. ⭐ GNU make 4.4: "prerequisites cannot be defined in recipes" (Makefile.lib:332) — và cái bẫy khi sửa
- Alpine edge make 4.4.1 (Ubuntu 24.04 = 4.3 build OK). make 4.4 truyền biến exported vào `$(shell)`; các dòng `export` TRẦN trong 27 Kbuild của `techpack/audio` (đang ACTIVE) export mọi biến → parse `size_append` gãy
- ❌ Fix sai (đã thử): xoá các dòng `export` → build OK nhưng **mất toàn bộ driver audio** (thiếu 212 chuỗi compatible, Image nhỏ ~2.5MB) — chỉ phát hiện nhờ so compatible-strings với k1
- ✅ Fix đúng: thay bằng `export $(filter CONFIG_%,$(.VARIABLES))` → Image đúng 45,092,888 B, compatible 2757/2757 khớp k1, DTB giống từng byte LOS
- **Bài học**: sau mọi "fix build", so sánh artifact (compatible strings / size / DTB) với bản đã biết tốt — build xanh ≠ kernel đúng

### C28. busybox ash (abuild) không parse `$(` trong comment bên trong function
- `APKBUILD: line 83: syntax error: unterminated quoted string` dù `bash -n` OK. Thủ phạm: comment chứa `$(shell)` trong `prepare()`. Kiểm APKBUILD bằng `ash -n`, không phải bash

### C29. ⭐⭐ BUG KERNEL: `u_ether` RX chết sau đúng 10 gói (NCM lẫn ECM) → không có USB networking
- **Triệu chứng**: Mac thấy interface, link active, nhưng DHCP/ping fail; NCM: Mac `Oerrs` tăng; ECM: Mac gửi OK nhưng máy không nhận. Phía máy `rx_packets` dừng ở **10** mỗi lần ifup (down/up → +10 rồi dừng)
- **Root cause** (`drivers/usb/gadget/function/u_ether.c` `rx_complete()`, bản Samsung): nhánh `case 0` xử lý frame inline nhưng **không set `queue = 1`** → request chỉ bị trả về `rx_reqs`, không resubmit, `rx_work`/`rx_fill` không chạy → hết qlen (10 ở high-speed) là RX chết. Android không bị vì adb/MTP dùng FunctionFS, tethering dùng IPA/GSI
- **Fix**: `pmos/kernel/patches/0001-usb-gadget-u_ether-resubmit-rx-request.patch` (thêm `queue = 1;`) → kiểm chứng: 8 MB Mac→máy sha256 khớp, 5894 gói, 0 lỗi, ~46 MB/s
- Kèm theo: kernel bật thêm `CONFIG_USB_CONFIGFS_ECM=y` (k2); deviceinfo `ecm.usb0`. NCM có thể cũng chạy sau fix (chưa thử lại)

### C30. pmOS initramfs `pmos.debug-shell`: thêm ACM → rebind UDC → netdev `ecm0` bị tạo lại, mất IP, telnetd/unudhcpd bind hụt
- dmesg `ecm0 all refcnt has gone` lúc ~19s; `ecm0` DOWN không IP; telnetd không listen
- Workaround qua serial ACM (`/dev/cu.usbmodempostmarketOS3`): `ifconfig ecm0 172.16.42.1 netmask 255.255.255.0 up; kill $(pidof unudhcpd); unudhcpd -i ecm0 -s 172.16.42.1 -c 172.16.42.2 &; telnetd -b 172.16.42.1:23 -l /sbin/pmos_getty`
- Chưa sửa gốc (chỉ ảnh hưởng debug-shell)

### Công cụ mới
- `pmos/tools/src/reboot-mode.c` (static musl): `reboot-mode download` từ shell pmOS → vào Download Mode không cần bấm phím (đã kiểm chứng)

## G. Boot rootfs pmOS từ userdata (UFS 4K) — 2026-11-14

### C31. Rootfs mặc định là systemd — KHÔNG chạy được trên kernel 4.14
- `pmaports.cfg`: `systemd_linux_min_version=5.4`. Phải dùng OpenRC: trong pmbootstrap_v3.cfg đặt **`service_manager = openrc`** (`systemd = never` KHÔNG có tác dụng — đã thử). Đổi xong phải `pmbootstrap zap` (zap -p không đủ)

### C32. pmbootstrap install trong Docker: `File did not appear: /dev/loop0p2`
- Container không có udev → node phân vùng loop không tự tạo. `pmos/pmb/run.sh` chạy vòng nền mknod từ `/sys/block/loop*/loop*p*/dev`

### C33. heimdall flash USERDATA image thô chết ở 7% (`Failed to confirm end of file transfer sequence`)
- sboot cần **Android sparse** cho partition lớn: `img2simg samsung-a70q.img samsung-a70q.sparse.img` (787MB → 153MB; simg2img ngược lại giống từng byte) → flash OK
- Sau một phiên DM lỗi giữa chừng, DM không nhận phiên mới (`Protocol initialisation failed`) → phải vào DM lại

### C34. Bỏ mất `loop.max_part=7` khỏi cmdline → loop không quét được partition (rc=-22)
- dmesg `loop_reread_partitions: partition scan of loop0 failed (rc=-22)`; cmdline LOS có `loop.max_part=7` — mình đã lược đi khi viết deviceinfo. **Bài học: khi viết cmdline mới, diff với cmdline gốc từng tham số**

### C35. ⭐⭐ Subpartition pmOS trên UFS 4K: 3 lỗi chồng nhau (2 cấu hình + 2 bug kernel)
- UFS `logical_block_size=4096` → image phải dùng `deviceinfo_rootfs_image_sector_size="4096"` (với 512, fdisk trên sda29 không thấy 2 partition)
- initramfs chạy `losetup -Pf --direct-io=on --sector-size 4096`, nhưng loop vẫn **512** → GPT 4K không thấy
- Root cause (kiểm bằng `LOOPDEV_DEBUG=all losetup ...` → `LOOP_CONFIGURE: OK`): kernel có **backport LOOP_CONFIGURE của Android** nhưng chỉ đưa `block_size` vào `set_blocksize()`, KHÔNG đặt queue logical block size → **patch 0004** (đặt `blk_queue_logical_block_size` như upstream)
- Đường cũ `LOOP_SET_BLOCK_SIZE` cũng hỏng: trả `EAGAIN` ("has still dirty pages (nrpages=2)") vì `invalidate_bdev()` không bỏ được page do partition scan để lại → **patch 0003** (invalidate_bh_lrus + truncate_inode_pages như upstream kill_bdev 5.3); **patch 0002** rescan partition sau khi đổi block size
- Cách debug hiệu quả: boot `pmos.debug-shell`, tool C nhỏ gọi ioctl trực tiếp (`loopbs`) + `echo 3 > drop_caches` để tách từng giả thuyết
- Kết quả: `/dev/loop0p2 on / type ext4`, tự resize 108GB, SSH `user@172.16.42.1` OK

### C36. post-install của device package chạy TRƯỚC postmarketos-base-openrc
- `ln -s unudhcpd unudhcpd.ecm0` + `rc-update` trong post-install không có hiệu lực → ship symlink service + runlevel trực tiếp trong package

### C37. Upload qua serial: kiểm hash CẢ HAI đầu, không đếm dòng
- `reboot-mode` upload hỏng (0 byte) nhưng kiểm tra lỏng (grep -c) báo OK → flash.sh chờ DM 180s vô ích. So sánh remote sha256 == local sha256 tường minh

## H. Hiển thị (2026-11-14)

### C38. ✅ Màn hình AMOLED chạy qua DRM/KMS — "không thấy gì" là lỗi cách test
- `/dev/dri/card0`, connector `DSI-1` connected, mode `1080x2400x60x184032` (tên mode kiểu Samsung → `modetest -s 28@102:#0`, KHÔNG `1080x2400`)
- `modetest` chạy nền với stdin không phải tty → đọc EOF ngay → thoát → giải phóng fb → panel tắt sau ~0.4ms (log: `DISPLAY_ON` rồi lập tức `ss_brightness_dcs level 0` + `ss_panel_off_pre`). User chỉ thấy "nháy 1 cái"
- Fix test: giữ stdin mở `sleep 120 | modetest -M msm_drm -s 28@102:#0` → **user xác nhận thấy pattern**. Backlight do driver tự bật lúc bridge_enable (không cần ghi sysfs)
- **Bài học**: đọc timestamp dmesg từng bước (enable → DISPLAY_ON → off) trước khi đổ lỗi cho driver

### C39. ❌ fbdev trên SDE → bootloop (patch 0005 đã gỡ, để ở kernel/patches/disabled/)
- Không có `/dev/fb0`: `msm_fbdev_create()` dùng `priv->kms->aspace` (SDE không set) → `failed to get buffer obj iova: -22`
- Sửa aspace (lấy qua `get_address_space(MSM_SMMU_DOMAIN_UNSECURE)`) → kernel **bootloop**, không bắt được log
- Kết luận: không cần fbdev — UI pmOS (Phosh/Plasma/Sxmo) dùng KMS trực tiếp

### C40. Không bắt được panic log: ramoops/pstore không giữ qua reboot do panic
- ramoops attach `0x100000@0xa1300000` + `console [pstore-1]`, nhưng sau `echo c > /proc/sysrq-trigger` → reboot → /sys/fs/pstore rỗng. `/proc/last_kmsg` chỉ chứa log ABL (bootloader)
- Hệ quả: thử kernel rủi ro → nếu bootloop phải vào DM bằng phím; ưu tiên kiểm chứng trên kernel đang chạy (tool userspace) trước khi đổi kernel

### C41. Firmware: kernel chờ `/vendor/firmware_mnt/image` → timeout 60s mỗi lần (venus...)
- `apnhlos` = sda17 (vfat, có a612_zap, adsp, bdwlan.*...), `modem` = sda18 (vfat), `dsp` = sda19 (ext4)
- ~~WiFi: `CONFIG_QCA_CLD_WLAN` chưa bật~~ **SAI (đính chính)**: `CONFIG_QCA_CLD_WLAN=y` (profile default) có sẵn trong config (giống LOS), module `wlan` + `icnss` đã nạp

## I. Modem + WLAN (2026-11-14) — ✅ WiFi chạy (C43)

Tiến độ (tất cả kiểm chứng trên máy, nay là `/usr/libexec/a70q/a70q-modem-up` trong `device-samsung-a70q`):
- ✅ Firmware: mount `apnhlos`(sda17)→/vendor/firmware_mnt, `modem`(sda18)→/vendor/firmware-modem; symlink modem.* vào /lib/firmware cho PIL
- ✅ `restart_level=RELATED` cho mọi subsystem (mặc định SYSTEM → modem crash = reboot cả máy)
- ✅ Modem boot qua `/dev/subsys_modem` (giữ fd mở) → ONLINE ổn định khi đủ dịch vụ
- ✅ Dịch vụ QRTR userspace (kernel 4.14 KHÔNG có name service trong kernel):
  - `qrtr-ns` (linux-msm/qrtr @6d950d2, commit cuối trước khi bỏ ns), chạy `qrtr-ns -f 1`
  - `rmtfs -P -r` (READ-ONLY, shadow buffer — EFS thật không bị ghi); cần `/dev/qcom_rmtfs_uio1 -> /dev/uio0` (uio0 name=rmtfs)
  - `pd-mapper` + patch `PD_MAPPER_JSON_DIRS` (downstream không có /sys/class/remoteproc)
  - `tqftpserv` + patch fallback `TQFTPSERV_FW_DIRS` (cùng lý do) → modem tải được `wlanmdsp.mbn`
  - ⭐ Bẫy: phải CHỜ qrtr-ns trả lời (lookup thấy service 52) rồi mới chạy dịch vụ — publish quá sớm thì gói NEW_SERVER mất im lặng → thiếu rmtfs → modem fatal sau ~40s
- ✅ icnss: QMI WLFW connected, msa_info/msa_ready/cap OK — chip_id 0x140 (WCN3990), fw 0x301a82cf (2019-08-21)
- ✅ **WiFi CHẠY** (scan thấy mạng xung quanh): mảnh thiếu là BDF — icnss downstream KHÔNG gửi board data; Android do cnss-daemon (độc quyền) làm
  - `a70q-qcom-userspace/a70q-bdf.c`: QMI WLFW (service 0x45) `BDF_DOWNLOAD` (msg 0x25, đoạn 6144B, file `apnhlos/image/bdwlan.bin` vì board_id 0xff) + `CAL_REPORT` rỗng (msg 0x26) → firmware gửi `FW_READY` ngay (`icnss: WLAN FW is ready`)
  - qcacld built-in hoãn khởi tạo: `echo 1 > /sys/kernel/boot_wlan/boot_wlan`, rồi `echo ON > /dev/wlan` (việc của wifi HAL Android) → tạo `wlan0` + `p2p0`
  - Cần `WCNSS_qcom_cfg.ini` (+regdb.bin) từ partition vendor ở `/lib/firmware/wlan/qca_cld/`
  - Toàn bộ đã gom vào `a70q-modem-up.sh`
- Debug tốt nhất: `/sys/kernel/debug/ipc_logging/icnss/log` (chi tiết hơn dmesg), `/sys/kernel/debug/icnss/stats`

### C42. Bộ lọc grep tự gây hiểu nhầm
- `grep -v 4097` để bỏ log DIAG đã xoá luôn các dòng `add server [4097:..]` → tưởng ns từ chối mọi server. Lọc theo nội dung cần tìm, không lọc ngược quá rộng

### C43. ✅ Kết nối WiFi + internet
- `wpa_passphrase` → `/etc/wpa_supplicant/wpa_supplicant.conf` (chỉ PSK băm, 600) → `wpa_supplicant -D nl80211 -i wlan0 -c ...` → WPA2 CCMP OK → `udhcpc -i wlan0` → IP 192.168.1.x, ping internet, `apk update` OK, SSH qua WiFi OK
- Bẫy: wpa_supplicant của Alpine KHÔNG hỗ trợ `-f <logfile>` (in usage rồi thoát) → dùng redirect stdout
- Bẫy: `/etc/resolv.conf` trong rootfs là file Docker sinh lúc pmbootstrap build (nameserver 0.250.250.200) — DHCP ghi đè; nên xoá khi đóng gói image

## J. Giao diện, GPU, âm thanh, suspend (2026-11-14)

### C44. Flash từ xa: `reboot-mode` gửi qua SSH chạy trễ → flash.sh hết giờ chờ
- `ssh ... 'sudo sh -c "(sleep 1; reboot-mode download) &"'`: tiến trình nền gắn phiên SSH, máy vào DM sau khi flash.sh (chờ 180s) đã bỏ cuộc; bộ lọc grep của mình còn giấu dòng "no Download Mode device"
- Fix: flash.sh chờ 300s; gọi `sudo setsid -f reboot-mode download` (tách khỏi phiên); không lọc output flash.sh bằng whitelist

### C45. ✅ Giao diện Sxmo + cảm ứng
- Cảm ứng mất khi boot từ màn hình sạc: cmdline `androidboot.mode=charger` → sec_battery đặt `lpcharge=1` → `ist40xx_init: Do not load driver due to : lpm 1` → patch kernel 0006 (luôn lpcharge=0)
- Sxmo báo `No deviceprofile found qcom,sm6150p-idp` (DT dùng compatible bo mạch tham chiếu) → device package ship `/usr/bin/sxmo_deviceprofile_qcom,sm6150p-idp.sh`
- CPU không bị khoá: governor performance, max 1.7/2.0 GHz; chậm là do render CPU (pixman) + boot chờ firmware venus

### C46. ✅ Boot nhanh 3x: tắt CONFIG_FW_LOADER_USER_HELPER_FALLBACK
- Android dùng ueventd nạp firmware qua user-helper; trên pmOS không ai trả lời → kernel chờ 60s mỗi firmware thiếu (venus lúc v4l_id trong initramfs: `firmware state wait timeout`)
- Tắt FALLBACK (kernel r14) → firmware thiếu báo lỗi ngay; modem/WLAN nạp file trực tiếp nên không ảnh hưởng
- Đo: SSH USB 41s, WiFi 53s sau flash (trước: ~100–190s / ~3 phút); 0 timeout
- flash.sh giờ luôn ghi log `/tmp/a70q-flash-*.log` (lần trước thoát sớm mà output bị pipe nuốt)

### C47. ✅ GPU Adreno 612 qua kgsl: chuỗi 5 trở ngại
- Kernel có kgsl (`Adreno612v1`), firmware: zap `a612_zap.*` trên apnhlos; `a630_sqe.fw` + `a612_rgmu.bin` chỉ có trên partition **vendor** (sda24) → chép vào `/lib/firmware` (khi mở kgsl lần đầu thấy `a612_zap: Brought out of reset`)
- `/dev/kgsl-3d0` mặc định 0600 root (Android dùng ueventd) → udev rule 0666; `/dev/ion` cũng cần cho dma-buf (không có `/dev/dma_heap`)
- **Mesa của Alpine không có backend kgsl** (`-Dfreedreno-kmds=msm,virtio`): turnip chỉ thấy `renderD128` (msm_drm = SDE hiển thị) → `not compatible with turnip`. Lưu ý chuỗi "kgsl" vẫn có trong .so (tên tuỳ chọn TU_DEBUG) — không chứng minh được gì; xác nhận bằng APKBUILD → tự build turnip `msm,kgsl`
- Package riêng bị abuild tự thêm `provides so:libvulkan_freedreno.so` → conflict với mesa-vulkan-freedreno → `patchelf --set-soname libvulkan_freedreno_kgsl.so`
- `vulkaninfo` báo INITIALIZATION_FAILED: nó bật VK_KHR_display, turnip-kgsl từ chối (`I can't KHR_display`) — không phải lỗi GPU
- vkcube segfault trong `wsi_GetSwapchainImagesKHR(swapchain=0)`: tạo swapchain thất bại (sway pixman không có linux-dmabuf) và vkcube không kiểm tra → `MESA_VK_WSI_DEBUG=sw` (present qua wl_shm) → chạy
- A612 không có HW multiview → turnip chỉ báo Vulkan 1.0; Zink cần ≥1.2 → `TU_DEBUG=noconform` (→ 1.3)
- Zink `failed to choose pdev`: EGL đưa major:minor của `renderD128` (SDE) và Zink tìm GPU Vulkan có DRM node khớp → không có. EGL tự rơi về đường swrast; với `GALLIUM_DRIVER=zink` đường đó chạy Zink trên turnip (thông báo lỗi lần thử đầu là vô hại)
- Đo: glmark2-es2 Zink 73–77 vs llvmpipe 34; vkcube ~33 fps (giới hạn bởi copy wl_shm + compositor pixman)

### C48. ✅ Âm thanh: ADSP + card q6 + UCM cho PipeWire
- ADSP không tự boot (giống modem): link `adsp.*` từ apnhlos + giữ `/dev/subsys_adsp` mở → `apr_adsp_up: Q6 is Up` → card `sm6150-idp-snd-card` (WCD937x + 1 WSA881x)
- Đường âm thanh lấy từ vendor `mixer_paths_idp.xml`: loa = MultiMedia1 → `WSA_CDC_DMA_RX_0` → `SpkrLeft`; tai nghe/loa thoại = `RX_CDC_DMA_RX_0`; mic = `TX_CDC_DMA_TX_3` (ADC1 mic chính, ADC2/INP2 mic tai nghe)
- UCM: driver name bị cắt còn `sm6150-idp-snd-` → `ucm2/conf.d/sm6150-idp-snd-/sm6150-idp-snd-card.conf` → `Samsung/a70q/`; `File` trong UCM tính từ gốc ucm2
- **ACP chỉ ra profile "off"**: PipeWire mở PCM để probe TRƯỚC khi bật device; với q6 DPCM, front end chưa nối back end nào → `snd_pcm_hw_params failed: Invalid argument`. Lần đầu chạy được chỉ vì route mình đặt tay còn sót → route MultiMedia1→WSA ở mức verb
- Nhưng back end WSA khi amp tắt làm **treo cả front end chung** (pw-play đứng, speaker-test `I/O error`) → Headphones/Earpiece tắt route WSA khi bật, bật lại khi tắt
- Jack: MBHC downstream chỉ báo qua input device (`SW_HEADPHONE_INSERT`), PipeWire chỉ đọc kcontrol → kernel patch 0008 thêm `Headphone Jack`/`Headset Mic Jack` (iface=CARD; `amixer cget` phải ghi `iface=CARD`)
- Kiểm chứng sau reboot: card + profile tự lên, loa và tai nghe phát OK (người dùng nghe được), mic ghi được (peak 275 im lặng / 32767 khi phát tone)

### C49. DTBO: tắt node wb-display → mất luôn màn hình (lỗi của mình)
- Muốn bỏ connector writeback Virtual-1 nên thêm `status = "disabled"` cho `qcom,wb-display@0` trong 12 overlay → máy kẹt ở logo: không có DRM card nào; component master SDE chờ đủ mọi connector trong `connectors` mà wb không bao giờ probe
- Đúng: giữ node, chỉ bỏ phandle wb khỏi `connectors` của mdss_mdp + sửa offset trong `__fixups__` (sde_dp `connectors:8`→`:4`) và `__local_fixups__` (`<0x4 0xc>`→`<0x8>`) → `pmos/tools/dtbo-drop-wb.sh`; overlay đang dùng: `androidboot.dtbo_idx=10`
- Kết quả: chỉ còn `card0-DSI-1`; workaround `output Virtual-1 disable` của sway không còn cần

### C50. Tự giết nhầm tiến trình của mình (lỗi quy trình, lặp nhiều lần)
- `pkill -f susp.sh` / `pkill -f sshh.exp` / `kill $(pgrep -f Front_Left)` khớp luôn chính shell (hoặc task nền trên Mac) có chuỗi đó trong command line → mất lệnh flash/build đang chạy, phải làm lại
- `ssh ... reboot-mode download` để phiên SSH treo tới khi expect hết giờ (900s) → flash.sh không bắt đầu dù máy đã ở DM
- macOS không có `timeout`
- Quy tắc: kill theo PID cụ thể (`pgrep -x` tên chính xác), lệnh reboot/DM chạy nền phía Mac rồi chờ thiết bị DM bằng ioreg

### C51. Suspend: `lpm_levels.sleep_disabled=1` trong cmdline
- Cmdline LOS có `lpm_levels.sleep_disabled=1`; Android init ghi lại `0` sau boot. pmOS không làm → CPU idle nông, suspend không vào được → `a70q-modem-up` ghi `0`
- Suspend còn bị chặn bởi wakelock khi cắm USB (`usb_notify`, `a600000.ssusb`) và `sxmo_not_suspendable`; `power_supply/usb/online` báo 0 khi sạc PD — dùng `pc_port/online`

