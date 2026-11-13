# Memory wipe — current state (2026-11-13)

The kernel-side wipe is implemented and verified again on the XZ
kernel; the userspace **kexec handoff is still the open blocker**.  A
serial console is now available (`/dev/ttyUSB0`, 115200 8N1), so the
handoff can finally be debugged.

## Branch and commits

- Linux: `airgap/cam-xz-wipe`, head `c47f7605af5`, branched from the
  current camera+release work and carrying the memory-wipe engine plus
  the XZ piggy.  It is the `airgap/memory-wipe` work cherry-picked onto
  the current tree.
- The camera branch (`airgap/cam-ksu-dump`) is untouched and is the
  branch to use for camera work.
- `scripts/build/build-kexec.sh` and `rootfs/stub/krux-wipe` are back
  on master for the kexec tests.

## Verified with the serial console (2026-11-13)

- `wipe=test`: wipes **1488 MB** and boots normally to recovery.
- `wipe=1`: wipes **1488 MB** and powers off; the device stays off.
- Kernel: `CONFIG_KERNEL_XZ=y` (zImage 4.1 MB, zImage-dtb 5.5 MB).

The wipe=1 serial trace:

```
wipe: start (mode 1, test 0 MB, verify 0)
wipe: 1488 MB wiped
wipe: powering off
```

The power-off is `kernel_power_off()` from `kernel_init()` after
`do_basic_setup()`, so `pm_power_off` (the MSM PMIC path) is already
installed.  With the USB cable connected the device re-powers itself
(`reboot_mode restart reason = power_on`); disconnect USB for a true
off.  The RAM is wiped either way.

## What works

`wipe=` is an early parameter parsed in `setup_arch` and acted on at the
top of `mem_init()`:

| Mode | Action |
|---|---|
| `wipe=1` / `wipe=all` | Wipe free RAM, then power off from `kernel_init()` |
| `wipe=test` | Wipe free RAM and continue booting |
| `wipe=test:N` | Wipe at most N MB of free RAM and continue |
| `wipe=verify` | Wipe, sample lowmem pages, log the result, power off |
| `wipe=info` | Log the memblock/bootmem layout and return |

Verified on the device:

- `wipe=info`: 1891 MB RAM, 386 MB reserved, `max_low_pfn=187392`,
  `max_pfn=524288`, `mem_map` at `0xf41000` (16 MB), 137324 free lowmem
  pages.
- `wipe=test:1/4/6/7/8`: all boot normally.
- `wipe=test` (full): **1487 MB wiped**, stub boots normally.
- `wipe=1`: wipes 1487 MB and powers off; USB stays gone.

## Implementation

Kernel (all on `airgap/memory-wipe`):

- `arch/arm/mach-msm/wipe.c` — the wipe engine.
  - Lowmem: walks the **bootmem bitmap** and wipes pages whose bit is
    clear (free).  `memblock.reserved` is **not** enough: `mem_map`,
    page tables and other bootmem allocations are not in it, and
    wiping them crashes.
  - Highmem: walks `memblock.memory` above `max_low_pfn`, subtracts
    `memblock.reserved`, and wipes full pages with
    `kmap_atomic_pfn()`.
  - Full pages only; partial boundary pages are skipped.
  - `flush_cache_all()`, `dsb()`, `isb()` after the wipe.
  - `wipe=1` sets a pending flag; `wipe_poweroff()` is called from
    `kernel_init()` after `do_basic_setup()`, so the normal
    PMIC/restart `kernel_power_off()` path is used.  The direct
    PSHOLD write remains only as a fallback.
- `arch/arm/mm/init.c` — calls `wipe_main()` at the top of
  `mem_init()`, after `max_mapnr` is set and before
  `free_all_bootmem()`.
- `init/main.c` — `wipe_poweroff()` call in `kernel_init()`.
- `arch/arm/mach-msm/Kconfig` / `Makefile` — `CONFIG_MEMORY_WIPE`.
- `arch/arm/configs/lineageos_hammerhead_defconfig` —
  `CONFIG_KEXEC=y`, `CONFIG_MEMORY_WIPE=y`.
- `arch/arm/kernel/machine_kexec.c` — writes relocation parameters into
  the copied control page (`.text` is read-only under
  `CONFIG_STRICT_MEMORY_RWX`), finds the DTB segment and passes its
  physical address in r2, and logs `kexec: entry=... dtb=...`.
- `arch/arm/mach-msm/msm_kexec.c` — calls the V2 watchdog disable.
- `arch/arm/mach-msm/msm_watchdog_v2.c` — exports
  `msm_watchdog_v2_disable()` using the driver's ioremapped base.

Userspace / tooling:

- `rootfs/stub/krux-wipe` — loads `/data/krux/boot-wipe.img` (or the
  boot partition) with `wipe=1`, `--dtb /data/krux/hammerhead.dtb`, and
  executes it.
- `krux/firmware/micropython/ports/unix/machine_hw_linux.c` — tries
  `/sbin/krux-wipe` before the raw reboot/setprop path.
- `krux/src/krux/power.py` — `shutdown()` no longer suspends before
  `machine.reset()`.
- `scripts/build/build-kexec.sh` — clones/builds static ARM
  kexec-tools.
- `scripts/build/kexec-tools-hammerhead.patch` — appended-DTB
  detection, separate-DTB truncation, and a 256 MB minimum load
  address.
- `rootfs/build-stub-ramdisk.sh` — installs `/sbin/kexec` and
  `/sbin/krux-wipe` when `KEXEC` points at the binary.
- `rootfs/make-boot-image.sh` — `EXTRA_CMDLINE` appends to the image
  cmdline (used for `wipe=test`).

## Kexec blocker

The jump does not reach the new kernel: the phone stops on the
bootloader logo.  Everything below was fixed, but the failure remains:

1. `CONFIG_STRICT_MEMORY_RWX=y` made `machine_kexec` fault writing
   `kexec_start_address` in `.text`.  Fixed by patching the copied
   relocation block instead.
2. The V2 watchdog (`qcom,wdt@f9017000`, bark 11 s) kept running across
   the jump and reset the SoC.  Fixed with the driver's ioremapped
   disable helper.
3. The generic ARM `kexec_boot_atags` offset (`0x7000` below the
   zImage) is too small for the 176 KB hammerhead DTB.  Fixed by
   finding the DTB segment and passing its physical address in r2.
4. kexec-tools' Android loader keeps the appended zImage-dtb; the
   decompressor then prefers the appended DTB, whose
   `linux,initrd-start` points at the old initrd that the previous
   kernel already freed.  Fixed by truncating to the zImage header's
   `end` when a separate DTB is supplied.
5. The zImage at 0x8000 can overlap the kexec control page.  The
   loader now starts at 256 MB.

Current result: `kexec -e` resets to fastboot after ~6 s; no new-kernel
log.  The stub kernel has no `/proc/last_kmsg` (the TWRP/DTB boot does
not create the ram console device), and the debug UART is the headset
jack.

## Next steps

1. Get early output from the new kernel:
   - serial adapter on `ttyHSL0` (headset jack), or
   - boot the stub kernel with the Android boot image's DTB (which
     creates the ram console) so `/proc/last_kmsg` works, or
   - add an early framebuffer/display write.
2. With output, verify the decompressor entry, r2 DTB address, and
   whether `soft_restart` or the new kernel is the failure point.
3. If kexec remains unreliable, pivot to a non-kexec shutdown wipe
   (free pages, page cache, MicroPython heap, framebuffer, camd
   buffers) and keep the `wipe=` kernel mode for a future kexec path.

## Test commands

Build the wipe kernel:

```sh
TOOLCHAIN=host bash tools/build-kernel.sh --out /tmp/opencode/ko-wipe
```

Build the ramdisk (kexec + wrapper) and a test image:

```sh
KEXEC=artifacts/built/kexec-arm BUNDLE=1 \
  bash rootfs/build-stub-ramdisk.sh --force --bundle
EXTRA_CMDLINE='wipe=test' RAMDISK=artifacts/built/ramdisk-bundled \
  bash rootfs/make-boot-image.sh \
  artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
  /tmp/opencode/ko-wipe/arch/arm/boot/zImage-dtb \
  /tmp/opencode/boot-wipe.img
fastboot boot /tmp/opencode/boot-wipe.img
```

The memory-wipe branch was cut before XZ initramfs support.  Before
building the wipe image, cherry-pick `4bf26c63072`
(`hammerhead: enable XZ initramfs decompression`) from the camera branch
or pass `COMPRESS=gzip`.  With XZ the kexec image is about 16.6 MB; the
camera image without kexec is 16.1 MB.

Kexec test (after booting the stub and pushing the image/DTB):

```sh
adb push /tmp/opencode/boot-wipe.img /data/krux/boot-wipe.img
adb push /tmp/opencode/hammerhead.dtb /data/krux/hammerhead.dtb
adb shell '/sbin/kexec -l /data/krux/boot-wipe.img \
  --command-line="$(cat /proc/cmdline) wipe=1" \
  --dtb /data/krux/hammerhead.dtb; /sbin/kexec -e'
```

The DTB is extracted from the boot image's zImage-dtb (DTB magic at the
end); see `docs/memory-wipe-continue.md`.
