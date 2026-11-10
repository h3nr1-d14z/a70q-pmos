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
