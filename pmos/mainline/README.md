# Kernel mainline cho Galaxy A70 (a70q)

Mục tiêu: thay kernel downstream 4.14 bằng Linux mainline (SoC SM6150 = "talos" upstream) để có GPU
freedreno gốc, `openat2`/sandbox, kernel hiện đại. Khảo sát và nguồn: `../research/mainline.md`.

## Cách thử an toàn: phân vùng RECOVERY

Kernel mainline được nạp vào **RECOVERY**, không đụng BOOT:

- khởi động thường → pmOS downstream như hiện tại (không ảnh hưởng việc dùng máy)
- `sudo reboot-mode recovery` (hoặc tắt máy, giữ Volume Up + Power) → kernel mainline
- mainline ghi console vào **ramoops** cùng vùng và layout với downstream (`0xa1300000`, 1 MB, record/console/
  ftrace/pmsg 0x40000) → khởi động lại vào pmOS, đọc `/sys/fs/pstore/console-ramoops-0`
- máy không có UART; nếu mainline treo cứng: giữ Volume Down + Power ~10 s để khởi động lại

Recovery gốc (LineageOS recovery) đã sao lưu: `backups/full/recovery.img` (sha256 `fc034fd4…`), ghi lại bằng
`dd` hoặc `flash.sh --RECOVERY backups/full/recovery.img`.

## Build

```sh
pmos/mainline/build.sh      # linux 7.3.4 trong Docker (volume a70q-mainline) -> out/Image.gz, out/*.dtb
pmos/mainline/mkboot.sh     # initramfs thử nghiệm + boot image -> out/recovery-mainline.img
```

- `sm6150-samsung-a70q.dts`: `talos.dtsi` + clock board (`sleep_clk` 32 kHz, `xo_board_clk` 38,4 MHz),
  `qcom,msm-id = <355 0>`, `qcom,board-id = <0x22 0xd>` (sboot chọn DTB theo đó), simple-framebuffer tại
  `0x9c000000` (splash của bootloader, 1080×2400 ARGB8888), ramoops, và **toàn bộ vùng nhớ dành riêng lấy từ DT
  downstream đang chạy** (`ref/a70q-downstream.dts` = `dtc -I fs /proc/device-tree`, không commit): Samsung dời
  4 vùng mặc định của `talos.dtsi` (video, cdsp, adsp, gpu — nằm trong vùng modem của Samsung) và thêm hyp, TZ
  (`removed`), uH/RKP/TIMA, sec_debug, qseecom, modem-shared…
- `a70q.config`: arm64 defconfig + pstore/ramoops, simpledrm, fbcon, panic=10
- `init-test`: busybox tĩnh, ghi dòng `A70Q-MAINLINE: ...` vào kmsg và màn hình, 30 s sau reboot

## Các mốc

| Mốc | Nội dung | Trạng thái |
|---|---|---|
| 1 | boot tới initramfs, framebuffer, log qua ramoops | kernel chạy, start_kernel xong; đang gỡ từng driver treo khi probe (TLMM: chân secure 0–3, 6–9, 37–38; cpufreq, smp2p, interconnect, crypto… tạm tắt) — "Nhật ký thử" |
| 2 | regulator PM6150/PM6150L (rpmh), UFS, USB gadget (SSH qua USB) | |
| 3 | GPU A612 (RGMU + zap firmware Samsung), ADSP/CDSP | |
| 4 | panel S6E3FC2 AMS670TA01 (biến thể driver s6e3fc2x01), cảm ứng IST40xx (driver mới) | |
| 5 | WiFi/BT WCN3988, âm thanh, pin/sạc, modem | |

Rủi ro đã biết: sboot áp DTBO (phân vùng DTBO hoặc recovery_dtbo) lên DTB — `mkboot.sh` để `recovery_dtbo`
trống; chưa biết sboot xử lý thế nào khi overlay không khớp DTB mainline. Hypervisor uH/RKP của Samsung chạy ở
EL2: kernel mainline không gọi RKP (các port mainline máy Samsung Snapdragon khác vẫn boot được).

## Nhật ký thử

**Lần 1 (2026-11-15)**: `recovery-mainline.img` (7.3.4, Image.gz + DTB nối, header từ recovery gốc, `recovery_dtbo`
trống) ghi vào RECOVERY bằng `dd`, `reboot-mode recovery`.
- Kết quả: logo Samsung đứng yên. Giữ Volume Down + Power để khởi động lại → **lại vào recovery** (bootloader giữ
  yêu cầu recovery khi kernel không xoá nó) → phải vào Download Mode và ghi `pmos/out/boot-r22.img` (kernel
  downstream đang dùng) vào RECOVERY để thoát vòng lặp. Recovery của LineageOS vẫn ở `backups/full/recovery.img`.
- Sau đó pmOS lên, `/sys/fs/pstore` trống (mất cả log của lần downstream trước) → không có log mainline.
- **Bài học về cách thử**: luôn giữ một image khởi động được trong RECOVERY giữa các lần thử; mỗi lần thử mainline treo
  tốn một vòng Download Mode bằng tay.
- Nghi vấn cần loại trừ lần sau, theo thứ tự: (1) bootloader không nhảy vào kernel — DTB không được chấp nhận (sboot
  chọn DTB theo msm-id/board-id, có thể cần thêm thuộc tính, hoặc việc áp DTBO thất bại) hoặc vị trí nạp kernel
  (mainline `text_offset` = 0, vùng `kaslr_region@80001000` của Samsung nằm ngay đầu RAM); (2) kernel chết trước
  khi ramoops khởi tạo. Cách lấy tín hiệu sớm nhất khi không có UART: ghi thẳng một mảng màu vào framebuffer
  `0x9c000000` từ `head.S` (trước MMU) — thấy màu là kernel đã được nhảy vào.

**Lần 2–5 (2026-11-15)**: gỡ lỗi khi không có UART bằng **đánh dấu trên framebuffer** (`patches/000*-a70q-*.patch`):
- 0001: tô 200 dòng đầu màu xanh lá ngay lệnh đầu tiên của `primary_entry` (MMU tắt) → **thấy xanh lá: bootloader
  đã nhảy vào kernel mainline** (DTB, header, vị trí nạp đều được chấp nhận)
- 0002: một dải màu cho mỗi mốc khởi động (setup_arch, paging_init, mm_core_init, trước rest_init, sau initcall,
  trước init) → dải cuối là **tím hồng**: `start_kernel` xong, treo trong lúc chạy initcall
- 0003: số thứ tự initcall sắp chạy vẽ thành 16 ô (trắng = 1) → `0x76a` = 1898 = `deferred_probe_initcall`
  (`initcalls.sh 1898`, thứ tự theo `System.map`)
- 0004: tên thiết bị + driver vẽ bằng font VGA 8x16 ×3 trước mỗi probe → **`1700000.interconnect` / `qnoc-qcs615`**
  (aggre1_noc): `icc-rpmh` bật clock AXI rồi ghi thanh ghi QoS của NoC → treo. Sửa: bỏ `reg` khỏi 6 NoC có QoS trong
  DTS → driver bỏ qua QoS (`skip_qos_config`), vẫn bỏ phiếu băng thông qua RPMh → **qua được**
- Vào recovery: chỉ `reboot-mode recovery` là chắc chắn (tổ hợp phím có lúc vào boot thường, giữ Volume Up còn làm
  initramfs pmOS vào debug shell). Mỗi lần thử: Download Mode → `flash.sh --RECOVERY <image>` (heimdall khởi động lại
  xoá cờ recovery → pmOS) → `reboot-mode recovery` qua SSH
- Lần 6: dừng ở `3100000.pinctrl` / `qcs615-tlmm` rồi **khởi động lại** (XPU reset khi đọc chân của secure world).
  Sửa: `gpio-reserved-ranges = <0 4>, <6 4>` (SPI vân tay `spi@888000` = gpio0-3, eSE `spi@a88000` = gpio6-9, lấy từ
  DT downstream) → **qua được**
- Lần 7: dừng ở `18323000.cpufreq` / `qcom-cpufreq-hw` rồi khởi động lại. Downstream dùng driver OSM riêng
  (`qcom,clk-cpu-osm-sm6150`). Tạm `status = "disabled"` cho `cpufreq_hw` (CPU giữ xung của bootloader) → qua
- Ghi chú: khi treo kiểu "khởi động lại" thì vòng lặp recovery tự lặp lại (bootloader giữ cờ recovery) → vẫn phải vào
  Download Mode để thoát
- Lần 8: `smp2p-cdsp` / `qcom_smp2p` → khởi động lại. Tắt `smp2p-adsp`/`smp2p-cdsp` (chưa dùng DSP)
- Lần 9: `1dc4000.dma-controller` / `bam-dma-engine` (BAM của crypto engine). Thay vì dò từng cái: tắt 93 thiết bị
  mốc 1 không cần (CoreSight, OSM L3, TRNG, QFPROM, GPU/GMU/GPUCC, BWMON, LLCC, videocc/camcc/dispcc, 2 SMMU, crypto)
- Lần 10: `interconnect@9680000` (gem_noc) dù đã bỏ QoS → khi đăng ký, icc-rpmh bỏ phiếu băng thông tối đa qua RPMh.
  Tắt toàn bộ interconnect (giữ phiếu của bootloader)
- Lần 11: lại `3100000.pinctrl` (các lần trước có lẽ chỉ bị hoãn). Patch 0005: hiện 12 lần probe gần nhất kèm mã trả
  về (`= 0`, `= -517` hoãn, `...` đang chạy) → lần 12: probe thứ 53 là TLMM, đang chạy thì dừng
- Nguyên nhân: kernel downstream của Samsung bỏ qua đúng 3 nhóm chân trong `msm_gpio_is_valid()` (`pinctrl-msm.c`):
  SPI vân tay `CONFIG_SENSORS_FP_SPI_GPIO` 0–3, SPI eSE `CONFIG_ESE_SPI_GPIO` 6–9, **MST Samsung Pay 37–38**
  (`MST_GPIO_D_DATA/EN`). Sửa: `gpio-reserved-ranges = <0 4>, <6 4>, <37 2>` → lần 13: **TLMM `= 0`**
- Lần 13–15: không còn probe nào treo, nhưng máy **tự khởi động lại** ở initcall 1913–1919 (`ip_auto_config`,
  `bpf_tcp_ca_kfunc_init`… — chỗ vặt, thay đổi mỗi lần). Đã thử: `CONFIG_QCOM_WDT=y` (driver tự đặt lại watchdog 30 s
  nếu bootloader để chạy), tắt `CONFIG_IP_PNP` → vẫn reset. Patch 0006: khi panic vẽ ~140 dòng log cuối lên màn hình,
  tắt watchdog, `panic=0 oops=panic softlockup_panic=1` → **không hiện log**: không phải panic của kernel mà là reset từ
  bên ngoài (watchdog/firmware/hypervisor), có vẻ theo thời gian
- Patch 0007 (lần 16, đã build, **chưa thử**): dòng đồng hồ cập nhật 100 ms (hrtimer) ở dòng 1000: `t=` giây từ lúc
  boot + thanh ghi watchdog APSS (EN, STS, BARK, BITE) → biết reset xảy ra lúc nào và watchdog có đang chạy không
