# a70q-pmos — postmarketOS trên Samsung Galaxy A70 (a70q)

postmarketOS (Alpine, OpenRC) chạy trên Galaxy A70 với kernel downstream Samsung/LineageOS 4.14.190
build từ source, rootfs cài vào UFS (phân vùng USERDATA), giao diện Sxmo.

## Trạng thái (2026-11-14)

| Tính năng | Trạng thái | Ghi chú |
|---|---|---|
| Boot pmOS từ UFS | ✅ | SSH USB ~41s, WiFi ~53s |
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
| Phát WiFi (hotspot) | ✅ | menu Sxmo Networks → Create Wifi Hotspot (wlan0 chuyển sang AP, 10.42.0.1); chưa thể vừa phát vừa nối WiFi |
| USB OTG | ✅ | tự chuyển vai host (typec); đã thử chuột, hub, LAN USB (`cdc_ether`); kernel có USB storage, vfat, exFAT |
| GPS | 🟡 | QMI LOC chạy, chưa thấy vệ tinh khi thử trong nhà (`pmos/gnss/README.md`) |
| Camera, vân tay | ❌ | không khả thi trên pmOS thuần (cần HAL Android), xem `pmos/README.md` mục 7 |
| Gọi điện/SMS | ⏸ | hoãn (chưa dùng SIM) |

## Cấu trúc

| Đường dẫn | Nội dung |
|---|---|
| `pmos/README.md` | Hướng dẫn chi tiết: build, flash, quy trình test từng thành phần |
| `pmos/pmaports/device/downstream/` | Package: `device-samsung-a70q`, `linux-samsung-a70q`, `a70q-qcom-userspace`, `mesa-vulkan-freedreno-kgsl` |
| `pmos/kernel/` | Build kernel nhanh bằng Docker + các patch (`patches/`) |
| `pmos/pmb/` | pmbootstrap trong Docker |
| `pmos/tools/` | `mkboot.sh`, `bootimg.py`, `dtboimg.py`, `dtbo-drop-wb.sh`, `flash.sh` (heimdall an toàn trên macOS) |
| `pmos/mesa/` | Build thử turnip-kgsl ngoài pmbootstrap |
| `ERRORS.md` | Sổ lỗi: triệu chứng → nguyên nhân → cách sửa |
| `LOG.md` | Nhật ký theo ngày |
| `backups/` | Backup phân vùng gốc của máy (MANIFEST + sha256) |

## Thiết bị

SM-A705F/DS (XXV), SoC Qualcomm SM6150 (Snapdragon 675), GPU Adreno 612, UFS 128GB, RAM 6GB.
Bootloader đã unlock; vbmeta tắt verify. Kernel từ `a70q-lineage/android_kernel_samsung_sm6150`
nhánh `lineage-22.2` @ 699d9d7da.
