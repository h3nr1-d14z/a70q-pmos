# a70q-pmos — postmarketOS trên Samsung Galaxy A70 (a70q)

postmarketOS (Alpine, OpenRC) chạy trên Galaxy A70 với kernel downstream Samsung/LineageOS 4.14.190
build từ source, rootfs cài vào UFS (phân vùng USERDATA), giao diện Sxmo.

## Trạng thái (2026-11-14)

| Tính năng | Trạng thái | Ghi chú |
|---|---|---|
| Boot pmOS từ UFS | ✅ | kernel 1,9 s; SSH USB ~16 s, Sxmo ~15 s, WiFi ~31 s (kernel r22, C66) |
| USB networking (ECM) | ✅ | `ssh user@172.16.42.1` |
| Modem DSP + WiFi (WCN3990) | ✅ | NetworkManager, tự kết nối khi boot |
| Màn hình (DRM/KMS) + Sxmo | ✅ | sway render pixman; DTBO đã bỏ connector writeback |
| Cảm ứng, phím, nút nguồn | ✅ | |
| GPU Adreno 612 | ✅ | turnip-kgsl (Vulkan 1.3) + Zink (GL/GLES); sway render bằng Vulkan (wlroots có patch) |
| Âm thanh | ✅ | loa, tai nghe (tự chuyển theo jack, kernel r18), mic chính; PipeWire + UCM |
| Suspend + nút nguồn | ✅ | ngủ khi tắt màn hình, nút nguồn đánh thức; WiFi giữ kết nối (ERRORS C51–C52) |
| Đèn pin, rung, độ sáng | ✅ | qua Sxmo (ERRORS C56); máy không có LED thông báo |
| Bluetooth | ✅ | WCN3988, BlueZ; quét/bật OK (ERRORS C59) |
| Xoay tự động, cảm biến ánh sáng/tiệm cận/la bàn | ✅ | qua Sensor Core ADSP + iio-sensor-proxy (ERRORS C61) |
| % pin trên thanh trạng thái | ✅ | `SXMO_BAR_SHOW_BAT_PER=1` (device r29) |
| Bàn phím ảo | ✅ | wvkbd: vuốt từ mép dưới lên, hoặc Volume Down khi không mở menu |
| NFC | ❌ | không có chip trả lời trên SM-A705F; driver tắt để boot nhanh (C66) |
| Phát WiFi (hotspot) | ✅ | menu Sxmo Networks → Create Wifi Hotspot (wlan0 chuyển sang AP, 10.42.0.1); chưa thể vừa phát vừa nối WiFi |
| Menu + thanh trạng thái | ✅ | menu rộng, dòng cao dễ chạm; mục **Back** trong menu con; bấm icon trên thanh (loa = Audio, lăn chuột = âm lượng, wifi = Networks, pin = chi tiết, giờ = lịch, ○ = System); chuột phải lên thanh / phím Menu / Super+P = menu chính (device r32) |
| Tự chỉnh độ sáng | ✅ | cảm biến ánh sáng, giữ mức chỉnh tay; Config → Adaptive Brightness (device r33) |
| Trình duyệt | ✅ | Epiphany (WebKitGTK, giao diện điện thoại), chạy không sandbox vì kernel 4.14 (C64) |
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
| `INSTALL.md` | **Cài từ đầu** (cho người khác): chuẩn bị, build, flash, lần boot đầu |
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
