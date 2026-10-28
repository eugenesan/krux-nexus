# Project layout (post-restructure)

The project has three git submodules (krux, micropython, linux) plus the port, the camera work, the rootfs and the scripts. All documentation lives under docs/.

Top-level:
- README.md - project overview (route B)
- docs/ - all documentation
- krux/ - Krux firmware (submodule; `krux/firmware/MaixPy` is intentionally uninitialized — only needed for the original MaixPy-based Krux build, not the MicroPython route)
- micropython/ - MicroPython (submodule, port applied)
- linux/ - kernel (submodule, airgap/master-cleanup)
- kruxport/ - the port: modules + C modules + build scripts
- cam/ - camera: server sources, captured data, probes
- rootfs/ - init stub + ramdisk + boot image helpers
- scripts/ - device-driving scripts (by topic)
- tools/ - kernel build + defconfig + profiles
- nfc/ - NFC stack + vendor headers
- phase0/ - phase 0 proof-of-concept (reference)
- probes/ - probe sources

See docs/README.md for the doc index.
