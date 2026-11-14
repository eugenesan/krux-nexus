# Memory wipe — current state (2026-11-13)

**Status: implemented, flashed and verified.**  A power-off of the running
kernel marks RAM, restarts into the recovery image, wipes every free page
at `mem_init()`, and powers off before userspace starts.  It is kernel and
RAM only: no NAND/BCB write, no userspace helper, no kexec and no second
kernel.

The earlier userspace/kexec handoff is abandoned; it is kept at the end of
this document as history.

## Design

The wipe needs a way to survive one reset without touching storage.  The
LGE kernel reserves a 1 MB persistent-RAM region at
`meminfo.bank[1].start + 1 MB`; the ram console used only the first
256 KB.  The spare tail is preserved by the bootloader across a warm
reset, so it is the one place a marker can live and still disappear with
RAM if the recovery boot never happens.

```
KRUX_MARKER_PHYS   0x100F0000
KRUX_MARKER_MAGIC  0x4b525857      ("KRXW")

struct krux_wipe_marker {
        u32 magic;
        u32 magic_inv;
        u32 seq;
        u32 crc;                  /* CRC32 of the fields above */
};
```

The marker is valid only when `magic`, `~magic` and the CRC all match.
The CRC rejects stale or partially written RAM.

`CONFIG_MEMORY_WIPE_ON_POWEROFF=y` gates the whole power-off behaviour.
`CONFIG_ANDROID_PERSISTENT_RAM=y` must stay on: it is what reserves the
region.  The ram console itself (`CONFIG_ANDROID_RAM_CONSOLE`) is not
needed and can be turned off; the region is still reserved (with no
descriptors) and the marker still lands at `0x100F0000`.

## The flow

1. `sys_reboot(LINUX_REBOOT_CMD_POWER_OFF)` is intercepted in
   `linux/kernel/sys.c` (unless this boot is already the wipe boot):
   `krux_wipe_reboot()` writes the marker and calls
   `kernel_restart("recovery")`.
2. `msm_restart_prepare("recovery")` writes the bootloader's restart
   reason `0x77665502` to IMEM — the same mechanism as
   `reboot bootloader`/`reboot recovery`.  No NAND, no BCB.
3. The bootloader boots the recovery partition.  At the top of
   `mem_init()` (`arch/arm/mm/init.c` → `wipe_main()`) the kernel sees the
   marker and sets `wipe_mode = WIPE_MODE_ALL`, `wipe_from_marker = 1`.
4. The wipe engine zeroes every free lowmem page (walking the bootmem
   bitmap) and every unreserved highmem page (walking memblock and
   `kmap_atomic_pfn`), then flushes the caches.
5. `kernel_init()` calls `wipe_poweroff()` after the PMIC/restart
   drivers are initialised.  It clears the marker, prints
   `wipe: powering off`, and uses the normal `kernel_power_off()` path.
   The direct PSHOLD write is only a fallback if that returns.

Because the marker is cleared only at the final power-off, the same boot
cannot loop: `wipe_from_marker` also stops the `sys_reboot` interception
on the wipe boot.

## What is wiped, and what is not

**Wiped:** every page the bootmem bitmap calls free, at wipe time, and
every highmem page not covered by `memblock.reserved`.  That covers the
previous kernel's free pages, the page cache, freed userspace heap,
MicroPython/camd allocations, and the like.

**Not wiped:** reserved memory.  The framebuffer/scanout, the ION/camera
carveouts and other reserved regions are never free pages, so this pass
does not zero them.  The display should be blank before shutdown; the
framebuffer scrub is still a tracked item in
[security-audit.md](security-audit.md), Phase 4.

If the recovery boot does not happen, the marker is gone with RAM, and so
are the secrets; the wipe is **not** retried on a later cold boot.  The
"recovery crashed → Android → user reboots recovery" resume path was
considered and deliberately dropped: there is nothing to persist the
request to without writing storage.

## Verified

Kernel: `CONFIG_KERNEL_XZ=y` (zImage 4.1 MB, zImage-dtb 5.3 MB),
`CONFIG_MEMORY_WIPE=y`, `CONFIG_MEMORY_WIPE_ON_POWEROFF=y`.
The final image's kernel is
`673242ee6e09e25edc658dfb6ee05e794eb821e5e35e08f2cd647a3588652805`.

Serial trace (`artifacts/built/logs/wipe-serial-2026-11-13.log`):

```
wipe: marking RAM and restarting into recovery
[390] reboot_mode restart reason = recovery
...
bootreason=recovery
...
wipe: poweroff marker found, wiping this boot
wipe: start (mode 1, test 0 MB, verify 0)
wipe: 1488 MB wiped
wipe: powering off
Powering off the SoC
```

The last line is printed from `swapper/0`, i.e. before userspace exists.
A real Krux **Shutdown** was also tested and produced the same flow.

With the USB cable connected the charger can re-power the phone after the
power-off; that is the charger, not a failed wipe.  Disconnect USB for a
true off.

## The `wipe=` modes

The engine can still be driven explicitly for testing.  It is parsed as
an early parameter in `setup_arch` and acted on at the top of
`mem_init()`:

| mode | action |
|---|---|
| `wipe=1` / `wipe=all` | wipe free RAM, then power off from `kernel_init()` |
| `wipe=test` | wipe free RAM and continue booting |
| `wipe=test:N` | wipe at most N MB of free RAM and continue |
| `wipe=verify` | wipe, sample lowmem pages, log the result, power off |
| `wipe=info` | log the memblock/bootmem layout and return |

Measured on the device:

- `wipe=info`: 1891 MB RAM, 386 MB reserved, `max_low_pfn=187392`,
  `max_pfn=524288`, `mem_map` at `0xf41000` (16 MB), 137324 free lowmem
  pages.
- `wipe=test:1/4/6/7/8`: all boot normally.
- `wipe=test` (full): 1487–1488 MB wiped, the image boots normally.
- `wipe=1`: wipes and powers off; USB stays gone.

## Building and testing

```sh
# the kernel used by the firmware (the wrappers do this on demand)
bash tools/build-kernel.sh --tree linux --out artifacts/built/ko

# a test image that wipes and continues
EXTRA_CMDLINE='wipe=test' RAMDISK=artifacts/built/ramdisk-bundled \
  bash rootfs/make-boot-image.sh \
  artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
  artifacts/built/ko/arch/arm/boot/zImage-dtb \
  artifacts/built/boot-wipe-test.img
fastboot boot artifacts/built/boot-wipe-test.img
```

The final image is the delivered one:
`artifacts/built/boot-final-sealed.img`, flashed to the recovery partition.
A power-off from Krux, TWRP or `reboot -p` is the real test.

## Implementation map

Kernel:

- `arch/arm/mach-msm/wipe.c` — marker, wipe engine, `wipe_main()`,
  `wipe_poweroff()`.
- `arch/arm/mm/init.c` — calls `wipe_main()` at the top of `mem_init()`.
- `init/main.c` — calls `wipe_poweroff()` in `kernel_init()`.
- `kernel/sys.c` — intercepts `LINUX_REBOOT_CMD_POWER_OFF`.
- `arch/arm/mach-msm/Kconfig` / `Makefile` — `CONFIG_MEMORY_WIPE`,
  `CONFIG_MEMORY_WIPE_ON_POWEROFF`.
- `arch/arm/configs/lineageos_hammerhead_defconfig` — enables both,
  plus `CONFIG_KERNEL_XZ` and `CONFIG_RD_XZ`.

Kernel branch: `airgap/cam-xz-wipe`, head `cc17c59739b`.

## History: the kexec handoff (abandoned)

The first approach loaded a second kernel with `kexec` from userspace
(`rootfs/stub/krux-wipe`, `scripts/build/build-kexec.sh`).  The kernel
side worked — `wipe=test` wiped 1488 MB, `wipe=1` powered off — but the
handoff never reached the new kernel: the old kernel printed
`Starting new kernel`, then went silent with a blank screen and the
backlight on.  A control kexec without `wipe=` hung the same way, so the
failure was in `soft_restart`/relocation, not the wipe.  Early UART from
the decompressor would have been needed to debug it.

The kernel-only marker path replaced it and is what ships.  The kexec
code (`CONFIG_KEXEC`, `machine_kexec.c`, `msm_kexec.c`, the kexec-tools
patch) remains in the tree for history; it is not used by the firmware.
