# artifacts/built/

Built boot images and ramdisks (gitignored). Contains camera kernel boot
images, stub ramdisks, and bundled firmware images. Rebuild with
tools/build-kernel.sh and rootfs/make-boot-image.sh.

## The reference image

`boot-standard-gzip.img` is the project's result: the **standard
delivery mode** with the firmware bundled read-only in the ramdisk.
It is the image to flash to a finished phone.

- 21,762,048 bytes (20.75 MiB), against the 23,068,672-byte limit of
  the boot and recovery partitions -- 1.25 MiB of headroom.
- The ramdisk is gzip, because the stock bootloader only unpacks a
  gzip ramdisk.  `boot-standard-xz.img` is the same image with an XZ
  ramdisk: 15,982,592 bytes, and it boots only on the `fastboot boot`
  path, where the kernel unpacks it.
- Checksums in `boot-standard.SHA256SUMS`.

It was built from the tree at `229a119` (the init stub that carries the
delivery mode) with the kernel `cf9fd9c2edf`:

```sh
bash tools/build-kernel.sh --tree linux --out /tmp/opencode/ko
bash scripts/build/build-micropython.sh
bash scripts/cam/build-camd.sh
bash rootfs/build-stub-ramdisk.sh --standard --bundle --force
RAMDISK=artifacts/built/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    artifacts/built/boot-standard-gzip.img
```

The cpio is not byte-reproducible (it carries inode numbers and
mtimes), so a rebuilt image has the same *content* but a different
hash; the sums are there to say which build a screenshot or a bug
report was about.

## Older artifacts

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