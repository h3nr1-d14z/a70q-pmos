# LOG.md — Nhật ký theo ngày

postmarketOS trên Galaxy A70 (a70q). Chi tiết từng lỗi: `ERRORS.md`; hướng dẫn: `pmos/README.md`.

## 2026-11-13 tối — Kernel tự build boot được (k0, k1)
- Bỏ cây 4.14.261 (lineage-18.1); dùng a70q-lineage `lineage-22.2` @ 699d9d7da = source của blob LOS (config + DTB khớp 100%)
- k0 (tái tạo) và k1 (+DEVTMPFS) đều boot LOS; k1 xác nhận qua adb: `a70q@pmos`, Ubuntu clang 19.1.1, devtmpfs có
- Mất ~1h vì USB adb "không lên" → hoá ra node ma Download Mode kẹt ở hub cổng front Mac mini (C23), không phải kernel; heimdall stall lần đầu (C24) → `pmos/tools/flash.sh`
- Chi tiết: ERRORS.md mục B; quy trình: pmos/README.md

## 2026-11-14 rạng sáng — pmOS initramfs CHẠY trên A70 + USB networking OK
- pmbootstrap 3.12.1 trong container arm64 (`pmos/pmb/`), pmaports `device-samsung-a70q` + `linux-samsung-a70q` (downstream, build từ source 699d9d7da, Alpine clang 23)
- Initramfs pmOS + `pmos.debug-shell` boot OK: shell qua USB ACM serial; kernel `4.14.190-perf #3-postmarketOS`
- Sửa 4 lỗi build (C26–C28) + BUG kernel u_ether RX (C29) → USB ECM: DHCP 172.16.42.2, ping, telnet, 8MB sha256 khớp
- Không có microSD (mmc0: card not present); UFS sda 128GB — chưa đụng userdata/rootfs

## 2026-11-14 sáng — ⭐ pmOS BOOT VÀO ROOTFS + SSH OK
- Userdata xoá (theo lựa chọn user), rootfs OpenRC sparse-flash vào USERDATA; `/` = loop0p2 ext4 (4K), resize 108GB
- Chuỗi lỗi: systemd≥5.4 (C31) → loop node Docker (C32) → heimdall cần sparse (C33) → thiếu loop.max_part (C34) → 2 bug kernel loop/4K (C35, patches 0002–0004)
- `ssh user@172.16.42.1` — Nura (pmOS) edge, kernel 4.14.190-perf #9-postmarketOS, RAM 5.5GB

## 2026-11-14 trưa — GPU Adreno 612 chạy (Vulkan + GL qua Zink)
- turnip build với backend kgsl (package `mesa-vulkan-freedreno-kgsl`), firmware GPU từ vendor, udev rule, env trong profile.d
- Sau reboot sạch: `zink Vulkan 1.3(Turnip Adreno (TM) 612)`, glmark2 73 (llvmpipe 34); chi tiết C47

## 2026-11-14 chiều — Âm thanh chạy (loa, tai nghe, mic) + DTBO bỏ Virtual-1
- ADSP boot từ service; UCM cho PipeWire; kernel r15 (patch 0008 jack kcontrol); device r11
- DTBO lần 1 hỏng màn hình (C49), lần 2 đúng; mất thời gian vì tự kill nhầm tiến trình (C50)

## 2026-11-14 tối — Suspend + nút nguồn chạy
- "Bấm nguồn là tắt máy" thực ra là elogind `HandlePowerKey=poweroff` (PMIC: PS_HOLD), không phải crash
- WoWLAN magic-packet cho phép ngủ khi WiFi kết nối; wrapper `wlopm` (lần bật màn hình đầu luôn lỗi); hook postwake bật màn hình khi dậy bằng nút nguồn; device r16 (ERRORS C51–C52)

## 2026-11-14 tối — Giao diện chạy trên GPU + jack tai nghe
- wlroots 0.20.2 + patch `WLR_DISPLAY_ONLY_DRM`: sway render Vulkan trên turnip-kgsl vào dumb buffer của SDE (C54)
- Kernel r18 (patch 0009, 0010): alsa-lib nạp được mixer → PipeWire theo dõi jack, rút tai nghe tự về loa (C55)

## 2026-11-14 tối — Đèn pin, rung, độ sáng, xoay tay, Bluetooth
- Device r22: đèn pin/rung/độ sáng qua Sxmo (C56); xoay tay chạy, xoay tự động cần sensor registry qua FastRPC (C57); modem tạm hoãn vì chưa có SIM (C58)
- Bluetooth: kernel r19 (`BT_HCIUART` + H4), `a70q-btinit` nạp firmware WCN3988 ở userspace; tự lên khi boot (C59)

