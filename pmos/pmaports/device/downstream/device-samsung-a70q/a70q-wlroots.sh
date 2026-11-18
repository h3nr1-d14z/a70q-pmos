# a70q graphics environment.
# Compositor: the display DRM device (SDE) has no GPU and the GPU (kgsl) has no DRM node.
# wlroots0.20 from our pmaports carries the WLR_DISPLAY_ONLY_DRM patch: Vulkan renderer on
# turnip-kgsl, rendering into dumb buffers of the SDE device (ERRORS C54). Without that
# patch the Vulkan renderer cannot start, so fall back to pixman (CPU) then.
# Mainline kernel (pmos/mainline): the msm GPU (renderD128) renders, the DPU/DSI device
# (msm_dpu, separate card with msm.separate_gpu_kms=1) displays; without the panel driver only
# simpledrm is there and wlroots renders on the CPU.
if [ ! -e /sys/class/kgsl ]; then
	export WLR_NO_HARDWARE_CURSORS=1
	if [ -e /dev/dri/renderD128 ] && ls /sys/class/drm/ 2>/dev/null | grep -q -- -DSI-; then
		export WLR_RENDERER=gles2
		export WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128
	else
		export WLR_RENDERER=pixman
		export WLR_LIBINPUT_NO_DEVICES=1
	fi
	return 0 2>/dev/null || true
fi
if grep -q WLR_DISPLAY_ONLY_DRM /usr/lib/libwlroots-0.20.so 2>/dev/null; then
	export WLR_RENDERER=vulkan
	export WLR_DISPLAY_ONLY_DRM=1
else
	export WLR_RENDERER=pixman
fi
export WLR_NO_HARDWARE_CURSORS=1
# Clients: GPU via turnip-kgsl (mesa-vulkan-freedreno-kgsl).
#  - TU_DEBUG=noconform: A612 has no HW multiview, so turnip only exposes Vulkan 1.0
#    without it; Zink needs >= 1.2.
#  - MESA_VK_WSI_DEBUG=sw: the pixman compositor offers no linux-dmabuf, present via wl_shm.
#  - GALLIUM_DRIVER=zink: OpenGL/GLES on top of Vulkan instead of llvmpipe.
export TU_DEBUG=noconform
export MESA_VK_WSI_DEBUG=sw
export GALLIUM_DRIVER=zink
