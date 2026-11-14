# GNSS (GPS) trên a70q — đang làm dở

GNSS nằm trong modem (MPSS). Modem có dịch vụ **QMI LOC (service 16, v2)** trên QRTR node 0.

## Đã chạy
- `a70q-gnss.py [--seconds N]`: một tiến trình (QRTR client gắn với từng tiến trình, nên `qmicli --loc-start`
  rồi `--loc-follow-nmea` ở lệnh khác không dùng được: "Unknown client"). Script đăng ký sự kiện, bật các câu
  NMEA chuẩn (mặc định modem chỉ gửi `$GPRMC` + câu riêng `$PQWP*`), chạy phiên định kỳ 1 s, in NMEA ra stdout
- Cần `py3-gobject3` (typelib `Qmi-1.0`, `Qrtr-1.0` có sẵn từ libqmi/libqrtr-glib); `qmi-utils` cho `qmicli`
- Kết quả (2026-11-14, máy trong nhà): NMEA 1 Hz có giờ UTC, `GGA` fix=0, không có câu `GSV` nào,
  không có indication position/engine-state sau 4 phút

## Chưa rõ / việc tiếp
- Thử ngoài trời ≥ 10 phút để phân biệt "không thấy trời" với "đường RF/LNA GNSS chưa bật"
- Nạp giờ (`qmicli --loc-inject-time`) và XTRA (`xtra3grcej.bin`, nguồn modem khai từ izatcloud.net) để giảm TTFF
- Engine lock đang là `mt` (chỉ chặn phiên do mạng khởi tạo)
- Sau khi có fix: nối vào geoclue (nguồn NMEA qua socket / gnss-share)
