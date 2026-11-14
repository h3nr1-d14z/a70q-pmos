# a70q: no GPU driver yet (Adreno 612 via downstream kgsl) -> CPU rendering in wlroots
export WLR_RENDERER=pixman
export WLR_NO_HARDWARE_CURSORS=1
