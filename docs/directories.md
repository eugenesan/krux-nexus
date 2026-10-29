# Project layout (post-restructure)

The project has three git submodules (`krux`, `micropython`, `linux`) plus the
device-side work (rootfs, camera, NFC, scripts). All documentation lives under
`docs/`.

Top-level:

- `README.md` - project overview (route B)
- `docs/` - all documentation (see `docs/README.md` for the index)
- `krux/` - Krux firmware (submodule, branch `nexus-port`; `krux/firmware/MaixPy`
  is intentionally uninitialized — only needed for the original MaixPy build,
  not the MicroPython route)
- `micropython/` - MicroPython (submodule, branch `master`; the port's C modules
  live in `ports/unix/`)
- `linux/` - kernel (submodule, branch `airgap/master-cleanup`)
- `cam/` - camera: imaging server (`imaging8.c`), daemon (`camd.c`), sensor
  register tables, CPP firmware.  `camd` binary is gitignored.
- `rootfs/` - init stub + ramdisk + boot image helpers; the built static
  MicroPython binary is `rootfs/micropython-arm32` (gitignored).  `stock/`
  was dropped; built images are in `artifacts/built/`.
- `artifacts/built/` - built boot images and ramdisks (gitignored)
- `scripts/` - build drivers and device probes, grouped by topic.
  `scripts/misc/` holds test scripts (smoke, QR, entropy, LCD, stability,
  cycles, air-gap).
- `tools/` - reusable build/config helpers (kernel, defconfig, toolchain, profiles)
- `nfc/` - NFC stack + vendor headers (reference only; the transport is in Python)
- `phase0/` - phase 0 proof-of-concept (reference)
- `probes/` - probe sources
- `artifacts/` - downloaded/stock images (gitignored)

See `docs/README.md` for the doc index and `scripts/README.md` for the script
layout.
