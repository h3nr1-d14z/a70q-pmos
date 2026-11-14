# Cảm biến trên a70q (ADSP Sensor Core qua FastRPC downstream)

Gia tốc kế (LSM6DSM), ánh sáng/tiệm cận (STK3x3x), la bàn (AK0991x) nằm sau Sensor Core (SSC) của
ADSP. Linux đọc chúng bằng `iio-sensor-proxy` (backend `libssc`, QMI service 400 qua QRTR). SSC chỉ
báo cảm biến sau khi sensors PD trên ADSP nạp được registry/config từ HLOS qua FastRPC — việc của
`hexagonrpcd`.

`hexagonrpcd` (Alpine) viết cho FastRPC upstream (`/dev/fastrpc-*`); kernel downstream có
`/dev/adsprpc-smd(-secure)` với ABI khác. `fastrpc-downstream-shim.c` là shim `LD_PRELOAD` dịch:
- `INVOKE` → `INVOKE_ATTRS`; `INIT_ATTACH`/`INIT_ATTACH_SNS` → `INIT` cờ 0/3; `MMAP`/`MUNMAP` → số
  hiệu downstream (struct trùng bố cục trên arm64)
- sau `open()` gọi `GETINFO(domain 0)`: driver downstream chỉ gắn file với DSP và cấp session ở
  bước này (libadsprpc của Android làm vậy)
- kênh ADSP là secure-only: node thường bị `EACCES` → dùng `/dev/adsprpc-smd-secure`

Dữ liệu cho `hexagonrpcd -R /var/lib/a70q-sensors`: `sensors/config` + `sensors/sns_reg.conf` từ
`/vendor/etc/sensors`, `sensors/registry` từ phân vùng persist (`sensors/registry/registry`, do Android
sinh, có hiệu chuẩn), `socinfo` từ `/sys/devices/soc0`. Tất cả do `a70q-modem-up` chuẩn bị và chạy.

udev (`80-a70q-ssc.rules`): gắn `IIO_SENSOR_PROXY_TYPE=ssc-*` vào platform device của ADSP (node
fastrpc thuộc subsystem mà iio-sensor-proxy không duyệt) và `ACCEL_MOUNT_MATRIX` đảo X/Y (gia tốc kế
lệch 180° so với màn hình).

Trạng thái: gia tốc kế ✅ (Sxmo Autorotate), ánh sáng ✅ (lux); tiệm cận và la bàn làm
iio-sensor-proxy crash (libssc `g_assert_not_reached` ở báo cáo tiệm cận — giá trị Samsung khác) → đang làm.
