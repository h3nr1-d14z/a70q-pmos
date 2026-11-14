# GPU PORT — SM6150 (A70 a70q) Adreno 612 trên native Linux

Mục tiêu: **GPU accel (Adreno 612, chipid `0x06010200`) + WiFi** trên native Linux (pmOS/mainline) cho A70 — không cần display-out, không cần modem, không cần camera.

Trạng thái khoảng định (xác minh qua tree thực tế trong `halium-src`):
- Kernel halium hiện tại: lineage-18.1 tree (`/work/kernel/samsung/sm6150`) — kernel 4.14.261 downstream
- **`gpucc-sdmmagpie.c` TỒN TẠI trong tree 18.1** (magpie = codename SM6150) — driver GPU clock downstream có sẵn, KHÔNG cần viết từ đầu
- `sm6150-gpu.dtsi`: downstream GPU node dạng **kgsl** (compatible `qcom,kgsl-3d0`, chipid `0x06010200`, firmware `a612_zap`) — KHÔNG phải binding mainline
- Mainline 6.13+: **`gpucc-qcs615.c` ĐÃ MAINLINE** (Taniya Das/Qualcomm, patch v4 01/2025, gcc-qcs615 vẫn maintain 2026) — QCS615 = cùng die SM6150
- Mainline `qcs615.dtsi`: **CHƯA có GPU node** (chỉ có clock controllers)
- LOS 22.2 kernel (4.14.190): boot OK trên máy — proven baseline

## 2 PATH GPU accel

### PATH A — KGSL short path (downstream kernel + Mesa kgsl backend) — NHANH
- Dùng **kernel downstream Samsung 18.1** (tree `/work/kernel/samsung/sm6150` — đầy đủ GPU kgsl driver a6xx + panel + touch, proven bởi LOS 18.1 builds)
- pmOS hỗ trợ device downstream kernel (pmOS device category "downstream")
- GPU userspace: **Mesa turnip (Vulkan) với kgsl backend** (đã mainline) → GL qua **ZINK** (Vulkan-on-Vulkan)
- Điều kiện: kernel phải expose kgsl với permission đúng (root/gru access `/dev/kgsl-3d0`), ION/dmabuf allocation (kernel 4.14 dùng ION legacy — kiểm `CONFIG_ION`/dmabuf heapprovider trong defconfig; turnip kgsl backend kỳ vọng dmabuf heap — có thể cần backport hoặc dùng ION legacy path)
- Công việc chính: pmOS device port trên downstream kernel (kgsl DT gốc 18.1, có sẵn ✓) + Mesa kgsl/ZINK userspace
- Ước lượng: tuần (không phải tháng) — GPU accel thực, hiệu năng Vulkan turnip tốt cho a6xx
- Rủi ro: freedreno GL không có kgsl backend chắc chắn (chỉ turnip/kgsl chắc) → GL phải qua ZINK (một lớp thêm, perf -10-20%); kgsl trên 4.14 với Mesa mới có thể cần vá nhỏ

### PATH B — mainline msm GPU (a6xx) — ĐÚNG NGHĨA LÂU DÀI
- Dùng kernel mainline/pmOS 6.14+ với `gpucc-qcs615` (đã mainline ✓)
- Công việc 4 mảnh:
  1. **GPU + GMU + zap DT nodes**: port từ `sm6150-gpu.dtsi` downstream → mainline adreno binding. Reference chép từ mainline `sm7125.dtsi` (a618, cùng gen3 GMU) hoặc `qcs8300.dtsi` (a619). Reg/interrupt giữ từ downstream (kgsl_3d0_reg_memory 0x5000000, irq 300), thay compatible/binding, nối gpucc-qcs615 clocks + rpmhpd CX/MX
  2. **rpmhpd sdm675/sm6150**: kiểm `drivers/pmdomain/qcom/rpmhpd.c` mainline — nếu chưa có sdm675/sm6150 → port từ downstream (rpmhpd data: CX/MX/GX levels)
  3. **Firmware**: `a612_zap.mbn` extract từ stock (`/vendor/firmware_mnt/image/` hoặc `/vendor/firmware/` qua adb) + **GMU firmware** (a612 dùng cùng family a619_gmu — lấy từ linux-firmware `qcom/a619_gmu.bin` hoặc từ stock `/vendor/firmware_mnt/image/a619_gmu.bin` nếu có; verify qua downstream dtsi `qcom,gmu-fw` nếu có)
  4. **Mesa freedreno a612 check**: freedreno nhận GPU theo ID đọc từ kernel — nếu a612 (0x06010200) chưa trong bảng → vá nhỏ userspace (freedreno GPID table) — check Mesa git trước khi vá
- Ước lượng: **vài tuần → vài tháng** (gpucc port = phần lớn; DT = nhỏ; firmware = nhỏ; Mesa = nhỏ nếu cần)
- Rủi ro: gpucc port sang kernel 6.x APIs = công việc thật; zap-shader ký/security trên a612 từng làm hỏng GPU trên device khác (check logs mainline a6xx discussions); rpmhpd sdm675 có thể thiếu → port thêm

### Khuyến nghị thứ tự
1. **PATH A trước** (kgsl/ZINK) — GPU accel nhanh, dùng được, học được GPU behavior qua userspace
2. **PATH B song song/dần dần** — mainline GPU = long-term đúng đắn, ko chặn PATH A
3. Cả hai dùng chung initramfs/flash flow hiện tại (đã trong repo)

## Chi tiết PATH B — các mảnh port cụ thể

### B1. gpucc-sdm675/sm6150 clock driver
- Nguồn port: 18.1 tree `drivers/clk/qcom/gpucc-sdmmagpie.c` (ĐÃ CÓ trong volume ✓) HOẶC tốt hơn: **Samsung SM6150 tree mới hơn** (kernel 5.4/5.10 Android 12/13 — gpucc-sdmmagpie.c phiên bản gần upstream hơn) — tìm qua Samsung opensource (opensource.samsung.com: SM-A705F kernel source) hoặc GitHub mirrors
- Target: kernel pmOS a70q (rtd1250/pmos_a70q — kiểm version ở repo đó; khuyến nghị dùng 6.12+ để có gpucc-qcs615 mainline THAY THẾ hoàn toàn — khi đó có thể KHÔNG cần port gpucc riêng: dùng gpucc-qcs615 mainline + DT node gpucc của nó!)
- Conversion checklist downstream → mainline 6.x:
  - `parent_names` → `parent_data` (.fw_name/.index cho clock từ RPMh, .name cho internal)
  - `clk_rcg2` ops hiện đại, `freq_tbl` giữ nguyên từ downstream
  - GDSC: `struct gdsc_desc` mới, GX GDSC với `GENPD` binding
  - `qcom_cc_map`/`qcom_cc_really_probe` API mới (probe_data struct — qcom clk rework 2026)
  - Kconfig `CONFIG_GPUCC_QCS615` có thể TÁI SỬ dụng trực tiếp (QCS615 = cùng die!) — hoặc thêm alias compatible `qcom,sm6150-gpucc`
- Điểm mấu chốt: **so sánh gpucc-qcs615.c (mainline) với gpucc-sdmmagpie.c (downstream) từng clock** — nếu trùng (cùng RCG/GDSC/pll cho die giống nhau) → chỉ cần compatible alias + nhỏ tweaks

### B2. DT nodes
- GPU node mainline (copy từ mainline sm7125.dtsi a618 node, đổi reg/irq/clock theo sm6150):
  - compatible: `qcom,adreno-612.0` (hoặc theo cách mainline đặt cho a612)
  - reg: 0x5000000 (kgsl_3d0_reg_memory) + zap region
  - clocks: gpucc gfx3d src, rbbmtimer, axi — từ gpucc-qcs615
  - power-domains: CX (rpmhpd) + GX (gpucc GDSC)
  - iommus + dma ranges từ downstream sm6150-gpu.dtsi (gpu smmu context bank)
  - GMU node: gmu@506a000, firmware a619_gmu (kiểm trước)
  - zap-shader: memory-region + firmware a612_zap
- memreserve: `/memreserve/ 0xa1300000 0x100000` (ramoops — như đã inject vào 18.1 dtb, áp cùng cách lên DT pmOS)

### B3. Firmware
- `a612_zap.mbn`: adb từ LOS/stock: `/vendor/firmware_mnt/image/a612_zap.mbn` hoặc `/vendor/firmware/a612_zap.mbn` → install vào `/lib/firmware/qcom/a612_zap.mbn`
- GMU fw: linux-firmware `qcom/a619_gmu.bin` (hoặc extract từ stock nếu tên khác — kiểm downstream dtsi gpu node `qcom,gmu-fw`)
- Đặt vào pmOS image: `/boot` hoặc initramfs copy vào `/lib/firmware` trước khi probe GPU (hoặc initramfs hook)

### B4. Mesa userspace
- freedreno hỗ trợ a6xx family; GPU ID a612 đọc từ `Adreno 612` chipid — nếu freedreno thiếu entry a612 → patch nhỏ `src/freedreno/common/freedreno_devices.py` (a612 gần a618) + regenerate
- Nếu GL qua ZINK-on-turnip: chỉ cần turnip nhận a612 (tương tự)
- Alpine/pmOS Mesa: kiểm `mesa/src/freedreno` a612 trước khi vá

## B5. Chi tiết PATH A — kgsl (nếu chọn nhanh)
- Kernel: downstream 18.1 (tree có sẵn trong volume, kgsl GPU full stack)
- pmOS device port: deviceinfo ( Downstream category), kernel recipe dùng tree 18.1 + defconfig gốc
- Mesa: turnip kgsl backend — kiểm Alpine mesa build flags (`-Dvulkan-drivers=freedreno` + kgsl); freedreno GL: ZINK
- Firmware: kgsl cần zap tại `/vendor/firmware` (pmOS: copy vào initramfs)
- GPU perf: turnip trên a612 qua kgsl = Vulkan native, đủ cho GL desktop qua ZINK

## Verify từng stage (PATH B)
1. gpucc probe: `cat /sys/kernel/debug/clk/clk_summary | grep gpu` — gpu_cc_gx_gfx3d_clk_src xuất hiện với rate
2. GPU probe: `/sys/bus/platform/devices/5000000.gpu/` tồn tại + `dmesg | grep -i adreno` không lỗi zap/GMU
3. Mesa: `eglinfo` thấy freedreno/turnip device
4. glmark2-es2 score > 0 (GPU accel thực, không llvmpipe)

## Rủi ro tổng hợp
- gpucc port API drift (kernel 6.x qcom framework đổi nhiều 2024-2026 — cc-option rework, probe_data struct mới)
- zap-shader security: a612 zap có thể cần signed fw cụ thể của Samsung (extract từ stock, không dùng AOSP generic)
- GMU firmware không nhận die a612 (fallback: dùng `qcom,client-dev` GMU-less mode — mainline a6xx hỗ trợ GMU-less cho một số GPU)
- rpmhpd sdm675 thiếu → port CX/MX levels từ downstream rpmhpd driver
- Mesa a612 entry thiếu → vá freedreno_devices.py

## Tài liệu tham chiếu chính
- Downstream tree (trong volume): `/work/kernel/samsung/sm6150` — gpucc-sdmmagpie.c, sm6150-gpu.dtsi, drivers/gpu/msm/ (kgsl)
- Mainline gpucc-qcs615: `drivers/clk/qcom/gpucc-qcs615.c` (Taniya Das series, mainline ~6.13)
- Mainline GPU node reference: `arch/arm64/boot/dts/qcom/sm7125.dtsi` (a618), `qcs8300.dtsi` (a619), `qcs615.dtsi` (mainline, chưa GPU)
- Mesa freedreno devices: `src/freedreno/common/freedreno_devices.py`
- turnip kgsl: `src/freedreno/vulkan/kgsl/`
- linux-firmware: `qcom/` (a619_gmu, zap shaders)
- LOS boot.img gốc (ramdisk LOS + dtb LOS proven): `backups/full/boot.img`
