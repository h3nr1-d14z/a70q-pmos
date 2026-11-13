# Disabled patches

- `0005-drm-msm-fbdev-use-the-SDE-unsecure-address-space.patch` — makes msm_fbdev_create()
  get an iova from the SDE unsecure aspace (fixes "failed to get buffer obj iova: -22").
  **Result on device: bootloop** (kernel r10, 2026-11-14); panic log not captured
  (ramoops/pstore is not preserved across the panic reboot on a70q, /proc/last_kmsg holds only ABL log).
  Not needed for a UI: DRM/KMS works without fbdev (modetest pattern visible on the panel).
