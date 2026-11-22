# a70q-pmos — postmarketOS trên Samsung Galaxy A70 (a70q)

postmarketOS (Alpine, OpenRC) chạy trên Galaxy A70 với kernel downstream Samsung/LineageOS 4.14.190
build từ source, rootfs cài vào UFS (phân vùng USERDATA), giao diện Sxmo.

English: [README.en.md](README.en.md), [INSTALL.en.md](INSTALL.en.md).

## Kernel mainline (mặc định từ 2026-11-23)

Máy chạy mặc định **Linux 7.3.4 mainline** (phân vùng RECOVERY); kernel downstream 4.14 vẫn nằm ở BOOT làm đường lùi.
Chọn kernel trên máy: `a70q-boot-default mainline|downstream` rồi khởi động lại. Nếu mainline hỏng (không chạy ổn định
được 90 s) hai lần liên tiếp, máy tự ở lại downstream. Cập nhật kernel mainline qua gói `linux-samsung-a70q-mainline`
(`apk upgrade`). Trên mainline có thêm so với downstream: giải mã video phần cứng (venus), 4 camera qua libcamera
(chưa lấy nét), tai nghe USB-C qua OTG. Chi tiết, cách build và những gì còn thiếu: [pmos/mainline/README.md](pmos/mainline/README.md).
Không bao giờ ghi image mainline vào BOOT (bootloader áp DTBO của downstream → treo ở logo, phải vào Download Mode).

## Trạng thái (2026-11-15)

| Tính năng | Trạng thái | Ghi chú |
|---|---|---|
| Tiết kiệm pin tự động | ✅ | pin ≤ 20% khi đang xả: nhân lớn tối đa 1,2 GHz, độ sáng tối đa 50%, chữ "eco" trên thanh; tắt khi sạc hoặc ≥ 25% |
| Quản lý xung CPU/bus | ✅ | schedutil + bw_hwmon/mem_latency như Android (trước đó luôn xung tối đa, C68) |
| Boot pmOS từ UFS | ✅ | kernel 1,9 s; SSH USB ~16 s, Sxmo ~15 s, WiFi ~31 s (kernel r22, C66) |
| USB networking (ECM) | ✅ | `ssh user@172.16.42.1` |
| Modem DSP + WiFi (WCN3990) | ✅ | NetworkManager, tự kết nối khi boot |
| Màn hình (DRM/KMS) + Sxmo | ✅ | sway render bằng Vulkan (C54); DTBO đã bỏ connector writeback |
| Cảm ứng, phím, nút nguồn | ✅ | |
| GPU Adreno 612 | ✅ | turnip-kgsl (Vulkan 1.3) + Zink (GL/GLES); sway render bằng Vulkan (wlroots có patch) |
| Âm thanh | ✅ | loa, tai nghe (tự chuyển theo jack, kernel r18), mic chính; PipeWire + UCM |
| Suspend + nút nguồn | ✅ | tự tắt màn hình sau 120 s (Config → Auto-screen-off), ngủ khi tắt màn hình kể cả khi bật Bluetooth (C71); nút nguồn đánh thức; WiFi giữ kết nối |
| Đèn pin, rung, độ sáng | ✅ | qua Sxmo (ERRORS C56); máy không có LED thông báo |
| Bluetooth | ✅ | WCN3988, BlueZ; quét/bật OK (ERRORS C59) |
| Xoay tự động, cảm biến ánh sáng/tiệm cận/la bàn | ✅ | qua Sensor Core ADSP + iio-sensor-proxy (ERRORS C61) |
| % pin trên thanh trạng thái | ✅ | `SXMO_BAR_SHOW_BAT_PER=1`; % tính bởi `a70q-qgd` (driver QG cần daemon userspace, C72) |
| Bàn phím ảo | ✅ | wvkbd: vuốt từ mép dưới lên, hoặc Volume Down khi không mở menu |
| NFC | ❌ | không có chip trả lời trên SM-A705F; driver tắt để boot nhanh (C66) |
| Phát WiFi (hotspot) | ✅ | menu Sxmo Networks → Create Wifi Hotspot (wlan0 chuyển sang AP, 10.42.0.1); chưa thể vừa phát vừa nối WiFi |
| Hướng dẫn cử chỉ | ✅ | "Gestures & buttons" trong cài đặt nhanh và System menu: mọi nút cứng, cử chỉ vuốt, góc, thanh trạng thái (tiếng Việt) |
| Cài đặt nhanh | ✅ | vuốt 2 ngón từ mép trên xuống, hoặc chạm vòng tròn trạng thái trên thanh: WiFi, Bluetooth, đèn pin, xoay, độ sáng, tự chỉnh sáng, tiếng Việt, chụp màn hình, nhạc |
| Menu + thanh trạng thái | ✅ | menu rộng, dòng cao dễ chạm; mục **Back** trong menu con; thanh hiện % WiFi, icon Bluetooth khi có kết nối, nhiệt độ CPU, VI/EN; bấm icon trên thanh (loa = Audio, lăn chuột = âm lượng, wifi = Networks, pin = chi tiết, nhiệt độ = CPU/GPU/pin, VI/EN = đổi bộ gõ, giờ = lịch, ○ = cài đặt nhanh); chuột phải lên thanh / phím Menu / Super+P = menu chính (device r32) |
| Tự chỉnh độ sáng | ✅ | cảm biến ánh sáng, giữ mức chỉnh tay; Config → Adaptive Brightness (device r33) |
| Trình duyệt | ✅ | Epiphany (WebKitGTK, giao diện điện thoại), chạy không sandbox vì kernel 4.14 (C64); chặn quảng cáo (EasyList) bật sẵn; thiết lập lưu qua dconf |
| Tai nghe Bluetooth tự kết nối lại | 🟡 | khi đăng nhập và khi máy thức dậy (`a70q-bt-reconnect`); logic đã thử, chưa thử với tai nghe thật |
| Cỡ giao diện | ✅ | Config → Display scale (2,0–3,0), nhớ qua reboot |
| Chụp màn hình | ✅ | bấm đồng thời Nguồn + Volume Down (rung nhẹ), ảnh trong `~/Pictures/Screenshots` |
| Bàn phím ảo tự hiện/ẩn | ✅ | hiện khi chạm vào ô nhập chữ, ẩn khi rời đi (addon Lua của fcitx5, cần `fcitx5-lua`) |
| Gõ tiếng Việt | ✅ | fcitx5 + fcitx5-unikey (Telex) qua input-method-v2 của sway; gõ được từ bàn phím ảo, terminal, Epiphany. Bật/tắt: Ctrl+Space hoặc icon trên thanh |
| Giao diện | ✅ | thông báo dunst cùng kiểu menu; GTK/libadwaita chế độ tối |
| Thẻ nhớ microSD | ✅ | nhận khi lắp nóng (SDXC UHS SDR104), đọc ~36 MB/s, ghi ~51 MB/s; tự mount `/media/<nhãn>` (device r31) |
| USB OTG | ✅ | tự chuyển vai host (typec); đã thử chuột, hub, LAN USB (`cdc_ether`); USB storage cũng tự mount vào `/media/<nhãn>` |
| Giải mã video phần cứng (venus) | ❌ | firmware nạp được, nhưng driver downstream không hợp với ffmpeg/GStreamer (C62); CPU giải mã 1080p nhanh gấp 7–9 lần thời gian thực nên không cần |
| GPS | 🟡 | QMI LOC chạy, chưa thấy vệ tinh khi thử trong nhà (`pmos/gnss/README.md`) |
| Camera, vân tay, FM radio | ❌ | không khả thi trên pmOS thuần (cần HAL Android), xem `pmos/README.md` mục 7 |
| Gọi điện/SMS | ⏸ | hoãn (chưa dùng SIM) |

## Cấu trúc

| Đường dẫn | Nội dung |
|---|---|
| `INSTALL.md` / `INSTALL.en.md` | **Cài từ đầu** (cho người khác): chuẩn bị, build, flash, lần boot đầu (tiếng Việt / tiếng Anh) |
| `pmos/research/` | Ghi chú nghiên cứu (vd. khả năng chạy kernel mainline) |
| `pmos/README.md` | Hướng dẫn chi tiết: build, flash, quy trình test từng thành phần |
| `pmos/pmaports/device/downstream/` | Package: `device-samsung-a70q`, `linux-samsung-a70q`, `a70q-qcom-userspace`, `mesa-vulkan-freedreno-kgsl`, `wlroots0.20`, `libssc` |
| `pmos/kernel/` | Build kernel nhanh bằng Docker + các patch (`patches/`) |
| `pmos/pmb/` | pmbootstrap trong Docker |
| `pmos/tools/` | `prepare-install.sh`, `mkboot.sh`, `bootimg.py`, `dtboimg.py`, `dtbo-drop-wb.sh`, `flash.sh` (heimdall an toàn trên macOS), `repo.sh` (kho package) |
| `pmos/mesa/` | Build thử turnip-kgsl ngoài pmbootstrap |
| `ERRORS.md` | Sổ lỗi: triệu chứng → nguyên nhân → cách sửa |
| `LOG.md` | Nhật ký theo ngày |
| `backups/` | Backup phân vùng gốc của máy (MANIFEST + sha256) |

## Thiết bị

SM-A705F/DS (XXV), SoC Qualcomm SM6150 (Snapdragon 675), GPU Adreno 612, UFS 128GB, RAM 6GB.
Bootloader đã unlock; vbmeta tắt verify. Kernel từ `a70q-lineage/android_kernel_samsung_sm6150`
nhánh `lineage-22.2` @ 699d9d7da.

## Giấy phép

MIT (`LICENSE`) cho script, cấu hình và tài liệu của repo. Patch kernel (`pmos/kernel/patches`,
`linux-samsung-a70q/*.patch`) theo GPL-2.0 như kernel Linux; APKBUILD lấy từ Alpine/postmarketOS giữ giấy
phép gốc của chúng; mã nguồn và firmware tải về khi build giữ giấy phép riêng.
