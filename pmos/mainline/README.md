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

## pmOS trên kernel mainline (từ 2026-11-16)

`ROOTFS=1 pmos/mainline/mkboot.sh` → initramfs dựng USB gadget rồi `switch_root` vào pmOS đã cài (userdata =
image đĩa GPT **sector 4096 byte**; loop đọc sector 512 nên không thấy phân vùng → map p1/p2 bằng offset
`fdisk -b 4096`, loop1 = pmOS_root, loop2 = pmOS_boot). Module mainline cài vào rootfs: `MODULES=1 build.sh` →
`out/modules.tar.gz` → `/lib/modules/7.3.4-a70q` (kernel downstream không dùng thư mục module nào). Một tiến trình
giữ sẵn fd `/proc/sysrq-trigger` reset máy sau 15 phút (lưới an toàn).

Kết quả: OpenRC, sshd (SSH qua USB như bình thường), NetworkManager, elogind, seatd chạy; tinydm → Sxmo/sway hiện
giao diện qua simpledrm (`screenshot-sxmo-mainline.png`). Device package r51: `a70q-wlroots.sh` chọn pixman khi
không có `/sys/class/kgsl`, `WLR_LIBINPUT_NO_DEVICES=1` (chưa có cảm ứng/phím); deviceprofile cho
`samsung,a70q`. Hỏng (chưa có phần cứng): a70q-qgd, bluetooth, modemmanager, iio-sensor-proxy; không RTC (1970).

## Thử tự động, không cần Download Mode (từ 2026-11-16)

```sh
SSHPW=... pmos/mainline/remote-test.sh   # pmOS đang chạy, USB hoặc HOST=<IP WiFi>
```

Script chép `out/recovery-mainline.img` qua SSH, `dd` vào RECOVERY (kiểm sha256), `reboot-mode recovery`, chờ pmOS
quay lại rồi lấy `/sys/fs/pstore`. Kernel mainline tự đưa máy về khởi động thường (patch 0010/0011): ghi IMEM
`restart_reason` (`0x146aa65c`) = 0 và PMIC `SOFT_RB_SPARE` (`0x88f` bit 7:1) = `NORMALBOOT` (0x14) — đúng giá trị
`sec_debug_update_restart_reason()` của Samsung ghi khi reboot thường (cờ recovery "dính" trước đây là do khởi động lại
bằng phím, không ai ghi lại hai ô này) — và bật watchdog APSS (bite 60 s) để treo cứng cũng tự reset. Panic → reboot
sau 10 s. Đã chạy 5 vòng liên tiếp không đụng tay (~55 s/vòng).

**Log của mainline (từ 2026-11-16):** pstore không dùng được trên máy này — DRAM giữ nội dung qua reset nhưng bị lật
bit rải rác (cold reset; `memprobe.py mark/check` cho thấy chuỗi 16 byte còn nhưng sai 1–2 bit), header ramoops lệch
là bản ghi bị bỏ, kể cả với kernel downstream. Patch 0014 ghi 3 bản sao log kernel (`kmsg_dump_get_buffer`) vào đuôi
vùng splash `0x9ce00000` (3 × 320 KiB, sau framebuffer 1080×2400), mỗi giây và khi panic/restart; pmOS đọc bằng
`/dev/mem` (kernel downstream r23 bật `CONFIG_DEVMEM`) và bầu đa số theo từng bit: `memprobe.py log3`.
`remote-test.sh` tự lấy về `out/pstore-*/mainline.log`. Vẫn còn sót vài ký tự sai (~1% bit lật).

Lịch sử — chưa lấy được log qua pstore: sau mỗi vòng `/sys/fs/pstore` rỗng, PON ghi "Hard Reset / cold boot" dù đã thử đặt
PS_HOLD = warm reset (patch 0012) và `reboot=warm`; init ghi dmesg vào `/dev/pmsg0` cũng không thấy. pmsg của chính
pmOS thì vẫn sống qua reboot → nghi ramoops chưa hoạt động trong mainline. Kênh log tiếp theo: USB gadget (mốc 2).

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
| 1 | boot tới initramfs, framebuffer, log qua ramoops | ✅ 2026-11-16 (lần 22): tới `/init`, log hiện trên màn hình. Cần: TLMM bỏ chân secure 0–3/6–9/37–38, không PSCI cpuidle; nhiều thiết bị đang tạm tắt — "Nhật ký thử" |
| 2 | regulator PM6150/PM6150L (rpmh), UFS, USB gadget (SSH qua USB) | ✅ USB gadget 2026-11-16: ECM + telnet `172.16.42.1:23` (`mlsh '<lệnh>'`), 8 CPU, 5,2 GB RAM. Cần: `apps_rsc` bỏ `power-domains` (chờ PSCI domain mãi → rpmhcc/GCC/regulator kẹt), LDO L4A/L11A/L17A cho QUSB2, `qcom,select-utmi-as-pipe-clk` (không PHY USB3 → ep0 timeout), QFPROM bật lại. ✅ UFS 2026-11-16: KM2V7001CM 128 GB, 64 phân vùng (vcc L11C, vccq2 L12A, PHY L4A/L11A); cần bật lại `apps_smmu` (không có thì NOP OUT timeout — DMA của stream 0x300 bị chặn) |
| 3 | GPU A612 (RGMU + zap firmware Samsung), ADSP/CDSP | ✅ **GPU 2026-11-18**: driver msm gốc của 7.3.4 (A612 chạy như GMU wrapper, CPU tự bật GX; **không** cần bản vá RGMU của violet, đã chuyển sang `patches-off/`), SQE + `a612_rgmu.bin` + zap Samsung (`qcom/a612_zap.mdt`, symlink từ apnhlos). Nguyên nhân CP không chạy: OPP 845 MHz (CX turbo) vượt speed bin của chip (downstream tối đa 745 MHz) → GPU chết hẳn (thanh ghi CP đọc 0). Xoá `opp-845000000` → Mesa `FD612`, glmark2-es2-drm offscreen 252 điểm, devfreq 435–745 MHz. Compositor vẫn pixman (hiển thị qua simpledrm). ADSP/CDSP: chưa |
| 4 | panel S6E3FC2 AMS670TA01 (biến thể driver s6e3fc2x01), cảm ứng IST40xx (driver mới) | cảm ứng: driver mới `ist40xx` (patch 0016) trên i2c1 (QUP0 SE1, cần `gpi_dma0`), 0x50, IRQ GPIO 89, nguồn GPIO 93 + L18A. Trình tự như downstream: tắt 30 ms/bật 60 ms, `rI2C_CTRL`=0x003912D8 (I2C 32 bit — thiếu thì firmware không chạy), `eHCOM_FW_START`; TOUCH_STATUS 0x00800075 = firmware chạy. Định dạng thật (đọc từ chip, khác suy luận từ code downstream): thông điệp ngắt không có chữ ký 0x77 (vd 0x00001800, số sự kiện = bit 15:12); word 0 = id sự kiện 31:30, touch id 29:26, trạng thái 25:24 (1 chạm, 2 nhấc), X 23:12, Y 11:0. ✅ 2026-11-17: dùng được trong Sxmo (chạm đúng vị trí cả góc phải, vuốt mở menu). dispcc phải tắt (probe làm tối màn hình simpledrm). Panel: simpledrm |
| 5 | WiFi/BT WCN3988, âm thanh, pin/sạc, modem | ✅ **modem + WiFi 2026-11-18**: MPSS (PAS 4, firmware Samsung) + WCN3990 qua ath10k_snoc, kết nối WiFi nhà, DHCP, ping Internet; tự chạy lúc boot (device r54 `a70q-mainline-modem`: mount apnhlos/modem, symlink `wlanmdsp.mbn`/zap, rmtfs + tqftpserv + pd-mapper với thư mục firmware, `echo start` remoteproc). Mấu chốt: `QCOM_TZMEM_MODE_GENERIC` (SHM bridge làm CPU kẹt trong secure world khi assign), rmtfs `qcom,vmid` ở 0xf4a01000, patch 0019 bỏ quiet mode (firmware HL3.0 crash), patch violet 1015 tạo BSS peer trước khi start vdev (thiếu thì `vdev-start-response status -1`), không quirk host-cap. ✅ **Bluetooth 2026-11-18**: WCN3988 trên `uart7` (QUP1 SE3 = ttyHS0 downstream, alias `serial1` bắt buộc), `qcom,wcn3988-bt`, nguồn L1C/L1C/L2C/L10C, firmware `qca/apbtfw11.tlv` + `apnv11.bin`; NVM không có địa chỉ → controller "unconfigured": device r56 lưu địa chỉ của downstream vào `/var/lib/a70q/bt-addr` (không commit), mainline đặt bằng `btmgmt public-addr`. Âm thanh, pin: chưa |

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
- Patch 0007 (lần 16): dòng đồng hồ cập nhật 100 ms (hrtimer) ở dòng 1000: `t=` giây từ lúc
  boot + thanh ghi watchdog APSS (EN, STS, BARK, BITE) → biết reset xảy ra lúc nào và watchdog có đang chạy không
- Lần 16: reset ở **t=7.5 s**, watchdog APSS tắt (`EN=0`) → không phải nó. Patch 0008 đọc lý do reset trước từ PON của
  PM6150 qua SPMI (node `pmic@0` không con): `PON 01` (hard reset), `POFF 02` = **PS_HOLD** (SoC chủ động reset),
  FAULT2 bit 4 OTST3 (cờ cũ). Máy không nóng → không phải nhiệt. Lần 18: patch 0009 in log lên màn hình cả khi kernel tự
  restart/poweroff (giữ 30 s) → không thấy → kernel không tự reset. Lần 19: thêm trang `kaslr_region@80001000` → không đổi
- Lần 20: chờ 15 s ở `arch_initcall` → reset ở **t=22.5** → không phải hẹn giờ mà theo **bước khởi động**, ngay sau probe
  của `power-domain-cpu0..7` (cpuidle-psci-domain, chuyển PSCI sang OSI mode). Lần 21: `cpuidle.off=1` → vẫn reset
- **Lần 22: bỏ `CONFIG_ARM_PSCI_CPUIDLE` + `CONFIG_ARM_PSCI_CPUIDLE_DOMAIN` → qua: dải đỏ (initcall xong), dải trắng
  (chạy `/init`), log trên màn hình có `initramfs reached` / `a70q mainline: reboot in 30s` → MỐC 1 ĐẠT** (2026-11-16)
- Thủ phạm thật của các lần "khởi động lại" từ lần 13: PSCI domain/OSI. Một số thiết bị tắt ở lần 7–11 (cpufreq,
  smp2p, crypto BAM, interconnect…) có thể không hỏng thật — sẽ bật lại từng cái ở mốc 2 để kiểm tra
- `A70Q-MAINLINE` không hiện trên `tty0`: chưa có fbcon trên framebuffer (simpledrm chưa gắn / chưa có console chữ)
- **Quy tắc an toàn (2026-11-17)**: thử phần cứng mới ở chế độ initramfs có telnet trước (tự gỡ được vòng lặp: UFS chạy
  trên mainline → `dd` kernel pmOS vào RECOVERY), chỉ sau đó mới thử với `ROOTFS=1`. Một bản sửa sai (bỏ phiếu
  `gfx.lvl`) làm pmOS-trên-mainline treo trước sshd và máy lặp vào RECOVERY → phải vào Download Mode bằng tay.

## Bản vá lấy từ dự án violet (Redmi Note 7 Pro, SM6150)

`patches/1xxx-violet-*.patch` lấy từ https://github.com/VipinVIP/violet-sm6150-mainline (GPL-2.0, cùng SoC, gặp đúng
các vấn đề này độc lập với mình): 1003 ACV interconnect, 1004 RGMU A612, 1005 SPMI stale IRQ, 1007 dispcc GDSC/XO,
1008 DSI PHY vcca, 1012 GLINK early intents, **1015 ath10k BSS peer trước vdev start (WiFi)**, 1017 SMMU hyp CB. Trùng với
bản của mình nên không lấy: 0002 (tzmem), 0010 (quiet mode), 0013 (rpmhpd MSS); 1011 (pd-mapper trong kernel) không áp được.
Từ DTS violet: `&gcc { power-domains = <&rpmhpd RPMHPD_CX>; }` (GPUCC thiếu phiếu CX). GPU vẫn chưa chạy (RGMU GPUSTART timeout).


### GPU: ghi chú gỡ lỗi (2026-11-18)
- Bản vá debug tạm (dump thanh ghi khi CP_INIT treo) cho thấy: GX GDSC bật, clock gfx3d bật, nhưng mọi thanh ghi CP đọc lại 0 → GPU chạy quá tần số/điện áp cho phép.
- PLL0 GPU không hỏng (bật & lock ở 1020/1300/1490 MHz). Thử lần lượt 435 → 650 → 745 MHz đều chạy; chỉ 845 hỏng.
- Speed bin downstream: `qcom,gpu-speed-bin = <0x6004 0x1fe00000 21>`; kgsl trên máy này báo `gpu_available_frequencies` = 745/700/550/435/290 MHz.
- "GPUSTART thành công" trước đây với bản vá RGMU là đọc nhầm bit cũ: RGMU chưa từng bật GX trên máy này.

### Màn hình thật + pin (2026-11-18)
- ✅ **Màn hình**: DPU → DSI0 (4 lane, command mode, TE GPIO 90) → panel S6E3FC2 AMS670TA01, driver mới
  `panel-samsung-s6e3fc2-ams670ta01.c` (patch 0021; chuỗi lệnh on/off lấy từ `qcom,mdss-dsi-on-command`
  của downstream, độ sáng DCS 0x51 10 bit). Nguồn: vddio L13A, enable = PM6150L GPIO 6, reset GPIO 91,
  DSI L3C, PHY L4A; dispcc bật lại (patch violet 1007). Đọc ngược trạng thái panel qua
  `/sys/bus/mipi-dsi/devices/ae94000.dsi.0/panel_state` (power_mode 9f = sleep out + display on).
  Tắt/bật màn hình (sway `output * power off/on`) chạy đúng. Cảnh báo `disp_cc_mdss_pclk0_clk_src: rcg didn't
  update` lúc khởi động là vô hại (PLL DSI chưa bật khi đặt rate).
- ✅ **Compositor dùng GPU**: device r58/r59 — sway `WLR_RENDERER=gles2`, `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128`
  (render trên msm GPU, hiển thị trên msm DPU); client Wayland dùng `FD612`. Script backlight dò tên thiết bị
  (`panel0-backlight` trên downstream, `ae94000.dsi.0` trên mainline).
- ✅ **Pin**: `a70q-battery.c` (patch 0022) chuyển từ `violet-vbat.c`: đọc điện áp/dòng của QG PM6150,
  đếm coulomb, bảng OCV 25°C và FCC 4238 mAh từ profile downstream `a70q_4400mAh`, lưu % vào SDAM.
  SMB5 sạc theo mặc định phần cứng (không có driver sạc mainline). Chưa kiểm chứng khi xả pin.

### Âm thanh (2026-11-18, đang làm)
- ✅ ADSP chạy (firmware `adsp.mdt` từ apnhlos, symlink `qcom/a70q/`, khởi động bởi `a70q-mainline-modem`),
  APR cũ (`apr_audio_svc`, không phải AudioReach của talos.dtsi): q6core/q6afe/q6asm/q6adm probe, PDR
  `msm/adsp/audio_pd` lên nhờ pd-mapper.
- Phần cứng (DT downstream): bolero (va/tx/rx/wsa macro @0x62f20000/0x62ec0000/0x62ee0000/0x62f00000),
  loa = 1 WSA881x (SoundWire 0x21170213 = id 3, swr0 @0x62f10000 SPI 136, SD_N GPIO 47, chân swr GPIO 110/111),
  tai nghe/mic = WCD937x (swr1/swr2, reset = **LPI** GPIO 24 → cần driver pinctrl LPASS LPI cho SM6150).
- ❌ Chặn: ADSP trả `ADSP_EBADPARAM` cho mọi `AFE_PARAM_ID_CLOCK_SET` của clock codec (VA 0x30b, WSA 0x309,
  19.2 MHz, attri 1 — giống hệt downstream), cả lệnh SVC v1 (0x100f3) lẫn v2 (0x100fc, `patches-off/0024`), có hoặc
  không có vote `LPASS_HW_MACRO` (block 3 vote OK; block DCODEC 4 → `ADSP_EUNSUPPORTED`). Macro đọc thanh ghi khi
  chưa có clock → ADSP báo "AHB Bus hang addr 0x62f00008" và crash; q6afe-clocks re-probe lúc phục hồi → oops.
  Các node codec/sound để `status = "disabled"` (`a70q-audio-step`).
- `a70q.log3head` (patch 0023): giữ phần đầu log thay vì phần cuối (bắt lỗi lúc boot khi log bị spam).

### Modem ổn định (2026-11-18)
- ✅ Modem từng crash "fatal error without message" đúng 40 s sau mỗi lần lên (WiFi rớt theo chu kỳ). Nguyên nhân:
  thiếu IPA phía AP. Thêm cặp smp2p `ipa` (ipa-ap-to-modem / ipa-modem-to-ap) trong `smp2p-modem` và node IPA v4.2
  (`qcom,sc7180-ipa`, SPI 311/432). Lúc đầu dùng `modem-init` như violet: lỗi vẫn lúc có lúc không, vì modem không
  bao giờ gửi `ipa-setup-ready` nên AP không dựng IPA/QMI. Đúng như downstream (PIL `ipa_fws`, PAS 15): AP tự nạp
  firmware GSI — `qcom,gsi-loader = "self"`, `memory-region = <&ips_fw_mem>` (0x9a900000), firmware
  `qcom/a70q/ipa_fws.mdt` (chép từ apnhlos vào rootfs bởi `a70q-mainline-modem`, device r61, vì IPA nạp lúc probe).
  "IPA driver setup completed successfully", có `rmnet_ipa0`, modem không còn crash.

### Sửa chân enable panel, rung, thẻ SD (2026-11-18)
- ⚠️ **Lỗi đã sửa**: panel từng khai báo `enable-gpios = PM6150L GPIO 6`, nhưng chân đó là ngõ ra PWM của motor rung
  → mainline lái nó lên mức cao = motor có thể rung liên tục (cấu hình GPIO PMIC còn giữ qua reboot mềm).
  `platform-en-gpio` downstream là PM6150 GPIO 6, và downstream để nó là input pull-up (lái nó làm panel hỏng),
  nên panel giờ **không** dùng enable GPIO.
- ✅ **Rung**: `pwm-vibrator` trên kênh LPG 3 (0xbc00) của PM6150L, chân PM6150L GPIO 6 ở `func1` (như
  `samsung_vib` downstream, chu kỳ 1 ms). Kiểm chứng ở mức PWM (bật 100% khi phát hiệu ứng FF rumble).
- ✅ **Thẻ microSD**: VDD của thẻ qua công tắc tải bật bằng TLMM GPIO 121 (Samsung `sec,tflash-en-gpio`) sau L9C
  (regulator-fixed `sd_vdd`); thiếu nó thẻ vẫn trả lời nhưng ACMD41 không bao giờ xong ("Card stuck being busy").
  L9C phải 2.952 V (2.95 V lệch lưới 8 mV làm hỏng cả regulators-1 → UFS/DSI defer). Card detect GPIO 99
  active-high, vqmmc L6C. Kết quả: UHS-I SDR104, đọc ~90 MB/s, tự mount.

### Âm thanh: tiến triển và lý do tạm tắt (2026-11-19)
- Đọc log APR của downstream (`/sys/kernel/debug/msm_apr_debug` = 1, ipc_logging `apr`): downstream đặt clock codec
  bằng `AFE_SVC_CMD_SET_PARAM` v1 (0x100F3) và được chấp nhận → định dạng lệnh mainline đúng.
- Thử từng ID clock lúc probe (patch tạm `patches-off/0027`): ADSP nhận mọi clock codec 19.2 MHz **trừ VA core 0x30b**.
  VA macro được cấp clock TX core (như downstream: bolero `va-without-decimation`, MCLK lấy từ TX) → macro probe
  không còn treo bus, sound card "Samsung Galaxy A70" đăng ký.
- SoundWire master của WSA là **v1.5.0** (`SWRM_COMP_HW_VERSION`), 6 dout + 3 din. SD_N loa = **GPIO 108** (không phải 47).
  WSA881x nằm ở device 0 nhưng enumerator phần cứng không bắt được ID (bus clash) → UNATTACHED. Downstream dùng
  driver `swr-wcd` cũ với liệt kê bằng phần mềm. Khung 48×2 (`patches-off/0028`) không giúp.
- **Bật các macro + SoundWire làm modem assert mỗi 40 s** (chia đôi qua nhiều lần boot). Vì vậy macro, swr0 và sound
  card đang tắt; ADSP + APR vẫn chạy. Thẻ SD chạy ở high-speed (`no-1-8-v`): chế độ UHS (L6C 1.8 V) cũng làm
  modem crash. ADSP được khởi động trước modem (device r62).

### Modem qua ModemManager, GPS, suspend (2026-11-19)
- ✅ ModemManager nhận modem qua QRTR (`qrtr0` + `rmnet_ipa0`, firmware MPSS.AT.4.3.c3). Máy hiện không lắp SIM
  (downstream cũng báo `no-atr-received` ở cả hai khe) nên trạng thái `sim-missing`.
- ✅ GNSS: `qmicli -p -d qrtr://0 --loc-start --client-no-release-cid` rồi `--client-cid=N --loc-follow-nmea` → NMEA
  chạy (trong nhà chưa có vệ tinh/fix; cần thử ngoài trời).
- ❌ Suspend (s2idle, `rtcwake -m freeze`): máy không tỉnh lại. Bị nhiễu bởi watchdog APSS do patch thử nghiệm
  (0010–0012) bật (cắn sau 60 s, không được vỗ khi đang ngủ) → cần tắt watchdog đó khi suspend rồi thử lại.
- Đã xoá `/etc/modprobe.d/a70q-ath10k-debug.conf` (log debug ath10k làm tràn log3).
- ⚠️ Lần thử suspend thứ ba (`echo freeze > /sys/power/state`, lúc chưa có `pm_test`) làm máy **kẹt hẳn**: không USB,
  không WiFi, watchdog không reset được → cần giữ Nguồn + Giảm âm lượng ~10 s. Không thử suspend thật nữa khi chưa
  có đường đánh thức chắc chắn. Đã bật `CONFIG_PM_DEBUG`/`PM_SLEEP_DEBUG` để dùng `pm_test=devices` (chưa chạy được).

### Suspend chạy (2026-11-19)
- ✅ s2idle thật (`rtcwake -m freeze -s 15`): vào suspend và RTC đánh thức. `pm_test` freezer/devices/platform đều qua.
- Lỗi cũ "máy không tỉnh lại" thực ra là **USB gadget chết sau resume** ("failed to enable ep0out"): GDSC USB tắt lúc
  ngủ làm mất cấu hình chọn pipe clock UTMI trong QSCRATCH. Patch 0029: `dwc3_qcom_resume()` ghi lại
  `qcom,select-utmi-as-pipe-clk`. Giờ SSH qua USB, WiFi, panel đều sống sau resume.
- (Lần máy kẹt hẳn trước đó nhiều khả năng cũng là USB chết, và máy vào lại suspend vì không có gì giữ nó thức.)
- Còn lại: Bluetooth đôi khi timeout rồi tự reset sau resume (khi đó mất địa chỉ BT đã đặt); WiFi báo timeout khi gỡ
  key lúc suspend (vô hại, kết nối lại được).
- Bluetooth: device r63 đặt lại địa chỉ BT mỗi khi controller mất cấu hình (sau reset/resume) → đúng địa chỉ sau resume.
- ⚠️ Modem vẫn **thỉnh thoảng** assert mỗi 40 s trong vài phút đầu sau khi boot rồi tự ổn định (0–5 lần/boot, kể cả
  khi tắt macro âm thanh). Không phải do suspend. ModemManager được D-Bus kích hoạt và dò modem qua QRTR lúc đó
  (đã thử bật ModemManager trễ 90 s: vẫn 0–1 lần crash/boot → không phải nguyên nhân chính; thường chỉ lần khởi động đầu tiên của modem crash, sau khi tự khôi phục thì ổn định). Coredump của PAS không dùng được ("Invalid coredump configuration").

### Cảm biến (2026-11-19)
- ✅ Gia tốc kế, ánh sáng, tiệm cận, la bàn qua SSC trên ADSP (QMI service 400) + `hexagonrpcd` trên node FastRPC
  upstream `/dev/fastrpc-adsp-secure` (không cần shim như downstream), dữ liệu registry/config dùng chung
  `/var/lib/a70q-sensors`. `a70q-mainline-modem` khởi động hexagonrpcd (thử lại tối đa 5 lần) rồi restart
  iio-sensor-proxy; luật udev `80-a70q-ssc.rules` thêm thiết bị `62400000.remoteproc` (device r64).
  `monitor-sensor`: tilt face-up, la bàn ~63°. Downstream vẫn chạy như cũ.

### USB Type-C / OTG (2026-11-19)
- ✅ Khối Type-C/PD của PM6150 (`pm6150_typec`, vdd-pdphy L17A, như downstream qpnp-pdphy) + TCPM, connector
  dual-role (ưu tiên sink, chỉ PDO 5 V), VBUS = boost OTG của SMB5 (`pm6150_vbus`, 0.5–1 A); `usb_1` `dr_mode = "otg"`
  + `usb-role-switch` nối với connector. Cắm vào Mac: partner nhận diện, data role `device`, power `sink`, USB gadget
  và sạc vẫn chạy. Chế độ host (cáp OTG + thiết bị USB) chưa thử — cần cắm thiết bị thật.

### Điều khiển sạc (2026-11-19)
- ✅ `a70q-battery` (patch 0022) ghi thẳng thanh ghi SMB5: `constant_charge_current` (FCC 0x1061, bước 50 mA,
  ≤3 A), `constant_charge_voltage` (FV 0x1070, 3.6 V + 10 mV, ≤4.35 V theo profile pin), `input_current_limit`
  (USBIN ICL 0x1370, bước 50 mA), `charge_behaviour` auto/inhibit-charge (CHARGING_ENABLE_CMD 0x1042). Mặc định
  bootloader để lại: FCC 2.1 A, FV 4.35 V, ICL 1.5 A. Đã thử đọc/ghi và chặn/bật sạc.
- SoundWire (2026-11-19): patch 0030 cho driver qcom báo cả device 0 và xử lý trạng thái slave một lần sau khi bus
  chạy (để lõi SoundWire tự liệt kê bằng phần mềm). Kết quả: hết lỗi đọc thanh ghi WSA, nhưng lúc bus khởi động
  trạng thái slave = 0 và **bus clash xảy ra ngay lúc khởi động bus kể cả khi amply đang tắt (SD_N thấp)** → nguồn
  clash nằm ở đường dữ liệu/controller chứ không phải WSA881x. Cần DYNAMIC_DEBUG hoặc đo tín hiệu để đi tiếp.
  Macro/swr0 tắt lại.
- Sửa nhận định: thanh ghi 0x104 là `SWRM_COMP_MASTER_ID` (=1), **không phải** trạng thái slave. `SWRM_MCP_SLV_STATUS`
  (0x1090) = 0: không có thiết bị nào trả lời trên bus (kể cả device 0); ngắt `NEW_SLAVE_ATTACHED` không bao giờ đến;
  trạng thái ngắt 0x80008 = MASTER_CLASH_DET + CMD_IGNORED. Nghi phạm: SWR clock/frame không thật sự chạy — `fsgen`
  của WSA lấy từ VA macro, mà khối VA ở đây được cấp clock TX core (ADSP từ chối VA core 0x30b), nên bộ đếm FS của
  VA có thể không chạy. Downstream (bolero, `va-without-decimation`) lấy MCLK chung từ TX macro. Hướng tiếp: sinh
  fsgen từ TX macro (tx-macro có MCLK/FS_CNT riêng) thay vì VA. `CONFIG_DYNAMIC_DEBUG` đã bật.
- ✅ **Bus SoundWire chạy** (2026-11-19): `fsgen` của WSA macro lấy từ **TX macro** (MCLK/FS counter của TX, như
  bolero downstream trên SM6150) thay vì VA (patch 0031: `fsgen` của TX macro thành optional; VA macro vẫn cần để
  đọc phiên bản codec). Hết bus clash (trạng thái ngắt 0), device 0 attach ngay khi bus chạy (`slave status 1`).
  Còn lại: lõi SoundWire liệt kê bằng phần mềm nhưng lệnh đọc DevID của device 0 không có dữ liệu ("read underflow",
  lệnh bị bỏ qua) → WSA881x vẫn UNATTACHED. Với cấu hình này modem không crash (0 lần/boot thử).
- Thử thêm (không hiệu quả, để trong `patches-off/`): liệt kê device 0 bằng phần mềm (0030: lệnh đọc DevID bị bỏ qua,
  "read underflow"), SSP period = 1 như downstream (0032). Enumerator phần cứng vẫn không chốt được ID dù bus chạy.
  Các node âm thanh tắt lại (cấu hình fsgen-từ-TX giữ trong DT). Hướng tiếp theo: so từng thanh ghi khởi tạo master
  và trình tự clock của WSA macro với `swr-wcd-ctrl.c`/`wsa-macro.c` downstream, hoặc đo tín hiệu SWR clk/data.
- Cũng đã thử: xung reset SWR (`SWR_CONTROL` bit 1) quanh bước bật clock như `wsa_swrm_clock` downstream
  (`patches-off/0033`) → không đổi. `CDC_WSA_TOP_FREQ_MCLK` mainline = downstream (0x01).

### ✅ Loa chạy (2026-11-19)
- Đọc thanh ghi downstream lúc đang phát (`ref/swr-wsa-regs-downstream-playing.txt`): enumerator phần cứng chốt
  WSA881x (`0x538 = 0x21170213`) ở device 1 (`MCP_SLV_STATUS 0x1090 = 0x4`), bank 0 = khung **50×2**, bank 1 (phát)
  = 50×16.
- Patch 0034 (`qcom,soundwire-v1.5.0`): liệt kê ở 50×2 (bank 0), chuyển bank khi phát dùng 50×16, không dùng clock stop
  (broadcast SCP_STAT lỗi trên master này → runtime PM kẹt "error"). Patch 0035: WSA macro chờ VA macro báo phiên bản
  codec (không còn phụ thuộc qua fsgen). Patch 0031: fsgen từ TX macro.
- Kết quả: WSA881x **Attached** (device 1, như downstream), sound card "Samsung Galaxy A70", `aplay -D hw:0,0` phát hết,
  không lỗi/collision, bank chuyển 16 cột, modem không crash.
- Định tuyến (tạm, chưa có UCM): `WSA_CODEC_DMA_RX_0 Audio Mixer MultiMedia1`=1, `WSA RX0 MUX`=AIF1_PB,
  `WSA_RX0 INP0`=RX0, `WSA_COMP1 Switch`=1, `SpkrLeft COMP/BOOST/DAC Switch`=1, `SpkrLeft VISENSE Switch`=0,
  `WSA_RX0 Digital Volume`=70, `SpkrLeft PA Volume`=10.
- Userspace: `a70q-mainline-modem` đặt định tuyến mixer loa khi card xuất hiện (device r65); WirePlumber
  (`51-a70q-alsa.conf`, r66) đặt profile `pro-audio` cho card mainline (profile stereo-fallback gắn với cổng tai
  nghe không có) → sink `alsa_output.platform-soc_0_sound.pro-output-0` là mặc định, `paplay` chạy, quantum 2048 như
  downstream. Chưa có tai nghe/micro (WCD937x cần pinctrl LPASS LPI cho reset GPIO 24 và swr1/swr2).

### Tai nghe / micro: WCD937x (2026-11-19)
- Patch 0036: driver pinctrl LPASS LPI cho SM6150 (`qcom,sm6150-lpass-lpi-pinctrl`, 32 chân bước 0x1000 từ
  0x62b40000, chức năng theo DT downstream: SWR TX 18-20, SWR RX 21-23, reset WCD 24 func2). Patch 0037: RX macro chờ
  VA macro báo phiên bản codec.
- DT: RX macro 0x62ee0000, master RX 0x62ef0000 (SPI 138, 6 out + 1 in), master TX 0x62ed0000 (SPI 137, 1 out + 6
  in), WCD937x RX = unique id 4, TX = 3; nguồn L15A (buck), L10A (rxtx/px), BOB (mic bias).
- WCD937x bị giữ reset từ lúc TX macro probe (chân 24 output-low): sau warm reset codec còn attach từ kernel trước làm
  bus clash (giống WSA881x).
- Patch 0034 (bổ sung): master v1.5.0 không clock stop → luôn runtime-active; nếu không thì mỗi lần resume master đánh
  số lại bus và resume slave → deadlock với device link của WCD937x (arecord kẹt trạng thái D).
- DAI capture phải là `<&swr2 1>`: master TX có 1 cổng out nên DAI 0 là playback.
- Kết quả: phát ra tai nghe không lỗi (chưa cắm tai nghe để nghe thử); **micro chính (AMIC1 → ADC1 → TX SWR_ADC0)
  thu được**: im lặng rms ~21, khi loa phát 440 Hz rms ~360; thanh ghi cổng 2 = 0x01000103 như downstream.
- Định tuyến micro: `MultiMedia2 Mixer TX_CODEC_DMA_TX_3`=1, `TX_AIF1_CAP Mixer DEC0`=1, `TX DEC0 MUX`=SWR_MIC,
  `TX SMIC MUX0`=ADC0, `ADC1 Switch`=1, `ADC1_MIXER Switch`=1, `ADC1 Volume`=16, `TX_DEC0 Volume`=84.
- ADC2 (WCD TX2) ở master port 3 trên lane 1 (LPI GPIO 20): `ADC2 MUX`=INP3 là micro phụ (AMIC3), INP2 là micro tai nghe
  (AMIC2); `TX SMIC MUX0`=ADC2. Port 3 ở lane 0 thì bus clash.
- MBHC: `qcom,ground-jack-type-normally-closed` (downstream gnd-swh = 0), ngưỡng 640 mV / 9999 mV; có input
  "Samsung Galaxy A70 Headset Jack" và control `Headphone Jack`/`Mic Jack`.
- Userspace (device r67): định tuyến micro chính lúc khởi động; PipeWire có source `pro-input-1` (mặc định), parecord
  thu được.
- Tự chuyển loa ↔ tai nghe: `a70q-jack-switch` (device r68, chạy từ `a70q-mainline-modem`) đọc input "Headset Jack"
  (EVIOCGSW lúc khởi động, rồi sự kiện SW_HEADPHONE_INSERT / SW_MICROPHONE_INSERT): cắm tai nghe → MultiMedia1 sang
  RX codec DMA (HPHL/HPHR), tắt loa; rút ra → loa; có mic tai nghe → thu từ AMIC2 (ADC2 INP2), không → AMIC1. Đã chạy
  lúc boot và đặt đúng trạng thái "không cắm"; chưa thử cắm thật.

### ✅ Modem crash 40 s sau khi khởi động (2026-11-19): đã tìm ra nguyên nhân
- Dấu hiệu: "fatal error without message" đúng 40,03 s sau "modem is now up", chỉ ở lần khởi động modem **đầu tiên** sau
  boot (khởi động lại thủ công/sau phục hồi thì không), và lần đó ath10k không bao giờ thấy WLFW (không có
  "qmi chip_id"). Downstream (icnss) không bị.
- Nguyên nhân: ath10k_snoc chỉ cấp nguồn cho WCN3990 trong `hif_power_up`, tức là sau khi firmware WLAN trên modem báo sẵn
  sàng; nhưng firmware đó cần chip có điện mới lên được. L1C/L2C/L10C dùng chung với Bluetooth (BT bật), riêng **L9A
  (vdd-0.8-cx-mx) chỉ wifi dùng** nên vẫn tắt → WLAN PD không lên → modem assert sau 40 s. Từ lần thứ hai ath10k đã bật
  nguồn nên không còn crash.
- Sửa: `regulator-always-on` cho L9A, L1C, L2C, L10C (như icnss bật chip khi modem lên). Thử 3 lần boot liên tiếp:
  0/0/0 crash, WLAN lên ~1 s sau modem (trước đó 2/1/1/2 crash mỗi lần boot).
- Theo dõi thêm (đến 2026-11-19): tổng 9 lần boot mainline có bản sửa, 8 lần 0 crash, 1 lần có 1 crash (phiên 14 phút,
  không kịp lấy dmesg vì máy đã tự về downstream). Trước bản sửa: gần như mọi lần boot đều crash 1-2 lần. Còn một
  nguyên nhân hiếm gặp chưa rõ; cần bắt lại dmesg khi nó xảy ra.
- Patch 0038 (QRTR ns thử lại khi quảng bá dịch vụ cho remote vừa HELLO thay vì bỏ phần còn lại) vẫn giữ: có ghi nhận
  "announced service 14:1 ... after 1 retries" khi modem lên.
- Hệ quả: **microSD UHS SDR104 chạy** (bỏ `no-1-8-v`): 202 MHz, 1,8 V, đọc 87,6 MB/s, 2/2 lần boot không crash. Lỗi
  "UHS làm modem crash" trước đây chính là lỗi nguồn WCN3990 ở trên.

### Camera: khảo sát (2026-11-19)
- Linux 7.3.4 đã có driver CAMSS cho SM6150 (`qcom,sm6150-camss`: CSIPHY/CSID/VFE), driver CCI
  (`qcom,msm8996-cci`/`sm8250-cci` dạng v2) và camcc QCS615; `talos.dtsi` chưa có node camss/cci → phải tự thêm.
- Downstream: CCI @0xac4a000 (SPI 460), 5 cảm biến + 1 EEPROM, MCLK GPIO 28-31, reset GPIO 45/46/47/100, CSIPHY 0-2
  (0xac65000/0xac66000/0xac67000); tóm tắt trong `ref/camera-downstream.txt`.
- `talos.dtsi` thực ra đã có `cci@ac4a000` (`qcom,sm6150-cci`) và `camss: isp@acb3000` (`qcom,sm6150-camss`), đều
  disabled. **CCI đã bật** (`&cci { status = "okay"; }`): probe được, có 2 bus I2C "Qualcomm-CCI" (i2c-1 = CCI0,
  i2c-2 = CCI1); quét CCI1 không thấy thiết bị vì cảm biến chưa được cấp nguồn/MCLK/bỏ reset (cần node cảm biến).
- Việc còn lại: node cảm biến (nguồn cam_vio/vdig/vana, MCLK GPIO 28-31, reset) để đọc chip ID, bật CAMSS, rồi viết
  driver cảm biến (S5KGD1, S5K4HA, GC5035 chưa có trong mainline; chuỗi thanh ghi phải lấy từ thư viện camera của
  vendor).

### Reboot-mode trên mainline (2026-11-19)
- Trước đây `reboot-mode recovery` (RESTART2) chạy từ mainline không có tác dụng (không có driver reboot-mode) → máy về
  downstream; đó là lý do 3/6 lần thử liên tiếp "không vào được mainline" (lần chạy bắt đầu khi máy đang ở mainline).
- DT: syscon trên IMEM 0x146aa000 + `syscon-reboot-mode` offset 0x65c (downstream qcom,msm-imem-restart_reason):
  recovery 0x77665502, bootloader 0x77665500, normal 0x77665501. Đã thử: từ mainline `reboot-mode recovery` → boot lại
  vào RECOVERY (mainline).

### Build chính thức (2026-11-19, OrbStack chuyển sang SSD ngoài)
- Build lại từ DTS + patch (0021 có thử lại khởi tạo panel), device r68 (jack-switch, watcher crash modem). Thử 3 lần
  boot liên tiếp (2 lần khởi động từ mainline qua reboot-mode): 3/3 vào mainline, 0 crash modem, WLAN ~36 s, panel
  không lỗi, microSD SDR104, jack-switch chạy.
- Vị trí cảm biến (thư viện camera vendor): 0 = S5KGD1 (sau, chính 32 MP), 1/8 = S5KGD1 (trước), 2 = S5K4HA (góc
  rộng 8 MP), 3 = GC5035 (độ sâu 5 MP). Dữ liệu driver (slaveAddr, trình tự nguồn, bảng thanh ghi init/mode,
  địa chỉ gain/exposure) nằm trong `com.samsung.sensormodule.*.bin` (QTI Chromatix, Parameter Parser V1.4.0) -
  đã chép về `ref/camera-vendor/` (gitignore, file vendor); bước tiếp theo là viết bộ giải mã định dạng này để lấy
  bảng thanh ghi cho driver V4L2.
- **Nguồn camera: PMIC S2MPB03** (0x56) trên bus I2C bit-bang GPIO 117 (SDA)/118 (SCL). Mainline không có driver →
  patch 0039 `regulator: s2mpb03` (regmap, 7 LDO tuyến tính; sơ đồ thanh ghi từ driver GPL downstream). Đã chạy:
  "S2MPB03 PMIC ID 0x02", cam_ldo1..7 đăng ký với điện áp như downstream (1,05/1,05/1,8/1,2/2,8/2,8/2,8 V).
- **Camera sau chính đã trả lời trên mainline:** patch 0040 `a70q-campower` (công cụ bring-up: cấp nguồn vio = S2MPB03
  LDO3, vdig = LDO2, vana = LDO6, vaf = L19A, MCLK0 24 MHz trên GPIO 28, bỏ reset GPIO 47). Quét CCI0: thiết bị ở
  **0x2d**, chip ID (reg 0x0000, 16 bit) = **0x0841 = S5KGD1**. Lưu ý: bus i2c-gpio của PMIC thành i2c-1, CCI0 =
  i2c-2, CCI1 = i2c-3.
- Còn lại cho camera: driver V4L2 S5KGD1 (bảng thanh ghi init/mode từ Chromatix), node CAMSS + CSIPHY0, rồi các cảm
  biến khác (trước S5KGD1, S5K4HA, GC5035) theo cùng cách.
