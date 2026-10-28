# Toolchain and host setup

Everything needed to build the rootfs, the NFC port, and eventually the kernel.

## Packages installed on the host (Ubuntu 26.04.1 LTS)

Exactly this, run as root:

```sh
aptitude install \
    binutils-arm-linux-gnueabihf \
    build-essential \
    bc bison flex \
    libssl-dev libelf-dev \
    cpio rsync file \
    gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf libc6-dev-armhf-cross \
    binutils-arm-linux-gnueabihf \
    cmake pkg-config libudev-dev \
    python3-pip python3-venv \
    patchelf device-tree-compiler
```

`aptitude` was used because `apt` reported a conflict on this machine.
`build-essential` itself was reported as conflicting, but its constituents were
already present, so the rest installed cleanly and `build-essential` was not
required.

### Why each group

| package | used by |
|---|---|
| `gcc-arm-linux-gnueabihf`, `g++`, `libc6-dev-armhf-cross` | cross-compiling anything straight into the rootfs |
| `binutils-arm-linux-gnueabihf` | **also gives `arm-linux-gnueabihf-objdump`**, without which the host `objdump` cannot disassemble ARM at all (it emits 5 lines and stops) |
| `build-essential bc bison flex libssl-dev libelf-dev cpio rsync file` | Buildroot's own build dependencies |
| `cmake pkg-config libudev-dev` | building the ported NFC transport |
| `patchelf` | fixing `RPATH`/`DT_NEEDED` on the shared objects lifted out of the ROM |
| `device-tree-compiler` | not needed — the DTB comes from `boot.img` unchanged |
| `python3-pip python3-venv` | MicroPython build tooling |

Verified present after install:

```
arm-linux-gnueabihf-gcc   15.2.0
arm-linux-gnueabihf-objdump      -> 23,249 lines of HAL disassembly
cmake cpio bison flex bc rsync patchelf dtc
rootfs/buildroot/                 full checkout
```

## Repositories

| path | what | state |
|---|---|---|
| `rootfs/buildroot/` | `buildroot.org/buildroot.git` | full checkout, ready |
| `rootfs/arm-linux-androideabi-4.9/` | AOSP gcc prebuilt | **binutils only — see below** |
| `~/Develop/src/krux-hammerhead/krux/` | clone of `eugenesan/krux` @ `unified-sighash_seedkeeper` | 275M |
| `~/Develop/src/krux-hammerhead/kruxmobileapp/` | clone of `eugenesan/kruxmobileapp`, same branch | 221M |

## ⚠ The GCC 4.9 prebuilt contains no compiler

Investigated 2026-10-18, because the initial clone appeared to be the wrong thing.

```
https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9
```

* Default branch `main` contains **only `OWNERS`**. Its HEAD commit is
  `Merge "Revert "kleaf: expose binaries.""` — the binaries were deliberately
  removed from that branch.
* 419 branches exist. Four were checked for content:
  `android11-release`, `android12-release`, `main-kernel`, `main-kernel-2026`.
  All four contain `bin/ lib/ libexec/ arm-linux-androideabi/` — **and no
  `arm-linux-androideabi-gcc` on any of them.**
* What is there: `arm-linux-androideabi-{ar,as,ld,nm,objcopy,objdump,ranlib,
  readelf,strip,addr2line,size,strings,…}` plus `cpp`, and `gcc-ar`, `gcc-nm`,
  `gcc-ranlib`, `gcov` — which are **binutils helpers, not the compiler**.
* Corroboration: `platform/prebuilts/clang/host/linux-x86/` contains a file
  literally named **`GCC_4_9_DEPRECATION.md`**.

**Conclusion: AOSP deprecated GCC 4.9 for kernel builds and moved to Clang.**
The prebuilt kept its `4.9` name for compatibility with device trees that still
reference it. This is why `build/build-kernel.sh` originally probed for
`arm-linux-androideabi-gcc` and would have failed at build time with a
confusing "no toolchain" error.

### What was fetched

`main-kernel-2026` (2025-03-28) shallow-cloned, 422 MB. Useful for **binutils**
if a legacy GCC build ever happens — `LD=ar`, `AS=as` etc. — but it will not
compile C.

### What Phase 6 actually needs

Clang, from:

```sh
git clone --depth 1 -b main-kernel \
    https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86
```

Available toolchains on that branch: `clang-r614150` (newest), `clang-r596125`,
`clang-r584948b`, `clang-r584948`, `clang-r547379`, `clang-stable`.

**Not fetched, and not needed until Phase 6.** No Clang is installed on the host.

### Honest caveat on the clang path

A 3.4 CAF kernel is *old*. `LLVM=1` is fully supported from ~4.12 onward and
was patchy well before that. Building msm-3.4 with Clang may or may not work
depending on how far back Qualcomm's tree was carried.

**The authoritative answer is whatever `device/lge/hammerhead` actually does** —
check `BoardConfig.mk` and `kernel/Android.mk` for `TARGET_KERNEL_CLANG_VERSION`
or `KERNEL_CLANG_VERSION`. If the ROM was built with Clang, we can too; if it
pins the 4.9 toolchain, then the actual GCC prebuilts must come from wherever
that manifest points, and they are not on this branch.

`build/build-kernel.sh` now probes for both and reports which it found, rather
than assuming.

## Fetching AOSP source

GitHub is blocked from this machine — `api.github.com` and
`codeload.github.com` both return 404, and `git` over SSH has no keys.
`android.googlesource.com` **is** reachable, including `git clone` over HTTPS.

`b/nfc/fetch-aosp.sh` wraps this, including the base64 decoding that
gitiles `?format=TEXT` requires:

```sh
./fetch-aosp.sh <ref> <path-in-repo> [dest]
./fetch-aosp.sh tree <ref> <dir>            # JSON directory listing
```
---

## Confirmed: the Android build system does not fetch ARM GCC 4.9

Checked the LineageOS-UL manifest (`LineageOS-UL/android`, branch `lineage-21.0`,
`default.xml`, fetched 2026-10-18) — the definitive answer to "what does
Android actually fetch?":

```xml
<!-- gcc: HOST compilers only -->
<project path="prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8" ... />
<project path="prebuilts/gcc/linux-x86/host/x86_64-w64-mingw32-4.8"      ... />

<!-- clang -->
<project path="prebuilts/clang/host/linux-x86" groups="pdk,sysui-studio" ... />
```

**There is no `prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9` project in
the manifest at all.** The only GCC prebuilts fetched are *host* compilers, used
for host-side tools, not for cross-compiling the kernel. Everything
target-ARM is Clang.

This corroborates the earlier finding three independent ways:

1. The `arm-linux-androideabi-4.9` repo has no compiler on any of its four
   content-bearing branches.
2. Its `main` HEAD is `Merge "Revert "kleaf: expose binaries.""`.
3. `platform/prebuilts/clang/host/linux-x86/` contains `GCC_4_9_DEPRECATION.md`.

So the `arm-linux-androideabi-4.9` checkout in `rootfs/` (422 MB,
`main-kernel-2026`) is of historical interest — it is the binutils a legacy
device tree would reference — but it cannot compile.

### Phase 6 fetch

```sh
git clone --depth 1 -b main-kernel \
    https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86
```

The manifest pins the AOSP remote at `refs/tags/android-14.0.0_r67`; the
`main-kernel` branch HEAD tracks the current toolchain instead. Toolchains
present: `clang-r614150` (newest), `clang-r596125`, `clang-r584948b`,
`clang-r584948`, `clang-r547379`, `clang-stable`.

Not fetched — Phase 6 is the last phase and needs nothing before it.

---

## Phase 6 toolchain — Clang (in progress)

Started 2026-10-18. Cloning:

```sh
git clone --depth 1 -b main-kernel \
    https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86 \
    prebuilts/clang-linux-x86
```

`android.googlesource.com` is the only reachable git remote from this machine;
GitHub is 404 for both `api.github.com` and `codeload.github.com`, and there
are no SSH keys.

### Why Clang and not the androideabi-4.9 prebuilt

Settled from three directions:

1. `platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9` has **no
   compiler** on any content-bearing branch (`android11-release`,
   `android12-release`, `main-kernel`, `main-kernel-2026`). Only binutils.
2. `platform/prebuilts/clang/host/linux-x86/GCC_4_9_DEPRECATION.md` exists.
   AOSP deprecated GCC 4.9 for kernels.
3. The LineageOS-UL `lineage-21.0` manifest lists **no ARM GCC project at
   all** — only host GCC (`glibc2.17-4.8`, `w64-mingw32-4.8`) and
   `prebuilts/clang/host/linux-x86`.

The `4.9` in the old prebuilt's name is historical.

### The unresolved question

**`LLVM=1` landed properly in ~4.12.** msm-3.4 is a 2013 CAF tree. Whether it
builds with Clang depends on how far back Qualcomm carried their patches, and
this is not knowable from here — only by trying.

Before spending time on a failed build, check what the device tree actually
does. In a LineageOS tree:

```
device/lge/hammerhead/BoardConfig.mk        TARGET_KERNEL_TOOLCHAIN_VERSION
device/lge/hammerhead/kernel/Android.mk     TARGET_KERNEL_CLANG_VERSION
build/soong/board/... or out/soong*.ninja    the real CC/LD/AR/NM/OBJCOPY args
```

If `BoardConfig.mk` pins `arm-linux-androideabi-4.9`, then the ROM was built
with GCC after all and the prebuilts must come from wherever that manifest
points — in which case we need the older `android-4.x` lineage of that repo,
which is a different fetch. That is the single fact that settles it.

`build/build-kernel.sh` probes Clang first, GCC second, a distro
cross-compiler third, and prints which one it found rather than dying with
"no toolchain".

---

## ⚠ CORRECTION — the kernel is built with GCC, prefix `arm-none-eabi-`

The two conclusions above are both wrong, and the reason is instructive: **I
inferred the toolchain from prebuilt *repo names* instead of reading the device
tree.**

`acroreiser/android_device_lge_hammerhead`, branch `lineage-23.2`,
`BoardConfig.mk` — the block that matters is near the bottom, under a comment
that reads "Define kernel config for inline building", and is easy to skim
past:

```makefile
TARGET_KERNEL_ADDITIONAL_FLAGS := HOSTCFLAGS="-fuse-ld=lld -Wno-unused-command-line-argument"
TARGET_KERNEL_CLANG_COMPILE := false
TARGET_KERNEL_LLVM_BINUTILS := false
TARGET_KERNEL_NO_GCC         := false
TARGET_KERNEL_CONFIG         := lineageos_hammerhead_defconfig
TARGET_KERNEL_SOURCE         := kernel/lge/hammerhead
KERNEL_TOOLCHAIN := $(TOP_PATH)/prebuilts/gcc/$(HOST_OS)-x86/arm/arm-eabi/bin
TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-none-eabi-
```

So, definitively:

| | |
|---|---|
| compiler | **GCC** — `TARGET_KERNEL_CLANG_COMPILE := false`, `TARGET_KERNEL_NO_GCC := false` |
| prefix | **`arm-none-eabi-`** |
| prebuilt | **`prebuilts/gcc/linux-x86/arm/arm-eabi`** |
| defconfig | `lineageos_hammerhead_defconfig` |
| kernel source | `kernel/lge/hammerhead` (`LineageOS/android_kernel_lge_hammerhead`) |
| linker | `lld`, via `HOSTCFLAGS="-fuse-ld=lld"` |

Also useful from the same file: `BOARD_KERNEL_IMAGE_NAME := zImage-dtb`,
`BOARD_KERNEL_CMDLINE` matches what we measured, `BOARD_RECOVERYIMAGE_PARTITION_SIZE
:= 23068672` (confirmed on-device), and `TARGET_KERNEL_SOURCE` tells us the
kernel tree to clone.

**Nothing about Clang.** The `main-kernel-2026` clone of
`prebuilts/clang/host/linux-x86` (in `prebuilts/clang-linux-x86`) is **not the
kernel toolchain**. It may still be useful for host-side tools, but it is not
what builds the kernel, and it was fetched on a wrong inference.

### What was wrong, in order

1. `build-kernel.sh` looked for `arm-linux-androideabi-gcc` — the famous AOSP
   prebuilt name. Guess, never checked against the device tree.
2. That prebuilt turned out to be binutils-only, so I concluded AOSP had moved
   kernels to Clang and rewrote the script around `LLVM=1`.
3. The actual answer was in `BoardConfig.mk` all along, naming a **third**
   toolchain: `arm-eabi` with prefix `arm-none-eabi-`.

Three steps, two wrong inferences, and a conclusion I stated confidently at
each stage. The pattern from the installer defects again: a plausible story
built on an unexamined assumption, written down as fact.

### The remaining obstacle

`platform/prebuilts/gcc/linux-x86/arm/arm-eabi` **does not exist on
android.googlesource.com**:

```
fatal: repository '.../platform/prebuilts/gcc/linux-x86/arm/arm-eabi/' not found
```

And GitHub is unreachable from this machine for both `git` and codeload
tarballs. So the exact prebuilt the ROM used is currently not fetchable here.

Note that `TARGET_KERNEL_ADDITIONAL_FLAGS` passes `-fuse-ld=lld`, so the build
wants `lld` as the linker even from a GCC toolchain — `lld` is easy to install
separately and is not the blocker.

Realistic options, in order:

1. Restore GitHub egress and clone `prebuilts/gcc/linux-x86/arm/arm-eabi`.
2. Use the distro `arm-linux-gnueabihf-gcc` already installed. **It will
   probably not compile a 2013 CAF tree** — modern GCC is much stricter — but a
   clean `olddefconfig` run is a genuine win and costs nothing.
3. An older Ubuntu in a container, for a period-appropriate GCC.

`build/build-kernel.sh` now probes `arm-none-eabi` first, then the legacy
androideabi path, then falls back to the distro compiler **and says loudly
that it is deviating**, rather than quietly building with the wrong thing.

---

## ✅ Phase 6 toolchain RESOLVED — GCC 4.9, from the LineageOS mirror

Supersedes both "Phase 6 needs Clang" and "the toolchain is not fetchable".
Both were wrong; the second was right about AOSP and wrong about the world.

```sh
git clone --depth 1 -b lineage-19.1 \
  https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 \
  prebuilts/gcc-androideabi-4.9
```

### Why the AOSP copy was empty and this one is not

AOSP moved kernel builds to Clang and **stripped the compiler** from
`platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9`. Verified four
ways: no `arm-linux-androideabi-gcc` on `android11-release`, `android12-release`,
`main-kernel` or `main-kernel-2026`; the `main` HEAD is `Merge "Revert "kleaf:
expose binaries.""`; `GCC_4_9_DEPRECATION.md` sits in the clang prebuilt; and
`git log --diff-filter=A -- bin/arm-linux-androideabi-gcc` returns nothing even
in full history — the compiler was never there, only binutils helpers.

**LineageOS still builds old CAF kernels, so it kept it.** And it carries
something the AOSP copy does not have at all:

```
bin/arm-linux-androideabi-gcc
bin/arm-linux-androideabi-gcc-4.9
bin/arm-linux-androideabi-g++
bin/arm-linux-androidkernel-gcc     <-- KERNEL-targeted build
bin/arm-linux-androidkernel-cpp
bin/real-arm-linux-androideabi-gcc
```

43 `bin/` entries versus AOSP's 23.

### Use `arm-linux-androidkernel-*`, not `arm-linux-androideabi-*`

AOSP ships two builds of the same 4.9 compiler. The userspace one bakes in
bionic assumptions that are wrong for freestanding code. For a kernel, the
`androidkernel` variant is the correct one, and `build/build-kernel.sh` now
prefers it and says which prefix it picked.

### Corrections to the record

- **"GitHub is unreachable from this machine"** — measured true earlier in the
  session (`api.github.com` and `codeload.github.com` returned 404, no SSH
  keys), but `git ls-remote` over HTTPS **does** work now. `codeload` tarballs
  still 404. Prefer `git clone --depth 1`.
- **"Phase 6 needs Clang"** — wrong. `TARGET_KERNEL_CLANG_COMPILE := false`
  in `BoardConfig.mk` said GCC all along; I had it backwards before reading it.
- **`arm-eabi`** — genuinely absent from googlesource, as established. But
  moot: the cross prefix only selects tool names, so GCC 4.9 works.

### Still open on Phase 6

Whether a 2013 CAF tree builds cleanly with 4.9 + `arm-linux-androidkernel-*`.
The vintage is right, so this is now a normal expectation rather than a hope.
If it fails, the diagnostics will tell us what the tree wants.

## ccache

Installed on the host (4.12.3), cache limit raised from the 5 GB default to
20 GB. Enabled in Buildroot via `BR2_CCACHE=y`.

Matters most for the two things that recompile the same objects repeatedly:
the Phase 6 kernel build, and iterating on the Broadcom NFC HAL port.

---

## The armel sysroot

The firmware's MicroPython and its init stub are built with the **armel**
(`arm-linux-gnueabi`) cross toolchain, statically, and against a sysroot
assembled from Debian's armel cross packages.

The packages themselves are vendored, unpacked, and ready:

| path | what |
|---|---|
| `vendor/toolchain.ubuntu/` | the `.deb` files (21 MB) |

They are not unpacked in the tree: the unpacked sysroot is 71 MB, it is
reproducible, and it does not change. So it is **built on demand** into a
tmpfs directory:

```sh
scripts/build-armel-sysroot.sh              # -> /tmp/opencode/armel-sysroot
scripts/build-armel-sysroot.sh /some/where
```

The result is byte-for-byte the tree the build expects (686 files, verified
against the one that was in use). `build-micropython.sh` calls the script
itself when the sysroot is missing, so a clean build needs nothing but the
vendored packages.

Note that the *compiler* is the host's `arm-linux-gnueabihf-gcc`; the
sysroot supplies the armel headers and the static libc it compiles
against. The two are not from the same Debian release, and that is
deliberate: it is the combination that built the binary the device runs.

---

## Verified working: GCC 4.9.x cross toolchain

Cloned to `prebuilts/gcc-androideabi-4.9` (136 MB, 43 `bin/` entries).

```
$ bin/arm-linux-androidkernel-gcc --version
real-arm-linux-androideabi-gcc (GCC) 4.9.x 20150123 (prerelease)
$ bin/arm-linux-androidkernel-ld --version
GNU ld (binutils-2.27-bd24d23f) 2.27.0.20170315
```

### What was actually tested, not assumed

| test | result |
|---|---|
| `-c -O2 -ffreestanding` | ELF 32-bit LSB relocatable, ARM, EABI5 ✓ |
| `-nostdlib -ffreestanding -o` link | ELF 32-bit LSB executable, ARM, EABI5 ✓ |
| `-march=armv7-a -mtune=cortex-a15 -mfloat-abi=softfp` | accepted ✓ |
| `as` / `ld` | 2.27.0.20170315 ✓ |
| hosted link (`-static` with default flags) | **fails** |

The hosted link fails:

```
ld: error: cannot find -lc
ld: error: cannot open crtend_android.o
```

because the prebuilt ships **bionic**, not a hosted sysroot. **That does not
matter for a Linux kernel** — the kernel is freestanding and links with
`-nostdlib`. Verified above. Worth knowing so nobody later reads the hosted-link
failure as "the toolchain is broken."

### Correction: `arm-linux-androidkernel-*` is a pure alias here

Every `arm-linux-androidkernel-*` entry in this prebuilt is a **symlink** to
the matching `arm-linux-androideabi-*` name. The wrapper does:

```python
if os.path.islink(__file__):
    compiler = os.path.basename(os.readlink(__file__))
self.real_compiler = os.path.join(compiler_path, "real-" + compiler)
```

so both names resolve to `real-arm-linux-androideabi-gcc` with identical flags.
AOSP shipped a genuinely separate kernel-targeted build historically; this
LineageOS prebuilt does not. I earlier claimed otherwise — both prefixes work
the same.

### The `#!/usr/bin/python` problem

The wrappers have a `#!/usr/bin/python` shebang and this host has only
`python3`, so every invocation died with `bad interpreter`. No sudo here, and
the shebang is absolute so a `PATH` shim cannot help.

Fixed by rewriting the shebangs of the **real files only**:

```sh
for f in *; do
  [ -L "$f" ] && continue                      # symlinks MUST be skipped
  head -1 "$f" | grep -q '^#!/usr/bin/python$' && \
    sed -i '1s|^#!/usr/bin/python$|#!/usr/bin/env python3|' "$f"
done
```

**The `[ -L ]` guard is load-bearing.** My first attempt ran `sed -i` across
every entry; on a symlink, `sed -i` replaces the symlink with a regular copy.
`arm-linux-androidkernel-gcc` stopped being a symlink, so `os.readlink` failed,
the wrapper looked for `real-arm-linux-androidkernel-gcc`, and it broke with:

```
FileNotFoundError: [Errno 2] No such file or directory
```

which looked like a missing compiler rather than a corrupted symlink.
`git checkout -- bin/` restored it. Recorded because it is the same mistake
shape as the `~/.wgetrc` one: changing something and not checking what was
actually there.

### Confirmed by the LineageOS 19.1 manifest

`LineageOS/android` @ `lineage-19.1`, `default.xml` — the platform manifest
lists exactly this prebuilt and nothing else for ARM:

```xml
<project path="prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9"
         name="LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9"
         groups="pdk,linux,arm" clone-depth="1" />
```

There is **no `arm-eabi` project in the manifest at all.** So
`KERNEL_TOOLCHAIN := .../arm/arm-eabi/bin` and
`TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-none-eabi-` in `BoardConfig.mk` name
a path the build never fetches — legacy overrides that modern
`build/soong/board` ignores. The build really does use
`arm-linux-androideabi-4.9`, which is what we have.

The manifest contains no hammerhead entries; device and kernel trees come from
the *device* manifest, which is the one to ask the ROM's author for.
