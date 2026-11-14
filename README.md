# a70q-pmos — postmarketOS trên Samsung Galaxy A70 (a70q)

postmarketOS (Alpine, OpenRC) chạy trên Galaxy A70 với kernel downstream Samsung/LineageOS 4.14.190
build từ source, rootfs cài vào UFS (phân vùng USERDATA), giao diện Sxmo.

Hướng Ubuntu Touch/Halium ban đầu đã dừng (2026-11-13); tài liệu của giai đoạn đó ở
`archive/ubuntu-touch/`.

## Trạng thái (2026-11-14)

| Tính năng | Trạng thái | Ghi chú |
|---|---|---|
| Boot pmOS từ UFS | ✅ | SSH USB ~41s, WiFi ~53s |
| USB networking (ECM) | ✅ | `ssh user@172.16.42.1` |
| Modem DSP + WiFi (WCN3990) | ✅ | NetworkManager, tự kết nối khi boot |
| Màn hình (DRM/KMS) + Sxmo | ✅ | sway render pixman; DTBO đã bỏ connector writeback |
| Cảm ứng, phím, nút nguồn | ✅ | |
| GPU Adreno 612 | ✅ cho app | turnip-kgsl (Vulkan 1.3) + Zink (GL/GLES); compositor vẫn CPU |
| Âm thanh | ✅ | loa, tai nghe (tự chuyển theo jack), mic chính; PipeWire + UCM |
| Suspend | ⏳ | đang thử (ERRORS C51) |
| Gọi điện/SMS, camera, Bluetooth, cảm biến | ❌ | chưa làm |

## Cấu trúc

| Đường dẫn | Nội dung |
|---|---|
| `pmos/README.md` | Hướng dẫn chi tiết: build, flash, quy trình test từng thành phần |
| `pmos/pmaports/device/downstream/` | Package: `device-samsung-a70q`, `linux-samsung-a70q`, `a70q-qcom-userspace`, `mesa-vulkan-freedreno-kgsl` |
| `pmos/kernel/` | Build kernel nhanh bằng Docker + các patch (`patches/`) |
| `pmos/pmb/` | pmbootstrap trong Docker |
| `pmos/tools/` | `bootimg.py`, `dtboimg.py`, `dtbo-drop-wb.sh`, `flash.sh` (heimdall an toàn trên macOS) |
| `pmos/userspace/`, `pmos/mesa/` | Mã nguồn tool riêng (BDF loader WLAN), build thử turnip |
| `ERRORS.md` | Sổ lỗi: triệu chứng → nguyên nhân → cách sửa (mục F trở đi là pmOS) |
| `LOG.md` | Nhật ký theo ngày |
| `backups/` | Backup phân vùng gốc của máy (MANIFEST + sha256) |

## Thiết bị

SM-A705F/DS (XXV), SoC Qualcomm SM6150 (Snapdragon 675), GPU Adreno 612, UFS 128GB, RAM 6GB.
Bootloader đã unlock; vbmeta tắt verify. Kernel từ `a70q-lineage/android_kernel_samsung_sm6150`
nhánh `lineage-22.2` @ 699d9d7da.
