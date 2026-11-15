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
| 1 | boot tới initramfs, framebuffer, log qua ramoops | image đã build (7.3.4), chưa thử trên máy |
| 2 | regulator PM6150/PM6150L (rpmh), UFS, USB gadget (SSH qua USB) | |
| 3 | GPU A612 (RGMU + zap firmware Samsung), ADSP/CDSP | |
| 4 | panel S6E3FC2 AMS670TA01 (biến thể driver s6e3fc2x01), cảm ứng IST40xx (driver mới) | |
| 5 | WiFi/BT WCN3988, âm thanh, pin/sạc, modem | |

Rủi ro đã biết: sboot áp DTBO (phân vùng DTBO hoặc recovery_dtbo) lên DTB — `mkboot.sh` để `recovery_dtbo`
trống; chưa biết sboot xử lý thế nào khi overlay không khớp DTB mainline. Hypervisor uH/RKP của Samsung chạy ở
EL2: kernel mainline không gọi RKP (các port mainline máy Samsung Snapdragon khác vẫn boot được).
