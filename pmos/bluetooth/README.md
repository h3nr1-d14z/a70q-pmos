# Bluetooth trên a70q (WCN3988 qua UART, kernel downstream 4.14)

`hci_qca` của 4.14 chỉ biết chip ROME và device tree kiểu Android không có node serdev, nên việc
khởi tạo chip mà upstream `btqca`/`hci_qca` làm cho WCN399x được làm ở userspace bằng
`a70q-btinit.c` (package `a70q-qcom-userspace`, chạy từ `a70q-modem-up`), rồi giao cho line
discipline HCI UART (H4) của kernel (`CONFIG_BT_HCIUART=y`, `CONFIG_BT_HCIUART_H4=y`, kernel r19).

Trình tự: bật nguồn `/dev/btpower` (ioctl `0xbfad`) → **bỏ phiếu clock cho UART GENI**
(`TIOCPMGET` 0x544D; thiếu bước này dữ liệu ghi nằm mãi trong hàng đợi) → `HCI_Reset` + đọc phiên
bản EDL ở 115200 → lệnh vendor `0xFC48` chuyển 3 Mbaud → nạp `apbtfw11.tlv` rồi `apnv11.bin`
(EDL `0xFC00`/`0x1E`, đoạn 243 byte) → `HCI_Reset` → `TIOCSETD N_HCI` + `HCIUARTSETPROTO H4` → giữ
tty mở (đóng là mất phiếu clock).

NVM (`apnv11.bin`, header bao ngoài loại 4, bảng tag loại 2): tag 17 (HCI) `data[0]` bit 7 = in-band
sleep → **xoá** (H4 không có IBS; nếu để, chip gửi byte wake/sleep mỗi 20 ms → "Frame reassembly
failed (-84)"), `data[2]` = baud vận hành (`-b`, mặc định 2 Mbaud = index 13; 3M = 14 cũng chạy); tag 27 deep sleep để nguyên.
Baud trong NVM phải bằng baud vận hành: `HCI_Reset` áp lại giá trị NVM.

Log `ACL packet for unknown connection handle 3804` = handle debug 0x2EDC của firmware, vô hại.
Nghe nhạc A2DP cần quantum lớn cho card q6 (`51-a70q-alsa.conf`, xem ERRORS C59).

Chip: product 0x0a, rom 0x0201, soc 0x40020140; firmware từ phân vùng vendor (`/vendor/firmware`).
`btpower` "tắt" không cắt nguồn thật (dùng chung với WLAN): chip giữ trạng thái (đã nạp patch,
baud cũ) giữa các lần chạy → tool dò 115200/3M/3.2M và bỏ qua bước nạp nếu đã nạp.

`a70q-btprobe.c`: công cụ thử ban đầu (bật nguồn, gửi HCI thô, in phản hồi).
