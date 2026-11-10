# ubuntu-touch-a70 — Port Ubuntu Touch lên Samsung Galaxy A70 (a70q)

**Trạng thái: kết thúc giai đoạn nghiên cứu + backup. Sẵn sàng vào Phase 1 (môi trường build) trên MÁY MỚI.**

Đây là repo handoff: chứa toàn bộ ghi chú nghiên cứu, quyết định kiến trúc, kế hoạch 7 phase, script backup và dump HAL. Đọc lần lượt:

| File | Nội dung |
|---|---|
| `RESEARCH.md` | Báo cáo nghiên cứu đầy đủ: so sánh OS (UT Noble vs postmarketOS vs Sailfish), ma trận 18 tính năng, các port cùng SoC, quirks Samsung |
| `PLAN.md` | Kế hoạch 7 phase với quyết định đã chốt (noble + halium-11), trạng thái từng mục |
| `LOG.md` | Nhật ký nghiên cứu chi tiết theo vòng — nguồn của mọi con số trong RESEARCH/PLAN |
| `ERRORS.md` | **Sổ lỗi chuẩn hoá**: mọi lỗi đã gặp (symptom → root cause → fix → bài học) — đọc trước khi dựng lại env/build |
| `backups/MANIFEST.md` | Bảng 50 partition đã backup: size + sha256 từng file |
| `backups/hals-lshal.txt` | Dump đầy đủ 209 dòng HIDL HAL đang chạy trên vendor A11 |
| `backups/full-backup.sh` | Script tái tạo toàn bộ backup từ thiết bị (~2 phút) |

## Thiết bị

| Mục | Giá trị |
|---|---|
| Model | SM-A705F/DS, vùng XXV (Việt Nam), serial `[serial removed]` |
| Codename | `a70q` |
| SoC | Qualcomm SM6150 (Snapdragon 675), GPU Adreno 612 |
| Kernel đang chạy | 4.14.190-perf+ (branch lineage-22.2 của `a70q-lineage/android_kernel_samsung_sm6150`) |
| ROM đang cài | LineageOS 22.2 (Android 15), unofficial rtd1250, rooted bằng `su` trong `/system` (userdebug build) |
| Vendor blobs | Android 11 stock Samsung **DXD2** (A705FXXU5DXD2) — **đúng era cho Halium 11** |
| Bootloader | Đã unlock, **Knox 0x1** (đã trip — không ảnh hưởng port) |
| Boot partition | Header v1, page 4096, `SEANDROIDENFORCE` footer, pad 64MB — đã repack thuần thục bằng magiskboot |
| NFC | **CÓ, chip NXP NQx** (`ro.hardware.nfc_nci=nqx.default`) — bảng "A705F = không NFC" trên Nura wiki SAI với biến thể XXV |
| Recovery | Riêng (82,825,216 bytes = đúng size recovery.img LineageOS) |
| Dynamic partitions | KHÔNG có (máy ra đời Android 9) — boot-only layout đơn giản |

## Quyết định kiến trúc đã chốt

1. **Ubuntu Touch noble + Halium 11** (không phải focal/halium-9 của port cũ 2021) — vendor là A11 DXD2 nên halium-11 là đường ngắn nhất; kho `note10lite-criolian/docs` là tài liệu tham khảo khuôn mẫu (cùng Samsung, A11 vendor, noble)
2. **Kernel build dùng branch `lineage-18.1`** của kernel repo (era A11 khớp halium-11), dù máy đang chạy lineage-22.2 — quyết định chi tiết trong LOG.md
3. Cấu trúc sản phẩm: `halium-boot.img` + rootfs UT noble chính thức (tải prebuilt, không build) + adaptation tarball theo cấu trúc GitLab community-ports

## Bug đã xác nhận sẽ fix ở Phase 2 (quan trọng nhất)

Kernel `lineage-22.2` (và phải kiểm tra lại trên `lineage-18.1` khi clone) **comment out RFCOMM/BNEP/HIDP trong `net/bluetooth/Makefile`** — bệnh chung Samsung. Bluez trên UT cần kernel RFCOMM cho HFP (call audio), PAN tethering, HID. Fix = uncomment 3 dòng. `hci_sock.c` đã kiểm tra nguyên vẹn (45KB, đủ bind/ioctl/create). Xác nhận qua note10lite-criolian docs: fix này đã hiệu nghiệm trên máy Samsung cùng kiểu.

## Backup — hiện vật và cách tái tạo

Ảnh partition 612 MB **KHÔNG nằm trong git** (GitHub chặn >100MB + binary tái tạo được). Chúng nằm ở máy Mac gốc: `~/Projects/HieuLD/ubuntu-touch-a70/backups/full/` — 50/50 partition, size-verified 100%, sha256 trong `backups/MANIFEST.md`.

**Trên máy mới, nếu cần lại ảnh**: nối thiết bị qua adb rồi chạy:

```bash
bash backups/full-backup.sh   # chỉnh biến ADB theo transport của bạn; ~110 giây, tự verify size từng partition
```

Lưu ý: backup **bao nhiêu lần cũng được, luôn read-only** (`dd if=... | base64` qua su) — không có rủi ro ghi. Tập tin quan trọng nhất là nhóm radio/EFS (modemst1/2, fsg, fsc, efs, sec_efs, persist) — đã có 2 bản: bản 2026-10-04 trên ổ cắm ngoài của Mac (⚠️ lúc 2026-11-10 ổ này KHÔNG mounted) và bản trong backup full 2026-11-10.

## Nối thiết bị từ máy mới (wireless ADB)

1. Trên điện thoại: Developer options → Wireless debugging →_PAIR DEVICE WITH CODE_ → nhập code hiện trên màn hình:
   ```bash
   adb pair <IP>:<pair-port> <code-6-số>
   ```
2. Port pair chỉ sống trong lúc dialog mở. Sau pair, dò port connect qua mDNS (`adb mdns services` → service `_adb-tls-connect`) rồi `adb connect <ip>:<port>`.
3. Máy đang chạy LineageOS userdebug nên `adb root` được, `su` có sẵn.

## Tiếp tục ở máy mới — Mac mini M4 (Apple Silicon, nhiều ổ đĩa)

**Kiến trúc chốt:** build trong container Linux arm64 (host arm64 = target arm64 → kernel build **native**, không qemu). Mac mini = build machine; Mac cũ = flash/debug host (heimdall đã thuần thục ở đó).

Repo này có sẵn **build kit**: `build/Dockerfile` (ubuntu 20.04 + deps Halium), `build/run.sh` (quản lý container), `build/manifests/samsung_a70q.xml` (local manifest).

Quy trình trên Mac mini:

1. Cài **Docker Desktop** hoặc **OrbStack** (OrbStack nhẹ hơn, khuyến nghị). Kiểm tra dung lượng disk image Docker ≥ 60 GB (Settings → Resources)
2. Clone repo này, rồi:
   ```bash
   ./build/run.sh build   # build image lần đầu (~5 phút)
   ./build/run.sh shell   # vào build shell
   ```
3. Trong shell (source tree nằm trong volume ext4 — **QUY TẮC VÀNG: không bind mount thư mục macOS vào /work**, APFS case-insensitive làm Android build gãy khó debug):
   ```bash
   repo init -u https://github.com/Halium/android -b halium-11.0 --depth=1
   mkdir -p .repo/local_manifests
   cp /handoff/build/manifests/samsung_a70q.xml .repo/local_manifests/
   repo sync --current-branch --no-clone-bundle --no-tags   # ~25–40 GB
   # ./halium/devices/setup a70q  (nếu manifest halium nhận device; xem PLAN Phase 1)
   ```
4. Kernel patches + **fix Bluetooth 3 dòng `net/bluetooth/Makefile`** (kiểm tra lại trên branch lineage-18.1 khi clone) + toolchain era đúng (clang 8/9 — clang 14 gây hang boot theo kinh nghiệm note10lite)
5. `source build/envsetup.sh && breakfast a70q && mka halium-boot` → `halium-boot.img`
6. Copy kết quả ra thư mục host: `./build/run.sh cp /work/out/... out/` (hoặc AirDrop/LAN giữa 2 Mac)
7. Rootfs UT noble **prebuilt tải trực tiếp** (không build) + adaptation tarball theo cấu trúc GitLab community-ports → flash ở Mac cũ qua heimdall (**dtbo/vbmeta phải đi kèm**, AVB fail `Hash of data does not match` nếu thiếu)

**Vendor blobs:** trees `proprietary_vendor_samsung_a70q` + `_sm6150-common` của org a70q-lineage đã chứa blobs extract sẵn trong git — dùng ngay. Muốn blobs khớp đúng firmware máy (DXD2, mới hơn tree) thì chạy `extract-files.sh` từ thiết bị qua adb sau.

**Địa chỉ tham khảo quan trọng**: `note10lite-criolian/docs` (GitHub) — quy trình Samsung halium-11 đầy đủ từng bước; `Halium/hybris-mer-kernel-setup`; `halium-generic-adaptation-build-tools`; thread XDA "CRDIDLPHALI/UBports Ubuntu touch for Galaxy A70" (2021, halium-9 cũ — tham khảo quirks, KHÔNG theo manifest cũ).

## Cảnh cáo đã đúc kết (đừng học lại bằng cái giá này)

- **vbmeta phải flash kèm boot** (hoặc `--disable-verification`) — nếu không boot fails AVB
- **Heimdall: 1 lệnh flash / lần Download Mode** (đã 6 lần flash thành công trên Mac bằng heimdall-apple-silicon v2.2.2)
- Samsung call audio là phần khó nhất của Halium (quirks matrix trong RESEARCH.md) — thẩm định từng bước, đừng ôm nhiều tính năng một lúc
- Không tin `/proc/config.gz` trên máy — verify bằng runtime
- `.gitignore` kernel có pattern `vmlinux*` không anchor — cẩn thận khi commit artifact build
- Đừng dùng `su -c dd` trực tiếp qua adb shell khi pipe lớn — dùng base64 pipe như script mẫu (tránh block/hang PTY)
