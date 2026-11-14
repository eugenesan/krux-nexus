# Continue memory-wipe work (superseded)

> **Historical.**  This was the hand-off note for the kexec approach.
> The kexec handoff was abandoned; the delivered firmware uses the
> kernel-only power-off marker described in
> [memory-wipe-state.md](memory-wipe-state.md).  Read that document
> first; the text below is kept for the record only.

Paste this into a fresh session.  Read `docs/memory-wipe-state.md` and
`docs/memory-wipe-kexec-review.md` first; they contain the full findings.

## Context

The project is a Krux port to the LG Nexus 5 (hammerhead, MSM8974,
Linux 3.4.113 CAF).  The goal is a high-assurance RAM wipe on shutdown
and reboot: kexec the same kernel with `wipe=1`, wipe free RAM, and
power off.

The kernel wipe is implemented and verified.  The userspace **kexec
handoff is the open blocker**: after `kexec -e` the phone stops on the
bootloader logo, and the new kernel produces no log.  Work is paused
until a serial console is available.

## Current state

- Linux branch: `airgap/memory-wipe`, head `44e3385803d`, based on the
  camera branch at `17774b5abd9`.  Do **not** rebase the camera branch;
  the outer repo points at this branch in commit `3eb0165`.
- Camera work lives on `airgap/cam-ksu-dump`; switch back to it for
  camera work.
- Verified: `wipe=test` wipes **1487 MB** and boots; `wipe=1` wipes and
  powers off.  `wipe=info`, `wipe=test:N`, and `wipe=verify` also work.
- The full bundled boot image is 23.0 MB against the 23,068,672-byte
  boot partition limit (~49 KB headroom).

## What has already been fixed

1. `CONFIG_STRICT_MEMORY_RWX=y` made `machine_kexec` fault writing the
   relocation parameters in `.text`; they are now written into the
   copied control page.
2. The V2 watchdog (`qcom,wdt@f9017000`) kept running across the jump
   and reset the SoC; `arch_kexec()` calls the driver's
   `msm_watchdog_v2_disable()`.
3. The generic ARM ATAGS offset (`0x7000` below the zImage) cannot hold
   the 176 KB DTB; the kernel now finds the DTB segment and passes its
   physical address in r2.
4. kexec-tools now truncates the Android zImage-dtb to the zImage
   header's `end` when a separate DTB is supplied, so the decompressor
   does not prefer the appended DTB (whose initrd pointer is stale after
   the old kernel freed it).
5. The zImage is loaded at/above 256 MB so it cannot overlap the kexec
   control page.
6. The wipe engine uses the **bootmem bitmap** for lowmem.  Do not go
   back to `memblock.memory - memblock.reserved`: `mem_map`, page tables
   and other bootmem allocations are not in `memblock.reserved`, and
   wiping them crashes.

## First task: get early output

The new kernel never reaches a console.  Pick one:

1. **Serial adapter on `ttyHSL0`** (headset jack).  This is the most
   direct; add `earlyprintk`/`keep_bootcon` to the kexec command line if
   needed.
2. **Android DTB + ram console.**  Pull `/dev/block/mmcblk0p19` from the
   installed Android boot image, extract its DTB, and boot the stub
   kernel with that DTB and the stub ramdisk.  If the ram console
   device is created, the next boot's `/proc/last_kmsg` contains the
   previous kernel's log.
3. **Early framebuffer write.**  Write a marker to the bootloader's
   framebuffer from the new kernel's early code.

Once output is available, check in this order:

- Did the decompressor run?  Look for `Uncompressing Linux...`.
- What are `kexec: entry=... dtb=...` (old kernel) and the new kernel's
  first lines?
- Is r2 pointing at the DTB segment, and does the DTB have the updated
  `chosen/bootargs` and `linux,initrd-start/end`?
- Does `soft_restart` return/hang, or does the new kernel start and
  crash?

## Test workflow

Build:

```sh
cd /home/user/Develop/src/krux-nexus
TOOLCHAIN=host bash tools/build-kernel.sh --out /tmp/opencode/ko-wipe
KEXEC=artifacts/built/kexec-arm BUNDLE=1 \
  bash rootfs/build-stub-ramdisk.sh --force --bundle
EXTRA_CMDLINE='wipe=test' RAMDISK=artifacts/built/ramdisk-bundled \
  bash rootfs/make-boot-image.sh \
  artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
  /tmp/opencode/ko-wipe/arch/arm/boot/zImage-dtb \
  /tmp/opencode/boot-wipe.img
```

Extract the DTB from the boot image:

```sh
python3 - <<'PY'
import struct
p='/tmp/opencode/boot-wipe.img'
d=open(p,'rb').read()
ks=struct.unpack_from('<I',d,8)[0]
page=struct.unpack_from('<I',d,36)[0]
k=d[page:page+ks]
magic=b'\xd0\x0d\xfe\xed'
best=None
for i in range(len(k)-8):
    if k[i:i+4]==magic:
        total=struct.unpack_from('>I',k,i+4)[0]
        if 0<total<=len(k)-i:
            best=(i,total)
off,total=best
open('/tmp/opencode/hammerhead.dtb','wb').write(k[off:off+total])
print('DTB', total)
PY
```

Boot the stub and test:

```sh
fastboot boot /tmp/opencode/boot-wipe.img
adb push /tmp/opencode/boot-wipe.img /data/krux/boot-wipe.img
adb push /tmp/opencode/hammerhead.dtb /data/krux/hammerhead.dtb
adb shell '/sbin/kexec -l -d /data/krux/boot-wipe.img \
  --command-line="$(cat /proc/cmdline) wipe=1" \
  --dtb /data/krux/hammerhead.dtb; /sbin/kexec -e'
```

Expected on success: the device wipes 1487 MB and powers off; USB stays
gone.  On failure it returns to fastboot or hangs on the bootloader
logo.

## Constraints and pitfalls

- `fastboot boot` is reversible; never flash.  A failed kexec can leave
  the device on the bootloader logo; power-cycle with a ~10 s press.
- The bundled image restores its own camd/firmware; rebuild the image
  after changing `/sbin/kexec` or the wrapper.
- Do not run `git clean -xdff` in the kernel tree while `wipe.c` or
  other new files are untracked; commit first.
- Keep KernelSU patches; the memory-wipe branch was created from the
  camera branch, not from `airgap/master-cleanup`.
- The boot partition is `/dev/block/mmcblk0p19`; under `fastboot boot`
  it is not the running kernel, so `/data/krux/boot-wipe.img` takes
  precedence in the wrapper.
- If kexec cannot be made reliable, pivot to a non-kexec shutdown wipe
  (free pages, page cache, MicroPython heap, framebuffer, camd buffers)
  and keep the `wipe=` kernel mode for a future kexec path.  Document
  the reduced coverage clearly.
