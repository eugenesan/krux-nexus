# artifacts/built/

Built boot images and ramdisks (gitignored). Contains camera kernel boot
images, stub ramdisks, and bundled firmware images. Rebuild with
tools/build-kernel.sh and rootfs/make-boot-image.sh.

`cam-yonly/` preserves the last verified camera test artifacts before the
host `/tmp` reboot:

- `boot.img` — stable y-only + CAMIF-mask TWRP boot image
- `camd` — matching camera daemon binary
- `camd.c` — exact daemon source used for that binary
- `zImage-dtb` — y-only/CAMIF-mask kernel image embedded in the boot image
- `kernel.config` — kernel configuration used for that image
- `logs/` — camera and standalone Krux test evidence
- `SHA256SUMS` — checksums of the preserved artifacts

These are ignored build artifacts, not source; `rootfs/micropython-arm32`
holds the current MicroPython firmware build on disk.