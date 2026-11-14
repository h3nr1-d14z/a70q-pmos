# a70q graphics environment.
# Compositor: wlroots renders with pixman (the display DRM node is SDE-only and the GPU is
# the downstream kgsl driver, which wlroots' GLES/Vulkan renderers cannot use).
export WLR_RENDERER=pixman
export WLR_NO_HARDWARE_CURSORS=1
# Clients: GPU via turnip-kgsl (mesa-vulkan-freedreno-kgsl).
#  - TU_DEBUG=noconform: A612 has no HW multiview, so turnip only exposes Vulkan 1.0
#    without it; Zink needs >= 1.2.
#  - MESA_VK_WSI_DEBUG=sw: the pixman compositor offers no linux-dmabuf, present via wl_shm.
#  - GALLIUM_DRIVER=zink: OpenGL/GLES on top of Vulkan instead of llvmpipe.
export TU_DEBUG=noconform
export MESA_VK_WSI_DEBUG=sw
export GALLIUM_DRIVER=zink
