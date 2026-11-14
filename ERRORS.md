# ERRORS.md — Sổ lỗi & bài học (postmarketOS trên Galaxy A70)

Mục đích: không mắc lại lỗi nào hai lần, và ai tiếp nhận repo sau này không phải debug lại từ đầu.
Format: **Triệu chứng → Nguyên nhân → Cách sửa → Bài học**. Số mục C giữ cố định để tham chiếu.

---

## A. Kiến thức nền về thiết bị và công cụ flash

### C12. Samsung sboot anti-rollback: `os_version` + SPL trong header boot là mức sàn
- **Triệu chứng**: flash boot.img tự đóng gói (heimdall OK) → máy rớt thẳng về Download Mode, không tới kernel
- **Nguyên nhân**: sboot ghi nhận mức sàn anti-rollback từ ROM cuối cùng đã boot được. Boot LOS 22.2 khai `os version 15.0.0` + `os patch level 2026-06` → mọi boot.img khai thấp hơn (cả `os_version`, không riêng SPL) bị bootloader từ chối
- **Cách sửa**: đóng gói bằng `pmos/tools/bootimg.py pack --like <boot LOS>` (copy header, gồm os_version/SPL); `pmos/tools/mkboot.sh` làm sẵn
- **Bài học**: trước khi flash boot tự làm, so mọi trường "mức sàn" với boot gốc (`bootimg.py info`)
- vbmeta: `avbtool make_vbmeta_image --flags 2` (tắt verify); heimdall không có cờ `--vbmeta-disable-verification` của fastboot

### C16. Cmdline trong header boot tối đa 512 byte — bị cắt âm thầm
- mkbootimg/bootimg cắt phần vượt 512 byte mà không báo lỗi → tham số cuối biến mất
- Sau mỗi lần sửa cmdline: kiểm bằng `bootimg.py info` (in độ dài `cmdline length N/511`)

### C20. Kernel Samsung tắt DEVTMPFS
- `a70q_defconfig` gốc: `# CONFIG_DEVTMPFS is not set` (Android dùng ueventd; first-stage init tự mount tmpfs lên /dev, ramdisk LOS có thư mục `dev/` rỗng)
- initramfs Linux chuẩn (pmOS) mount devtmpfs → không có thì /dev rỗng, init chết → bootloop
- **Cách sửa**: `CONFIG_DEVTMPFS=y` + `CONFIG_DEVTMPFS_MOUNT=y` (fragment k1)

### C21. `heimdall detect` làm điện thoại thoát Download Mode
- Mỗi lệnh `heimdall detect` mở rồi đóng interface USB → sboot coi là kết thúc phiên DM và reboot máy
- **Quy tắc**: không bao giờ poll `detect`; chờ DM bằng `ioreg` (macOS) rồi `heimdall flash` một lần (`pmos/tools/flash.sh`)

### C22. Hai tiến trình heimdall cùng lúc trên một DM
- Hai process claim cùng interface → một bị đá khỏi IOService giữa chừng
- **Quy tắc**: một lần flash = một process heimdall; kill watcher/adb server trước

## B. Dựng kernel và initramfs pmOS (2026-11-13)

> Mọi mục đều đã được **tự kiểm chứng** (đọc binary/header/dmesg/ioreg trực tiếp).

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

### Sự thật về boot image và kernel nguồn (đã kiểm chứng)
- **`backups/full/boot.img` = `magisk_patched.img`** (cùng SHA1), không phải boot LOS gốc. Boot LOS gốc: `a70-root/artifacts/boot/lineage-boot-orig.img`. Magisk không vá kernel (kernel hai bản giống từng byte)
- Boot LOS kết thúc bằng **AVB hash footer** (`AVB0`…`AVBf`), không có `SEANDROIDENFORCE`. Boot tự đóng gói không có footer vẫn boot khi flash kèm vbmeta flags=2. vbmeta trong zip LOS 22.2 có flags=3
- **Blob LOS có IKCONFIG** (`/proc/config.gz`); config nhúng = `a70q_defconfig` của a70q-lineage `lineage-22.2` @ `699d9d7da` (trừ `CONFIG_SEC_R1Q_PROJECT` của commit sau đó)
- Blob LOS build bằng clang 19; kernel tự build bằng clang 19 từ cây 4.14.190 đó boot được. Cây 4.14.261 (lineage-18.1) thì chết sớm → chọn đúng cây kernel quan trọng hơn toolchain

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

## C. Boot rootfs pmOS từ userdata (UFS 4K) — 2026-11-14

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

## D. Hiển thị (2026-11-14)

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

## E. Modem + WLAN (2026-11-14) — ✅ WiFi chạy (C43)

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

## F. Giao diện, GPU, âm thanh, suspend (2026-11-14)

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
- WiFi: không có cấu hình WoWLAN thì cfg80211 ngắt kết nối khi suspend (`DEAUTH_LEAVING`) và sự kiện đó huỷ suspend ("Some devices failed to suspend"). `wowlan enable disconnect magic-packet` → firmware đánh thức ngay (`WLAN_CE_2`, icnss `suspend_noirq` -11); chỉ `magic-packet` → ngủ được, WiFi vẫn kết nối sau resume → `a70q-modem-up` đặt WoWLAN magic-packet
- `qcom_rx_wakelock` (gói WiFi vừa nhận, ví dụ chính phiên SSH) chặn suspend vài giây — thử lại sau
- Kết quả: rtcwake 20 s ngủ đủ, dậy bằng `qpnp_rtc_alarm`; tắt màn hình trước rồi suspend cũng OK

### C52. ✅ "Bấm nút nguồn là máy tắt" + màn hình không sáng lại sau khi bấm nguồn
- **elogind** chạy với mặc định `HandlePowerKey=poweroff` và không ai giữ inhibitor `handle-power-key` → mỗi lần bấm nguồn (cả lần đánh thức từ suspend) elogind tắt máy: PMIC báo `Power-off reason: PS_HOLD (MSM Controlled Shutdown)`, Sxmo log "gracefully exiting (on signal)". Cắm sạc thì thấy biểu tượng pin rồi máy tự boot → trông như crash. Suspend không có lỗi
  - Fix: `/etc/elogind/logind.conf.d/50-a70q.conf` (`HandlePowerKey=ignore`, lid/hall `ignore`); nạp nóng bằng `kill -HUP $(cat /run/elogind.pid)`, kiểm bằng D-Bus `org.freedesktop.login1.Manager.HandlePowerKey`
- **Lần bật màn hình đầu tiên sau khi tắt luôn thất bại**: atomic check của SDE từ chối âm thầm (sway: `Backend commit failed`), lần thứ hai OK. Sau lần thất bại, wlroots bỏ object wlr-output-power của output → `wlopm` không liệt kê output nữa và `wlopm --on` không làm gì (trạng thái wlopm báo cũng không tin được)
  - Fix: `/usr/share/a70q/sxmo/default_hooks/wlopm` thay `wlopm` trong PATH của Sxmo: bật/tắt qua `swaymsg output * power`, thử lại tối đa 5 s, đọc trạng thái thật ở `/sys/class/drm/card0-DSI-1/dpms`. Thư mục được đưa vào PATH bằng `XDG_DATA_DIRS` trong deviceprofile (Sxmo dựng PATH từ `$XDG_DATA_DIRS/sxmo/default_hooks` sau khi nạp deviceprofile)
- **Lần bấm nguồn đánh thức máy bị nuốt**: phím được nhấn khi kernel đang resume nên sway không nhận → Sxmo vẫn ở `screenoff`, cần bấm thêm. Fix: hook `sxmo_hook_postwake.sh` riêng đọc `Resume caused by IRQ .., pon_kpdpwr` trong dmesg của lần resume cuối → `sxmo_state.sh set lock`; RTC/WiFi wake vẫn giữ màn hình tắt
- `SXMO_NO_MODEM=1`: modem không qua ModemManager → postwake gốc báo "Modem crashed!" và giữ wakelock 30 s mỗi lần resume
- `~/.cache/sxmo/sxmo.nosuspend` (và `sxmo.noidle`) được Sxmo tạo ở lần đăng nhập đầu khi chưa có deviceprofile → phải xoá để Sxmo tự suspend; cài mới có deviceprofile từ đầu thì không bị
- Kiểm chứng (device r16, rút cáp): bấm nguồn → màn hình tắt → 3 s sau suspend → 28 s sau bấm nguồn → dậy, màn hình sáng sau 0,7 s → bấm lần nữa → mở khoá, cảm ứng chạy
- Quy trình debug khi máy "chết" không để lại log: stream `dmesg -c` về Mac qua SSH; đọc `Power-on/Power-off reason` của qpnp-power-on ở lần boot sau; BusyBox `dmesg` không có `-w`, `grep` không có `--line-buffered`, `cat /dev/kmsg` chạy nền tự thoát (EPIPE)

### C53. ✅ Góc bo + tai thỏ cắt thanh trạng thái Sxmo
- Panel 1080x2400 bo góc r=60px, tai thỏ giọt nước giữa mép trên rộng ~189px sâu ~76px (lấy từ overlay LineageOS của a70q: `rounded_corner_radius_top/bottom` 60px, `config_mainBuiltInDisplayCutout` tính bằng dp, `ro.sf.lcd_density=420` → 2.625px/dp; thanh trạng thái Android 77px). Sway scale 2.5 → góc r=24, tai thỏ x=178..254, sâu ~30.5 (đơn vị logic)
- Thanh bar Sxmo nằm sát mép: ô workspace và đồng hồ rơi vào góc bo
- Fix: `/etc/sway/config.d/50-a70q.conf` (config Sxmo `include /etc/sway/config.d/*` sau khối bar, sửa được bar theo id `bar-0`): `height 31` (bằng độ sâu tai thỏ), `gaps 0 14 0 14` → kiểm bằng `grim -g "0,0 432x48"` + người dùng xác nhận trên máy
- Góc dưới chưa chừa lề (nội dung cửa sổ sát đáy mất vài pixel ở hai góc)

### C54. ✅ sway render bằng GPU (Vulkan trên turnip-kgsl) — xem `pmos/gpu-compositor/README.md`
- wlroots ghép renderer với thiết bị DRM qua GBM / `VK_EXT_physical_device_drm`; ở đây SDE (DRM) không có GPU, kgsl (GPU) không có DRM → renderer Vulkan báo "Could not match drm and vulkan device"
- Bài thử `dmabuf-kgsl-test`: GEM của SDE → dma-buf → import vào turnip-kgsl → GPU ghi, CPU đọc đúng 100% → chia sẻ buffer khả thi
- Patch wlroots `WLR_DISPLAY_ONLY_DRM`: chọn GPU Vulkan không có DRM node, dùng fd DRM của màn hình, allocator dumb buffer của SDE (Vulkan render được vào LINEAR, turnip còn hỗ trợ UBWC)
- Bẫy: binary sway có file capability `cap_sys_nice` → trình nạp musl bỏ qua `LD_LIBRARY_PATH` (thư viện thử không được nạp, log vẫn là đường gốc). Chép binary ra chỗ khác để thử
- Bẫy: `rc-service tinydm stop` chưa giết hết sway cũ → seat bận (`libseat ... Broken pipe`) → chờ đến khi không còn tiến trình sway
- Không có dma-buf sync_file trên 4.14 → wlroots chờ phía CPU (blocking)
- Package `wlroots0.20` r100 + profile chỉ bật Vulkan khi `.so` chứa chuỗi `WLR_DISPLAY_ONLY_DRM` (an toàn nếu Alpine nâng wlroots)

### C55. ✅ Rút tai nghe nhưng vẫn chọn tai nghe (icon tai nghe không mất)
- Kernel báo jack đúng (`Headphone Jack` off) nhưng PipeWire để `availability unknown`: ACP dùng mixer của alsa-lib để theo dõi jack, mà `snd_mixer_load()` của card thất bại (`amixer: Mixer load error`)
- alsa-lib đọc MỌI control MIXER khi nạp, dừng ở lỗi đầu tiên. Driver q6 downstream có ~170 control MIXER thực chất là lệnh chỉ-ghi (ADSP Stream Cmd, Playback Event Ack/ION FD, Audio Effects Config...): không có `get`, hoặc đọc trả EINVAL/EPERM/EAGAIN; `Playback 39 Volume` (loopback) trả ENODEV khi PCM chưa mở. Android dùng tinyalsa nên không bao giờ đọc
- Lỗi đổi dần ENODEV → EINVAL → EPERM qua từng bản vá: tìm bằng quét `amixer cget numid=N` + `strace -e ioctl amixer scontrols`
- Fix kernel: 0009 (loopback volume get trả 0 khi stream đóng), 0010 (ALSA core: control MIXER không đọc được → trả 0 thay vì lỗi). Kernel r18: mixer nạp được, jack `available`/`not available`, rút tai nghe → tự chuyển Speaker, icon mất
- Icon tai nghe trên thanh bar không đổi khi cắm/rút: `sxmo_soundmonitor.sh` của Sxmo chỉ làm mới khi có `'change' on sink` VÀ âm lượng đổi; đổi profile tạo/xoá sink (`'new'`/`'remove'`) → không làm mới. Bản sửa `/usr/share/a70q/sxmo/sxmo_soundmonitor.sh` + unit `/etc/superd/services/sxmo_soundmonitor.service` (superd ưu tiên `/etc/superd/services`, cần `superctl reload`; superd gọi script bằng đường dẫn tuyệt đối nên không thay được qua PATH)
- Tai nghe nhỏ + ù: phần mặc định của vendor `mixer_paths` (HAL Android áp lúc boot) đặt `RX HPH Mode = CLS_AB`, `HPHL/HPHR Volume 17`; UCM thiếu → `CLS_H_INVALID`. Thêm vào EnableSequence của verb → rõ, trái/phải đúng (thử bằng `speaker-test -t wav`; `pw-play` file mono sẽ ra cả hai tai)
- Đính chính một nhận định sai giữa chừng: `WSA_CDC_DMA_RX_0 Channels = 0` là mục `'One'` (enum One/Two), không phải "0 kênh"
- Chưa tái hiện được: một lần sau boot kernel báo `Headphone Jack = on` dù người dùng nói chưa cắm; lần reboot sau (r20, không cắm) báo đúng `off`

### C56. ✅ Đèn pin, rung, độ sáng trong Sxmo; máy không có LED thông báo
- Đèn pin: Sxmo tìm LED `white:flash`/`white:torch`; A70 dùng driver flash Qualcomm: `led:torch_0` (mA, max 500) bật bằng `led:switch_0`. `sxmo_flashtoggle.sh` thay thế (200 mA)
- Rung: `sxmo_vibrate` của Sxmo cần thiết bị input force-feedback; A70 rung qua `/sys/class/leds/vibrator` (`duration` + `activate`) → script thay thế
- Độ sáng: 3 thiết bị backlight; `brightnessctl` mặc định chọn `backlight` (WLED, không nối với panel AMOLED). Đèn nền thật là `panel0-backlight` (max 365; Samsung >255 là HBM). `sxmo_brightness.sh` riêng gọi lại bản gốc với shim `brightnessctl` (`-d panel0-backlight` khi không chỉ định)
- LED thông báo: A70 không có đèn; `red/green/blue` trong sysfs là kênh PMIC không nối → `SXMO_DISABLE_LEDS=1`
- Quyền: udev `61-a70q-feedback.rules` (nhóm `video` ghi được vibrator/torch); các script nằm ở `/usr/share/a70q/sxmo/default_hooks/` (trước `/usr/sbin` trong PATH của Sxmo)
- Bẫy khi thử qua SSH: thiếu PATH của phiên Sxmo → script gốc báo "can't open sxmo_hook_icons.sh"; lấy môi trường từ `/proc/$(pgrep -x superd)/environ`. `command -v X` rỗng làm `grep -v ... $F` đọc stdin và treo

### C57. Xoay màn hình: tay ✅, tự động ❌ (cảm biến sau SSC của ADSP)
- Không có gia tốc kế dạng IIO; cảm biến nằm trong Sensor Core của ADSP. Dịch vụ QMI `400 Snapdragon Sensor Core` có trên QRTR (node 5)
- `iio-sensor-proxy` 3.9 của Alpine có backend SSC (libssc); kích hoạt khi udev gắn `IIO_SENSOR_PROXY_TYPE=ssc-accel ...` cho một thiết bị trong subsystem nó duyệt (`/dev/adsprpc-smd` thuộc subsystem `fastrpc` → không được duyệt; gắn vào platform `62400000.qcom,lpass` thì được)
- libssc kết nối SSC, đăng ký client QMI, nhưng sensor `registry` "unavailable" mãi: framework cảm biến trên ADSP cần HLOS phục vụ sensor registry qua FastRPC (Android: sscrpcd; pmOS mainline: `hexagonrpcd`). `hexagonrpcd` chỉ hỗ trợ driver FastRPC upstream (`/dev/fastrpc-adsp`); downstream có `/dev/adsprpc-smd` với ABI ioctl khác → cần port
- Xoay tay: `sxmo_rotate.sh rotright|rotnormal` (menu Sxmo) chạy, cảm ứng xoay theo đúng
- Đã gỡ udev rule thử nghiệm (chưa đóng gói)

### C58. Modem: dịch vụ QMI có trên QRTR, ModemManager chưa nhận (đang điều tra)
- `qrtr-lookup`: node 0 (modem) công bố WDS(1), DMS(2), NAS(3), QOS(4), WMS(5), VOICE(9), UIM(11), PBM(12)... → điều khiển gọi điện/SMS qua QMI về nguyên tắc khả thi; data cần thêm rmnet/IPA
- **Tạm hoãn** (2026-11-14): người dùng chưa lắp SIM vào máy
- ModemManager 1.25.95 có QRTR (`qrtr devices allowed: yes`) nhưng `mmcli -L`: không có modem. NetworkManager tự kích hoạt MM qua D-Bus ngay khi nó thoát → không chạy được bản `--debug` riêng (thua giành tên `org.freedesktop.ModemManager1`); cần dừng NM hoặc chặn activation khi debug

### C59. ✅ Bluetooth (WCN3988) — xem `pmos/bluetooth/README.md`
- Kernel không có `hci_uart`; `hci_qca` 4.14 chỉ hỗ trợ ROME, DT không có serdev → khởi tạo chip ở userspace (`a70q-btinit`), kernel chỉ cần `BT_HCIUART` + `BT_HCIUART_H4` (r19)
- UART GENI downstream không phát gì (byte nằm mãi trong `TIOCOUTQ`) cho tới khi userspace gọi `TIOCPMGET` (bỏ phiếu clock; HAL Android làm việc này)
- NVM có header bao loại 4 (upstream: "nhiều bộ tag → header loại 4") → đọc tag từ sai chỗ thì không vá được baud → sau khi nạp NVM chip đổi sang 3.2 Mbaud (giá trị gốc) và im lặng
- Tag 17 `data[0]` bit 7 = in-band sleep (upstream bật vì `hci_qca` dùng IBS); với H4 phải xoá, nếu không: `Frame reassembly failed (-84)` mỗi 20 ms, mọi lệnh khởi tạo của kernel `tx timeout`
- Lệnh đầu tiên sau khi bật nguồn có thể mất → thử `HCI_Reset` nhiều lần, chấp nhận cả phản hồi đọc phiên bản
- `btpower` off không reset chip (nguồn dùng chung WLAN) → chip giữ trạng thái giữa các lần chạy; thử nghiệm sai một lần là phải reboot để có chip sạch
- Kết quả: tự lên khi boot, `bluetoothctl power on` OK, quét thấy 33 thiết bị
- Ghép đôi với Mac mini OK (Paired/Bonded/Trusted). Mac phát nhạc sang điện thoại (A2DP sink) bị **giật**: không phải do Bluetooth/UART mà do node loa ALSA xrun ~30 lần/giây với quantum 512 (`pw-top` cột ERR). `node.force-quantum = 2048` cho node của card q6 (rule WirePlumber `51-a70q-alsa.conf`) → 0 lỗi, hết giật
- Đính chính giữa chừng: `ACL packet for unknown connection handle 3804` KHÔNG phải UART hỏng: 3804 = 0xEDC = `QCA_DEBUG_HANDLE` 0x2EDC (log debug của firmware, upstream `hci_qca` lọc riêng) → vô hại. Việc hạ UART xuống 2 Mbaud vì nghi lỗi này là không cần (vẫn giữ 2M, đã kiểm chứng)
- Đổi baud bằng lệnh vendor không bền: `HCI_Reset` (kernel gửi mỗi lần bật adapter) áp lại baud trong NVM → baud vận hành phải bằng baud ghi vào NVM (`a70q-btinit -b`, mặc định 2000000)

### C60. ✅ Chuyển file Bluetooth (OBEX): kernel vendor không build RFCOMM
- Mac báo "the device does not have the necessary services"; Mac (`system_profiler SPBluetoothDataType`) chỉ thấy AVRCP/A2DP/GATT
- `obexd` báo đăng ký profile OPP/FTP thành công, nhưng `bluetoothd`: `RFCOMM server failed for Object Push: socket(STREAM, RFCOMM): Protocol not supported` → không có bản ghi SDP
- Bật `CONFIG_BT_RFCOMM/BNEP/HIDP=y` (r20) KHÔNG đủ: `net/bluetooth/Makefile` của vendor đã comment các dòng `rfcomm/ bnep/ hidp/` (Android dùng stack Bluetooth userspace) → config `=y` nhưng không build gì (không có log "RFCOMM socket layer initialized", không có symbol `rfcomm_` trong `/proc/kallsyms`). Patch 0011 bỏ comment (r21); `rfcomm/tty.c` cũng bị xoá → `CONFIG_BT_RFCOMM_TTY` phải tắt
- Kết quả: OPP/FTP được quảng bá, Mac gửi file sang điện thoại OK (`~/Downloads`). Cũng mở đường cho HFP (tai nghe gọi điện) và PAN
- `obexd` chạy trong phiên Sxmo qua superd (`a70q-obexd`, `-a` tự nhận); cài mới cần `superctl enable a70q-obexd` một lần (trạng thái enable nằm trong `~/.config/services/enabled/`)
- Khi thay đổi dịch vụ, Mac giữ danh sách cũ: Forget rồi ghép lại (phía điện thoại cũng `bluetoothctl remove`)

### C61. ✅ Xoay tự động: hexagonrpcd trên FastRPC downstream qua shim — xem `pmos/sensors/README.md`
- `hexagonrpcd` (ABI upstream) + shim `LD_PRELOAD` dịch ioctl sang `adsprpc` downstream
- Lỗi lần lượt: `kernel session not initialized yet` (thiếu `GETINFO(domain)` sau open) → `EACCES` trên `/dev/adsprpc-smd` (kênh ADSP secure-only → `-secure`) → chạy; ADSP đọc config/registry qua đường hầm ngược, sensor `registry` "available", iio-sensor-proxy thấy gia tốc kế
- Lúc boot: attach ngay khi QMI 400 xuất hiện thì `hexagonrpcd` thoát (kênh FastRPC ADSP mở vài giây sau) → chờ log `opened rpmsg channel for adsp`, thử lại tối đa 5 lần, rồi restart iio-sensor-proxy
- Hướng bị ngược hết: `ACCEL_MOUNT_MATRIX="-1, 0, 0; 0, -1, 0; 0, 0, 1"` trong udev
- `iio-sensor-proxy` 3.9 có sẵn backend libssc; cần tag udev trên thiết bị thuộc subsystem nó duyệt (platform `62400000.qcom,lpass`)
- Tiệm cận làm iio-sensor-proxy abort: `libssc-sensor-proximity.c:98 report_received: code should not be reached` — STK3x3x của Samsung báo `near` = 0/1 và cả **2** (lúc không có gì gần). Vá libssc (package `libssc` r100): 2 = xa, giá trị lạ khác → bỏ qua thay vì abort
- La bàn "crash" trước đó là hệ quả dây chuyền (daemon vừa bị tiệm cận làm sập); sau vá: cả 4 cảm biến (gia tốc, ánh sáng, tiệm cận, la bàn) chạy cùng lúc
- Lại mắc C50 một lần: `pkill -f /usr/libexec/iio-sensor-proxy` giết chính shell đang chạy → dùng `pkill -x iio-sensor-prox` (tên comm bị cắt 15 ký tự)

### C62. Venus (giải mã video phần cứng): firmware sửa được, nhưng client chuẩn không dùng được driver downstream
- **Triệu chứng**: mở `/dev/video32` (`msm_vidc_vdec`) báo "Out of memory"; dmesg `subsys-pil-tz aae0000.qcom,venus: Failed to locate venus.mdt(rc:-2)`
- **Sửa firmware**: symlink `venus.*` từ `apnhlos` (`/vendor/firmware_mnt/image`) vào `/lib/firmware` (`a70q-modem-up`) → venus "Brought out of reset", decoder nhận MPEG-2/H.264/HEVC/VP8/VP9, capture NV12/UBWC
- **Chưa dùng được với ffmpeg/GStreamer/mpv**:
  1. `msm_vidc` không có `VIDIOC_TRY_FMT` (ENOTTY), mà ffmpeg dùng nó để dò thiết bị: "Could not find a valid device". Shim LD_PRELOAD giả lập TRY_FMT qua ENUM_FMT thì vượt qua bước này
  2. Bước tiếp `VIDIOC_REQBUFS` (MMAP) → EINVAL: `vb2_mem_ops` của driver chỉ có `get_userptr`/`put_userptr`; buffer thật là fd ION trong `planes[].reserved[]` (giao thức OMX của Qualcomm). Client V4L2 chuẩn dùng MMAP/DMABUF
- **Kết luận**: muốn dùng phải viết lại quản lý buffer của driver (vb2 dma-contig/dmabuf) hoặc tự viết client kiểu OMX → không đáng: CPU giải mã H.264 1080p 269 fps (9×), HEVC 1080p 212 fps (7×) (`ffmpeg -benchmark`)

### C63. `sxmo_dmenu.sh close` chạy qua SSH giết cả phiên giao diện
- **Triệu chứng**: SSH bị "Connection closed by remote host"; sau đó sway/swaybar không còn, `superd` không lên lại sau `rc-service tinydm restart` (log: "some processes failed to stop after 10s") → phải reboot
- **Nguyên nhân**: `sxmo_dmenu.sh close` = `pkill "$SXMO_MENU"`; trong phiên SSH không có `SXMO_WM`/`SXMO_MENU` → `pkill ""` khớp **mọi** tiến trình của user (sway, superd, pipewire, chính shell SSH)
- **Quy tắc**: từ SSH không gọi `sxmo_dmenu.sh close`; đóng menu bằng `kill $(pgrep -x bemenu)` (tên chính xác). Cùng họ lỗi với C50

### C64. Epiphany/WebKitGTK chết ngay: bubblewrap cần `openat2` (Linux 5.6)
- **Triệu chứng**: `bwrap: Creating new namespace failed…`, `Failed to fully launch dbus-proxy`; `bwrap --ro-bind / / true` → `Can't open source /`
- **Nguyên nhân**: strace: `openat2(…) = -1 ENOSYS`; bubblewrap 0.13 dùng `openat2` không có đường lùi. Kernel cũng thiếu `CONFIG_PID_NS` (`unshare -p` → EINVAL), nhưng bật nó không đủ
- **Cách sửa**: `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` trong `/etc/profile.d/a70q-session.sh` (người dùng đã chọn Epiphany nhẹ thay Firefox; nội dung web chạy không cách ly). Firefox thì chạy được với sandbox seccomp riêng
- **Bẫy**: Epiphany một instance; `epiphany URL` qua D-Bus được `dbus-daemon` khởi động bằng env của nó → biến chỉ có hiệu lực sau khi đăng nhập lại/reboot

### C65. Cấu hình hệ thống bị bỏ qua / apk không nâng cấp
- **dunst** của Alpine build với `SYSCONFDIR=/usr/local/etc/xdg`, chỉ tìm `/etc/xdg/dunst/dunstrc` khi có `XDG_CONFIG_DIRS` (strace `newfstatat`) → đặt `XDG_CONFIG_DIRS=/etc/xdg` trong `a70q-session.sh`. Trước đó dunst chạy cấu hình mặc định build sẵn
- **apk upgrade không lên bản mới**: package từng cài bằng `apk add file.apk` bị ghim trong `/etc/apk/world` (`tên><Q1checksum`) → bỏ phần `><Q1…` rồi `apk upgrade -s` xem trước. Từ khi có kho riêng (`tools/repo.sh`) không cài từ file nữa
- **Sxmo đã có `sxmo_adaptivebrightness.sh`** nhưng không chạy trên máy này (source wrapper `sxmo_brightness.sh` có `exec`, brightnessctl nhầm thiết bị C56, so sánh lux số thực bằng `-lt`) → `a70q-autobrightness` thay vào mục menu "Adaptive Brightness"

### C66. Boot chậm 18 giây: driver NFC `pn547` dò I2C không có chip trả lời
- **Triệu chứng**: `Freeing unused kernel memory` ở giây 20,25; dmesg đầy `i2c_geni a84000.i2c: I2C xfer timeout / Abort` từ 1,6 s đến 19,4 s, kết thúc bằng `pn547_probe: ret(-110) … fail to get i2c addr`
- **Nguyên nhân**: `i2c-0` (a84000) chỉ có `0-0028 nq-nci` và `0-002b pn547`; probe (đồng bộ, chặn initcall) thử lại nhiều vòng timeout. Trên SM-A705F không có chip NFC nào trả lời (vendor có cấu hình NFC nhưng dùng chung cho mọi biến thể)
- **Cách sửa**: `# CONFIG_NFC_PN547 is not set`, `# CONFIG_SEC_NFC_LOGGER is not set` (kernel r22, fragment k3) → kernel vào userspace ở 1,9 s; SSH USB ~16 s, Sxmo ~15 s, WiFi ~31 s
- **Cách tìm**: awk tìm khoảng trống > 0,4 s giữa các dòng dmesg trước khi vào userspace

