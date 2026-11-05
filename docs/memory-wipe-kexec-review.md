# Review — Linux 3.4 kexec RAM wipe plan

Reviewed 2026-11-05 against `docs/linux34_kexec_wipe_plan.md`, the
hammerhead tree (`airgap/cam-ksu-dump`, 3.4.113 CAF) and
`docs/memory-wipe-findings.md`. Branch for the work:
`linux:airgap/memory-wipe`.

The plan is directionally right: kexec gives a clean memblock, so the
new kernel can wipe everything the old kernel used without having to
reason about page cache, slab, or process address spaces. The following
points need to change before it can be implemented safely.

## Blockers

### 1. There is no kernel image to kexec

The running kernel does not keep the original `zImage` in memory: the
decompressor discards it and the decompressed image occupies
`0x00008000.._end`. `/proc/bootimg_kernel` does not exist in this tree
(only the unused `bootimg` symbols in an ISDN driver). The plan's
`kexec -l /boot/zImage` therefore has no source.

Options:

- **Read the boot partition** (`/dev/block/mmcblk0p19`), parse the
  Android boot header, and extract the kernel. This is the only way to
  use the *same* image without a second image. It is a partition
  dependency, but not an extra wipe partition. Under `fastboot boot`
  the running kernel may not be the one in the boot partition, so the
  loader must compare build IDs and refuse on mismatch.
- **Embed the `zImage` in the initramfs.** The image is ~7.2 MB; the
  bundled boot image has ~0.4 MB headroom and the unbundled one ~1.4 MB.
  Not viable without a much smaller wipe-only kernel.
- **Store the image in `/data/krux`.** Works in development, adds a
  data-partition dependency, and must be protected/verified.

Recommendation: read the boot partition with a build-ID/hash check, and
allow a `--kernel PATH` override for development. This preserves "same
kernel image" while making the dependency explicit.

### 2. An early hook cannot power off or shut down devices

`wipe_main()` in `start_kernel()` runs before `device_initcall`s, so:

- `device_shutdown()` does nothing (no devices are registered yet) and
  the PMIC/restart driver is not up, so `pm_power_off` is `NULL`.
- `kernel_power_off()` therefore just loops; the device stays on with
  wiped RAM.
- `msleep()` with IRQs disabled will hang.

However, the **old** kernel already calls `device_shutdown()` from
`kernel_restart_prepare()` inside `kernel_kexec()`, so Phase 8 belongs
in the old kernel (or is already done). The modem and TrustZone are not
Linux devices and are not covered.

For the early wipe, power off directly:

```c
/* CONFIG_MSM_RESTART_V2 */
scm_call_atomic1(SCM_SVC_PWR, SCM_IO_DISABLE_PMIC_ARBITER, 0);
__raw_writel(0, MSM_MPM2_PSHOLD_BASE);
```

`MSM_MPM2_PSHOLD_BASE` is a static iomap (`0xFA107000`) and is usable
this early. For restart-v1 boards, `PSHOLD_CTL_SU` is private to
`restart.c` and would need exporting.

### 3. A single `_stext.._end + 16 MB` reservation is not enough

Page tables, `mem_map`, the DTB, the initrd, the kexec control page and
the per-CPU/stack areas are not all inside `_stext.._end`. Wiping them
while executing will crash before the poweroff.

The safer formulation is the inverse: the new kernel's `memblock`
already describes all RAM and all reservations. Wipe **all memory that
is not in `memblock.reserved`**. This automatically skips the kernel,
page tables, DTB, initrd, TZ/modem carveouts and the kexec segments,
and still covers the old kernel's page cache, slab, stacks and process
pages because none of those exist in the new kernel's memblock. This
removes the fragile range arithmetic entirely.

### 4. Highmem

The device has 2 GB and `CONFIG_HIGHMEM=y` with a 3G/1G split, so about
half of RAM is not in the linear map. `__va(phys)` on those PFNs is
invalid. Wipe lowmem directly and highmem page-by-page with
`kmap_atomic(pfn_to_page(pfn))` / `kunmap_atomic()`; `FLATMEM` is on, so
`pfn_to_page()` is cheap. Wiping 1 GB one page at a time is slow but
acceptable for a final action.

### 5. `__setup` is parsed too late; use `early_param`

`__setup("wipe=")` is parsed by `parse_args()` after `setup_arch()`.
The plan wants the flag before the wipe. Use
`early_param("wipe", wipe_setup)` (parsed inside `setup_arch`) and run
`wipe_main()` after `mm_init()`, so `mem_map`, `high_memory`, the page
allocator and `kmap_atomic` are ready. IRQs are still disabled and the
scheduler is not up.

### 6. Cache maintenance

`outer_flush_all()` is not present in this tree. `flush_cache_all()`
maps to `__cpuc_flush_kern_all()` (L1/L2 on this ARMv7), which is
enough for MSM8974's caches. Add `dsb()`/`isb()` after the flush. Do not
rely on an outer-cache helper that does not exist.

### 7. Wiping reserved memory is unsafe and out of scope

`memblock.reserved` includes TZ, modem, ION/camera carveouts and the
display scanout. Wiping those can crash the modem or violate TZ. They
must stay reserved. To cover camera/ION secrets, wipe/free them in the
**old** kernel before kexec (or stop `camd` and let ION free the
buffers), not by poking carveouts from the new kernel.

### 8. Validation stage 3 is self-defeating

"Wipe all RAM, then kexec a diagnostic kernel" destroys the diagnostic
kernel's segments unless they are explicitly preserved. Either:

- wipe a test range and return (safe: free pages can be zeroed and the
  boot continues), or
- reserve the diagnostic kernel's kexec segments and skip them, or
- power off and verify with an external/cold-boot tool.

### 9. Reboot and crash coverage

The plan only covers a userspace `kexec -e` at shutdown. To cover
reboot and crashes:

- Trigger the same wipe path from **reboot** as well as shutdown. The
  simplest is a userspace `krux-wipe` helper used by both
  `PowerManager.shutdown()` and `reboot()`.
- Preload the wipe image at boot and have the kernel's restart path use
  it (`kernel_kexec()`), so a reboot initiated anywhere still wipes.
- For panics, a best-effort panic notifier can call `crash_kexec()` if
  the same image is loaded as a crash kernel. This requires a
  `crashkernel=` reservation and `CONFIG_CRASH_DUMP`/`KEXEC_ON_CRASH`;
  a watchdog reset cannot run any software and remains uncovered.
- Loading the image once at boot also solves the "where is the image"
  problem for reboot/panic, at the cost of permanently reserved memory.

### 10. Tooling and size

There is no `kexec` binary in the TWRP ramdisk and no host `kexec`.
`kexec-tools` must be cross-compiled static (or a small loader written
against `kexec_load(2)`). The boot image is close to the 23.07 MB
limit, so the loader must fit in the existing headroom; the kernel-side
`CONFIG_KEXEC` + wipe code also grows the `zImage`. Measure both before
committing to the bundled image.

### 11. `fastboot boot` mismatch

The development workflow boots an image from the host, not the
installed boot partition. A loader that reads `mmcblk0p19` would kexec
a different kernel. The build-ID check is mandatory; for development,
pass the exact `zImage` from the build tree or copy it to `/data/krux`.

## Recommended revised design

1. Kernel: `early_param("wipe", ...)`, run after `mm_init()`, wipe
   `memblock.memory - memblock.reserved`, lowmem direct + highmem via
   `kmap_atomic`, `flush_cache_all()` + barriers, direct PSHOLD poweroff.
2. Modes: `wipe=1` (wipe and power off), `wipe=test[:MB]` (wipe a
   bounded range and continue booting), `wipe=verify` (sample pages and
   log/panic on mismatch).
3. Userspace: a static `krux-wipe` loader that reads the kernel from the
   boot partition (or a supplied path), verifies the build ID, and calls
   `kexec_load` + `kexec -e`.
4. Integrate with both shutdown and reboot; preload for panic coverage
   as a second step.
5. Test order: `wipe=test:8` on a normal boot, `wipe=test` full free
   memory, then kexec + `wipe=1` + poweroff, then reboot/panic paths.

## Open decision

The kernel-image source is the one decision that changes the design:
boot partition (recommended), `/data/krux`, or a minimal wipe-only
kernel embedded in the ramdisk. The rest of the plan can proceed with
the boot-partition assumption.
