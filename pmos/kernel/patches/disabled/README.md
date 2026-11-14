# Disabled patches

- `0005-drm-msm-fbdev-use-the-SDE-unsecure-address-space.patch` — makes msm_fbdev_create()
  get an iova from the SDE unsecure aspace (fixes "failed to get buffer obj iova: -22").
  **Result on device: bootloop** (kernel r10, 2026-11-14); panic log not captured
  (ramoops/pstore is not preserved across the panic reboot on a70q, /proc/last_kmsg holds only ABL log).
  Not needed for a UI: DRM/KMS works without fbdev (modetest pattern visible on the panel).
- `0007-arm64-dts-sm6150-drop-SDE-writeback-display-from-connectors.patch` — no effect: the display
  nodes (connectors) live in the Samsung DTBO overlay, not in the base DTB appended to the kernel
  (DTB stayed byte-identical). Needs a rebuilt dtbo.img. Workaround: `output Virtual-1 disable` in the
  compositor config.
