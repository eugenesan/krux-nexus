# artifacts/built/

Built boot images and ramdisks (gitignored). Contains camera kernel boot
images, stub ramdisks, and bundled firmware images. Rebuild with
tools/build-kernel.sh and rootfs/make-boot-image.sh.

## The reference image (2026-11-13)

`boot-final-sealed.img` is the delivered firmware: the **standard,
sealed** delivery mode with the firmware bundled read-only in the
ramdisk, flashed to the recovery partition (`mmcblk0p20`) and verified
on device serial `0879524b0068abca`.

- 20,064,256 bytes, within the 23,068,672-byte recovery-partition limit.
- sha256 `807facd8a5b521daf675b29e910efb441b77e432873f327030c079c540130892`.
- kernel `673242ee6e09e25edc658dfb6ee05e794eb821e5e35e08f2cd647a3588652805`
  (preserved in `kernel-final/` and at
  `ko/arch/arm/boot/zImage-dtb`).
- gzip ramdisk, because it is flashed; XZ is only for `fastboot boot`.
- TWRP by default with adb off; volume-up or `/sbin/start-krux` starts
  Krux; `/sbin/start-adb` and `/sbin/start-serial` are the ways in.
- a power-off marks RAM and the next boot wipes 1488 MB before userspace.

The serial trace is in `logs/wipe-serial-2026-11-13.log`; the kernel
provenance is in `kernel-final/README.md`.

`boot-standard-release.img` is the same standard/sealed build rebuilt
after the air-gap gate was added:

- 20,021,248 bytes, sha256
  `436d8a5595230edb3b7f6b4b66f1d4cd1153d26b464c1f345a190dd501cfeee4`.
- the same kernel (`673242ee…`) and the same behaviour.
- `check-airgap.py` PASS: no WLAN/BT/cellular/FM, NFC only.
- TWRP's dead Wi-Fi keystore HAL is pruned from its ramdisk.
- Flash it to `recovery` when the device is next in fastboot; the
  currently flashed `boot-final-sealed.img` has no wireless drivers
  either, only the two inert Wi-Fi-named keystore libraries.

`boot-standard-gzip.img` is the earlier reference build (standard mode,
gzip ramdisk); keep it for comparison.  The shipped image supersedes it.

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