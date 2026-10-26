# Phase 0 findings — the Android reference implementation

Captured 2026-10-18 from the physical device. Raw captures in this directory.

## What the kernel is

```
Linux localhost 3.4.113-...-ge4e483b4ef6 #315 SMP PREEMPT Sep 26 2026 armv7l
```

**armv7l, 32-bit.** Not arm64. The rootfs must be armhf or armel.

Kernel cmdline:

```
console=ttyHSL0,115200,n8 androidboot.hardware=hammerhead user_debug=31
msm_watchdog_v2.enable=1 androidboot.bootdevice=msm_sdcc.1 ... uart_console=enable
```

Two things worth knowing:

- **`console=ttyHSL0,115200,n8` is already in the cmdline.** A minimal rootfs gets
  a serial console for free on the same UART the kernel already uses. That is the
  cheapest possible bring-up path — no adb needed to get in.
- The `androidboot.*` parameters are inert to a non-Android init. They do not
  have to be removed from `boot.img`.

## NFC — the whole reference stack

| | |
|---|---|
| bus | `f9967000.i2c` → `i2c-0` → `0-0077`, name `bcm2079x` |
| node | `/dev/bcm2079x`, char **10:65**, `nfc:nfc`, opens successfully |
| driver | `CONFIG_NFC_BCM2079X=y`, built into the kernel |
| service | `init.svc.vendor.nfc_hal_service` = **running**, pid 656 |
| state | `nfc.initialized=true` |

Artifacts pulled and hashed (`01-nfc-artifacts.txt`), so the port can be pinned
to exactly these bytes:

| file | size | sha256 (first 16) |
|---|---|---|
| `nfc_nci.bcm2079x.default.so` | 107608 | `5c11343eb7098ed5` |
| `android.hardware.nfc@1.0-impl-bcm.so` | 12132 | `b08d90d210f173a2` |
| `android.hardware.nfc@1.0-service` | 10724 | `3404605fcf353e71` |
| `bcm2079x-b5_firmware.ncd` | 10101 | `282fb468fe77f06c` |
| `bcm2079x-b5_pre_firmware.ncd` | 571 | `65444ecd4d213f45` |
| `libnfc-nci-20791b05.conf` | 3497 | `4782204296b892a2` |

### Two findings that materially de-risk Phase 3

**The HAL is small and nearly self-contained.** `nfc_nci.bcm2079x.default.so` is
107 KB and its `NEEDED` list is:

```
libcutils.so  liblog.so  libc++.so  libc.so  libm.so  libdl.so
```

**No binder, no `libnfc_nci`, no `libhardware`, no Android framework.** The
`linux-nfc`/`phDal4Nfc` transport and the BCM2079X register handling are inside
it. That is far less Android-coupled than a typical HAL, and it means a
non-Android port is a port of *one* library rather than a subsystem.

**It carries a dynamic symbol table.** 434 exported symbols including the whole
transport vocabulary:

```
bcmi2cnfc_client_addr        bcmi2cnfc_read_multi_packets
HAL_NfcPrmSetI2cPatch        nfc_hal_prm_spd_download_i2c_fix
nfc_hal_dm_power_mode_execute  nfc_hal_prm_spd_handle_download_complete
HAL_NfcPrmDownloadStart/Continue  HAL_NfcPowerCycle   power_control_dev
```

The `.symtab` is stripped, so internal statics are gone, but this is enough to
read the call structure without reverse-engineering from scratch.

### ~~A complication: there is a second transport~~ — RESOLVED, there is not

One string in the HAL:

```
fail close linux_cb.sock_power_control; errno=%d
```

This was read as a *socket-based power-control interface* alongside the
`/dev/bcm2079x` data path. **That was wrong.** `sock_power_control` is an
`int` — a second **file descriptor**, opened on the *same* device node:

```c
typedef struct {
    ...
    int sock;                 /* O_RDWR|O_NOCTTY on /dev/bcm2079x -- data   */
    int sock_power_control;   /* same fd by default, or a second open()    */
} tLINUX_CB;
```

It is passed straight to `ioctl()` for `POWER_CTL` and `WAKE_CTL`. There is one
device, one driver, one set of ioctls. See `b/nfc/TRANSPORT.md`.

*Corrected 2026-10-18, after fetching the HAL source. Worth noting this is the
same failure mode as the earlier "there is no modem" and "no tag detected"
conclusions: a plausible reading of an ambiguous string, written down as fact.*

### Runtime state the HAL writes

```
/data/nfc/event_log.binpb    7904 bytes   (runtime event log)
/data/nfc/param/             EMPTY DIRECTORY
```

Note `param/` is an empty **directory**, not a file — so "reproduce
`/data/nfc/param` from the plan" is wrong; there is nothing in it to reproduce.

**`event_log.binpb` is not worth decoding, and here is why.** It was assumed to
be a live record of the chip conversation. It is byte-identical —
`93ebec9b3cc8034b…` — across *every* capture, including the 2026-10-18 run
where an ISO-DEP tag was detected eight times. A file that does not change while
tags are being discovered and APDUs exchanged is not logging the chip
conversation. It is most likely a static blob shipped in the ROM.

Decoding it is cheap, so do it if curious, but do not build the port plan on
the assumption that it contains what was said to the chip. If a real
transcript is wanted, it has to be captured, not recovered.

## Display

```
/dev/graphics/fb0   mdssfb_90000
bpp                 32
virtual_size        1080,5760        (1920 x 3, triple buffered)
stride              4352
```

**`stride 4352 != 1920 * 4 = 7680/…` — it is padded.** 4352 = 1088 * 4, so each
row is 8 pixels wider than the visible area. Any blit that computes
`y * width * 4` instead of `y * stride` will be skewed. A 64-row band is captured
in `fb0-top.bin` (278528 bytes = 4352 * 64) as a reference.

No DRM (`/sys/class/drm` is empty). Plain framebuffer; `mmap` and write pixels.
`/dev/kgsl-3d0` and `/dev/ion` exist but a signing UI does not need them.

## Touch

`/dev/input/event1`, `touch_dev`, raw evdev, `INPUT_PROP_DIRECT`:

| event | code | range |
|---|---|---|
| `ABS_MT_SLOT` | 0x2f | 0–9 (10 fingers) |
| `ABS_MT_TOUCH_MAJOR` | 0x30 | 0–15 |
| `ABS_MT_POSITION_X` | 0x35 | **0–1079** |
| `ABS_MT_POSITION_Y` | 0x36 | **0–1919** |
| `ABS_MT_TRACKING_ID` | 0x39 | 0–65535 |
| `ABS_MT_PRESSURE` | 0x3a | 0–255 |

A userspace reader gets proper slot-based multi-touch directly from evdev — no
kernel input translation involved. Note the coordinate ranges are exactly
1080×1920, so they need no rescaling to the framebuffer.

## Toolchain reachability

| route | result |
|---|---|
| `github.com` over HTTPS | 200 (HTML reachable) |
| `api.github.com` | **404 for every repo** |
| `codeload.github.com` tarballs | **404** |
| `git` over SSH | no keys |
| `git` over HTTPS | prompts for credentials |
| **`android.googlesource.com`** | **works** — AOSP source is fetchable |

So: AOSP source (which is what Phase 3 needs) is reachable. Prebuilt toolchains
from GitHub are not, and must be obtained by hand.

## Live capture — what actually arrived

### NFC: the full bring-up sequence, cleanly

This is the valuable part, and it is exactly the phase Phase 0 was for:

```
libnfc_nci: nfcManager_enableDiscovery: enter; tech_mask = ff
NfcNciHal : HaiWrite: enter; len=4
NfcNciHal : nfc_hal_dm_set_nfc_wake () ASSERT
libnfc_nci: NFA_RF_DISCOVERY_STOPPED_EVT: status = 0
libnfc_nci: NFA_POLL_DISABLED_EVT: status = 3
libnfc_nci: startPolling_rfDiscoveryDisabled: enable polling
libnfc_nci: NFA_POLL_ENABLED_EVT: status = 0
libnfc_nci: nfa_dm_set_rf_field_info_ntf:  val = 0x1
NfcNciHal : HaiPreDiscover: enter
NfcNciHal : HAL_NfcPreDiscover status:0
NfcNciHal : HaiWrite: enter; len=18        <- the 18-byte vendor pre-discover blob
NfcNciHal : nfc_hal_main_send_message() ls:0x10
NfcNciHal : nfc_hal_dm_power_mode_execute () event = 0 / 1
NfcNciHal : BroadcomHalDataCallback: enter; len=4
libnfc_nci: NFA_RF_DISCOVERY_STARTED_EVT: status = 0      <- SUCCESS
NfcService: applyRouting
NfcService: applyScreenState: screenState = ON_UNLOCKED
NfcNciHal : nci_brcm_lp_timeout_cback ()
NfcNciHal : nfc_hal_dm_set_nfc_wake () DEASSERT
```

Chip woken, poll cycled off/on, RF field notified, vendor pre-discover config
sent, **discovery started with status 0**, then low-power timeout.

**But no tag was detected.** `TAG_DISCOVERED` 0, `NFA_TAG_DISCOVERY` 0,
`IsoDep` 0, and all six `event_log.binpb` samples were byte-identical
(`93ebec9b…`). So the 60-second window had no tag in the field. Either none was
held there, or it was not positioned on the coil. The bring-up half is captured;
the APDU half is not.

### Touch: real, usable data — with two surprises

Decoded from `live-touch-raw.txt`. **`input_event` is 16 bytes on 32-bit ARM**
(8-byte `timeval` + `u16 type` + `u16 code` + `s32 value`), not the 24 a
64-bit-derived reader would assume. Reading it as 24 produces garbage that
almost looks plausible — worth knowing before anyone trusts a parser.

Two things a Krux touch backend must handle:

1. **The driver emits no `SYN_REPORT`.** Zero, across 73 events. A conventional
   evdev reader waits for `SYN_REPORT` to know a frame is complete; this one has
   to consume events as they arrive. Captured touch 1 was a ~4 s drag: X swept
   219 → 7 while Y stayed 1864 → 1905.
2. **`ABS_MT_SLOT` is not emitted per touch.** Touches 1 and 2 arrived with only
   `ABS_MT_TRACKING_ID` (1, then 2); `SLOT=1` appeared only on touch 3. Code
   that assumes a `SLOT` precedes each touch will mis-track.

Coordinates land in the declared ranges (`X` 6–219, `Y` 589–1905), so no
rescaling to the framebuffer is needed.

---

## The kernel driver interface — complete

Found by pulling the AOSP source (`android.googlesource.com` is reachable;
`api.github.com` and `codeload.github.com` return 404, which is why the
prebuilt toolchain fetch failed). See `../nfc/fetch-aosp.sh`.

`external/libnfc-nci/halimpl/bcm2079x/include/bcm2079x.h` — Apache 2.0,
Broadcom, 2012. **This is the whole kernel-side contract:**

```c
#define BCMNFC_MAGIC  0xFA

#define BCMNFC_POWER_CTL           _IO(BCMNFC_MAGIC, 0x01)  /* 0=off 1=on  */
#define BCMNFC_CHANGE_ADDR         _IO(BCMNFC_MAGIC, 0x02)
#define BCMNFC_READ_FULL_PACKET    _IO(BCMNFC_MAGIC, 0x03)
#define BCMNFC_SET_WAKE_ACTIVE_STATE _IO(BCMNFC_MAGIC, 0x04)
#define BCMNFC_WAKE_CTL            _IO(BCMNFC_MAGIC, 0x05)  /* 0=off 1=on  */
#define BCMNFC_READ_MULTI_PACKETS  _IO(BCMNFC_MAGIC, 0x06)

struct bcm2079x_platform_data {
    unsigned int irq_gpio;
    unsigned int en_gpio;
    int          wake_gpio;
};
```

Six ioctls. Power and wake are GPIO-backed, which resolves the
`linux_cb.sock_power_control` string seen in the HAL — it is GPIO control
through the driver, not a separate socket protocol to reverse.

This is the single most useful result of Phase 0. The port is now bounded:
`open("/dev/bcm2079x")`, three power/wake ioctls, then `read`/`write` of NCI
frames plus `READ_FULL_PACKET`/`READ_MULTI_PACKETS` for events.

**The remaining unknown — the packet framing — is now resolved too.** The HAL
source is on AOSP at `platform/external/libnfc-nci` path `halimpl/bcm2079x`
(it is under `halimpl/`, *not* `src/hal/` where I looked first). See
`b/nfc/TRANSPORT.md` for the full contract. Short version: byte 0 is the HCIT
type, `0x10` = command (4-byte header) / `0x04` = event (3-byte header), and
the length is the **last** header byte. Confirmed against the device binary's
disassembly, so the fetched source is the same version as the HAL in the ROM.

## Still outstanding

- [ ] **an APDU exchange** — needs an app; there is no shell path to `IsoDep`.
      Not required for the port, wanted as a reference trace.
- [ ] decode `event_log.binpb` (protobuf — `93ebec9b…`, 7904 bytes). Note this
      hash is **byte-identical across every capture so far**, so it appears to
      be a static ROM-shipped log, not a live one. Re-check its provenance
      before treating it as the "actual chip conversation".
- [x] ~~fetch the transceive/framing implementation~~ — **done**, see
      `b/nfc/TRANSPORT.md`. Found at `halimpl/bcm2079x/`, not `src/hal/`.
- [x] ~~the androideabi prebuilt~~ — **resolved differently than expected**:
      it contains no compiler on any branch. Phase 6 needs Clang.
- [x] ~~`patchelf` / `device-tree-compiler` missing~~ — installed.
- [ ] **Does `/dev/bcm2079x` actually deliver whole packets?** The framing above
      assumes the driver's `READ_FULL_PACKET`/`READ_MULTI_PACKETS` ioctls block
      and behave as `my_read()` expects. Untested without Android present —
      this is now the top open question, and it is answerable cheaply with a
      ~100-line test harness.
---

## Toolchain state (2026-10-18)

Working:

```
arm-linux-gnueabihf-gcc   15.2.0     cross compiler for the rootfs
arm-linux-gnueabihf-objdump           -> 23,249 lines of HAL disassembly
                                        (host objdump had no ARM support: 5 lines)
cmake cpio bison flex bc rsync        present
rootfs/buildroot/                      full checkout
```

Also installed since: `patchelf`, `device-tree-compiler`, `python3-pip`,
`python3-venv`. (`dtc` is not needed — the DTB comes from `boot.img`.)
Full list and rationale in `b/TOOLCHAIN.md`.

### The androideabi prebuilt contains no compiler, on any branch — SUPERSEDED

An earlier version of this section said the repo was "not fetchable, only an
`OWNERS` file, Phase 6 task". That was wrong in a way that mattered: the
binaries **are** fetchable, and once fetched it turns out they are not a
compiler.

`main-kernel-2026` (2025-03-28) cloned shallow, 422 MB. Four content-bearing
branches checked — `android11-release`, `android12-release`, `main-kernel`,
`main-kernel-2026` — **none contains `arm-linux-androideabi-gcc`.** What is
there is binutils (`ar as ld nm objcopy objdump ranlib readelf strip`),
plus `cpp`, and `gcc-ar`/`gcc-nm`/`gcc-ranlib`/`gcov` — which are *binutils
helpers, not the compiler*.

Three independent confirmations:

1. No compiler on any content-bearing branch.
2. The default branch's HEAD is `Merge "Revert "kleaf: expose binaries.""`.
3. `platform/prebuilts/clang/host/linux-x86/` contains a file named
   **`GCC_4_9_DEPRECATION.md`**.

And the decisive one: the LineageOS-UL `lineage-21.0` `default.xml` manifest
lists **no ARM GCC project at all** — only *host* GCC (`glibc2.17-4.8`,
`w64-mingw32-4.8`) plus `prebuilts/clang/host/linux-x86`. What the Android
build actually fetches for ARM is Clang.

**So Phase 6 needs Clang**, not GCC 4.9:

```sh
git clone --depth 1 -b main-kernel \
    https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86
```

`build/build-kernel.sh` now probes Clang first, then GCC, then a distro
cross-compiler, and reports which it found instead of dying with "no
toolchain". Open question carried into Phase 6: `LLVM=1` is only fully
supported from ~4.12 onward, so a 3.4 CAF tree may not build with Clang. The
authoritative answer is whatever `device/lge/hammerhead` pins in
`BoardConfig.mk`.
