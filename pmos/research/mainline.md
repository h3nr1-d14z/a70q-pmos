# Khả năng chạy kernel mainline trên Galaxy A70 (a70q) — nghiên cứu 2026-11-15

Chỉ tra cứu tài liệu/mã nguồn công khai (kernel master 7.4-rc2), chưa thử trên máy. Mục ⚠️ là chưa kiểm chứng.

## Tại sao quan tâm

Kernel downstream 4.14 là gốc của nhiều giới hạn hiện tại: GPU chỉ qua kgsl (Vulkan turnip, OpenGL phải qua
Zink + chép khung hình → trình duyệt lag, C69), thiếu `openat2` (bubblewrap/sandbox WebKit hỏng, C64),
modem/âm thanh/cảm biến dùng driver Android.

## SoC SM6150 trong mainline: phần lớn đã có (tên "talos")

- `sm6150.dtsi` được đổi tên thành `qcom/talos.dtsi` (SM6150 và QCS615 là cùng một chip)
  ([patch](https://patchew.org/linux/20250923-rename-dts-v1-0-21888b68c781@oss.qualcomm.com/20250923-rename-dts-v1-3-21888b68c781@oss.qualcomm.com));
  Qualcomm vẫn bảo trì tích cực ([lịch sử](https://github.com/torvalds/linux/commits/master/arch/arm64/boot/dts/qcom/talos.dtsi)).
  Board có DTS: chỉ board dev của Qualcomm (`qcs615-ride`, `talos-evk*`).
- Đã có trong `talos.dtsi` ([file](https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/qcom/talos.dtsi)):
  clock GCC/DISPCC/GPUCC/CAMCC/VIDEOCC, interconnect, pinctrl, cpufreq, UFS, USB (DWC3, QUSB2, USB3-DP),
  hiển thị DPU + DSI + DSI PHY 14nm, DP, Venus, camss, ADSP/CDSP, audio GPR/q6apm.
- **GPU A612**: driver msm có cấu hình A612 dùng `a612_rgmu.bin` (RGMU, không phải GMU đầy đủ)
  ([a6xx_catalog.c](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/msm/adreno/a6xx_catalog.c),
  [patch DT](https://patchew.org/linux/20251231-qcs615-spin-2-v6-0-da87debf6883@oss.qualcomm.com)) → freedreno
  OpenGL gốc khả dĩ (⚠️ phía Mesa chưa kiểm tra).
- **Chưa có**: modem (QCS615 là chip IoT không có MPSS) và node WiFi.
- WiFi/BT: `hci_qca` có `qcom,wcn3988-bt`; ath10k loại `wcn3990-wifi`; có chuỗi patch pwrseq WCN399x/WCN3988
  ([ath10k](https://lists.infradead.org/pipermail/ath10k/2026-January/016736.html)) ⚠️ chưa rõ đã merge.
- PMIC `pm6150.dtsi`/`pm6150l.dtsi` đã có (dùng cho SC7180).

## Port có sẵn để tham khảo

- **SM7150** (chip anh em): tổ chức [sm7150-mainline](https://github.com/sm7150-mainline) (kernel fork 7.2, U-Boot,
  pmaports, firmware), máy: sunfish (Pixel 4a), davinci, surya, sweet, toco, tucana; postmarketOS có gói chung
  `qcom-sm7150` boot qua U-Boot ([thông báo](https://postmarketos.org/edge/2025/03/09/sm7150-generic-migration/)).
  Fork dùng `sm7150.dtsi` riêng, không dựa trên talos; `sm7150.dtsi` upstream chưa merge
  ([chuỗi 33 patch](https://patchew.org/linux/20250422-sm7150-upstream-v1-0-bf9a9081631d@jiaxyga.com)).
- **Galaxy A71** (SM7150, gần A70 nhất): có [trang wiki pmOS](https://wiki.postmarketos.org/wiki/Samsung_Galaxy_A71_(samsung-a71)),
  chưa vào pmaports chính thức ⚠️ chưa xem bảng tính năng.
- SM6150 (Redmi Note 7 Pro "violet", Galaxy A60/M40): không thấy port mainline nào.
- Không thấy ai làm mainline cho a70q/a60q/m40.

## Phần riêng của Samsung A70

| Phần | Tình trạng |
|---|---|
| Panel | downstream: `CONFIG_PANEL_S6E3FC2_AMS670TA01_FHD` (1080×2400). Mainline có `panel-samsung-s6e3fc2x01.c` nhưng chỉ cho AMS641RW 1080×2340 của OnePlus 6T ([source](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panel/panel-samsung-s6e3fc2x01.c)) → thêm biến thể, chuỗi lệnh lấy từ driver downstream ⚠️ |
| Cảm ứng | downstream IST40xx; mainline `imagis.c` chỉ có IST3032C/3038… → phải viết driver |
| Boot | Samsung: Odin/heimdall, không fastboot; header boot phải qua anti-rollback (C12). Cách U-Boot của SM7150 với Samsung ⚠️ chưa rõ |
| PMIC camera S2MPB03 | không thấy driver mainline (chỉ cần cho camera) |

## Ước lượng

- **Nhanh (vài ngày – vài tuần)**: DTS a70q dựa trên `talos.dtsi` + PM6150/PM6150L; boot với simplefb (nếu
  bootloader để lại framebuffer), USB gadget/ECM, UFS, cpufreq. Rủi ro chính: đóng gói boot.img cho Samsung, không UART.
- **Vừa**: GPU A612 (driver có sẵn; cần firmware zap/RGMU đã ký lấy từ firmware stock), ADSP/CDSP (firmware
  ký của Samsung), WiFi/BT WCN3988 (firmware + có thể cần quirk), panel (biến thể s6e3fc2x01), pin/sạc PM6150 ⚠️.
- **Khó**: driver cảm ứng IST40xx; modem (thêm remoteproc MPSS + IPA + rmnet); âm thanh (codec WCD937x, tham
  khảo nhánh `audio-wip` của sm7150-mainline); cảm biến (Sensor Core/SSC) coi như không có.

**Kết luận**: phần SoC đã đủ để có GPU freedreno, `openat2` và kernel hiện đại; công sức dồn vào phần riêng của
Samsung (panel, cảm ứng, modem, âm thanh, boot). Hướng đi hợp lý: bắt đầu bằng DTS tối thiểu (simplefb + USB
networking) để kiểm tra đường boot, giữ song song bản downstream đang chạy.
