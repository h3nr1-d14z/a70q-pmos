# GPU compositor trên a70q (sway/wlroots Vulkan trên turnip-kgsl)

Kernel downstream tách hai phần: driver hiển thị SDE là DRM (`card0`, `renderD128`) nhưng không có
GPU; GPU Adreno 612 là kgsl (`/dev/kgsl-3d0`), không có DRM node. wlroots mặc định đòi GPU và
màn hình cùng một thiết bị DRM (GBM hoặc `VK_EXT_physical_device_drm`), nên trước đây sway phải
render bằng pixman (CPU).

- `dmabuf-kgsl-test.c`: bài thử tính khả thi — cấp phát GEM trên SDE (`MSM_GEM_NEW` trên
  `renderD128`), export dma-buf, import vào turnip-kgsl (`VK_EXT_external_memory_dma_buf`), GPU clear
  một image tuyến tính, CPU kiểm tra: 65536/65536 pixel đúng
- `wlroots-0001-WLR_DISPLAY_ONLY_DRM.patch` (áp vào wlroots 0.20.2): khi `WLR_DISPLAY_ONLY_DRM=1`
  - renderer Vulkan nhận GPU không có `VK_EXT_physical_device_drm` và dùng fd DRM của màn hình
  - allocator bỏ GBM, dùng dumb buffer của SDE (scanout được, export dma-buf cho GPU)
- `build-wlroots.sh`: build nhanh `libwlroots-0.20.so` có patch trong Docker để thử
- Package dùng thật: `pmaports/device/downstream/wlroots0.20` (pkgrel 100, cao hơn bản Alpine);
  `/etc/profile.d/a70q-wlroots.sh` chỉ bật `WLR_RENDERER=vulkan` khi thư viện đang cài có patch

Giới hạn: kernel 4.14 không có dma-buf sync_file → wlroots chờ GPU phía CPU ("Implicit sync interop
not supported, falling back to blocking"). Client vẫn trình bày qua wl_shm (`MESA_VK_WSI_DEBUG=sw`).
Thử bằng sway riêng: binary sway có file capability nên `LD_LIBRARY_PATH` bị bỏ qua — chép
`/usr/bin/sway` ra chỗ khác để chạy với thư viện thử.
