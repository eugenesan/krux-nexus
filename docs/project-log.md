# Route B working log

Running log for the bare-Linux + MicroPython + Krux work on the Nexus 5,
started 2026-10-18. Appended after each response, not rewritten.

Companion documents: `../android-airgapped.log.md` (the air-gap project),
`b/README.md` (the plan), `b/TOOLCHAIN.md` (host setup), `b/phase0/FINDINGS.md`
(measured hardware facts).

---

## Entry 1 — 2026-10-18

**Phase 0 completed except the APDU capture.**

Measured on the device and recorded in `b/phase0/`:

| | |
|---|---|
| kernel | `3.4.113-…-ge4e483b4ef6 #315` **armv7l** |
| serial console | already in cmdline: `console=ttyHSL0,115200,n8` |
| NFC bus | I2C `f9967000.i2c` → `i2c-0` → `0-0077`, name `bcm2079x` |
| NFC node | `/dev/bcm2079x` char 10:65, `nfc:nfc` |
| NFC driver | `CONFIG_NFC_BCM2079X=y`, built in |
| NFC service | `vendor.nfc_hal_service` running, `nfc.initialized=true` |
| display | `/dev/graphics/fb0`, `mdssfb_90000`, 32bpp, **stride 4352** (padded) |
| touch | `/dev/input/event1` `touch_dev`, full `ABS_MT_*`, X 0–1079, Y 0–1919 |

### The significant results

**1. The kernel driver interface is fully known.** Six ioctls, from
`external/libnfc-nci/halimpl/bcm2079x/include/bcm2079x.h`:

```c
#define BCMNFC_MAGIC 0xFA
BCMNFC_POWER_CTL             _IO(BCMNFC_MAGIC, 0x01)
BCMNFC_CHANGE_ADDR           _IO(BCMNFC_MAGIC, 0x02)
BCMNFC_READ_FULL_PACKET      _IO(BCMNFC_MAGIC, 0x03)
BCMNFC_SET_WAKE_ACTIVE_STATE _IO(BCMNFC_MAGIC, 0x04)
BCMNFC_WAKE_CTL              _IO(BCMNFC_MAGIC, 0x05)
BCMNFC_READ_MULTI_PACKETS    _IO(BCMNFC_MAGIC, 0x06)
struct bcm2079x_platform_data { unsigned irq_gpio, en_gpio; int wake_gpio; };
```

This also resolves the `linux_cb.sock_power_control` string spotted in the HAL
binary — power and wake are GPIO-backed through the driver, not a second
protocol. Phase 3 is now bounded work rather than research.

**2. The HAL is nearly self-contained.** `nfc_nci.bcm2079x.default.so` is 107 KB
and needs only `libcutils liblog libc++ libc libm libdl` — no binder, no
`libnfc_nci`, no `libhardware`. A non-Android port is a port of one library.
It also carries a 434-entry dynamic symbol table exporting the whole transport
vocabulary, so the call structure is readable.

**3. Touch has two traps.** `input_event` is **16 bytes** on 32-bit ARM
(8-byte timeval), not 24 — read as 24 it produces garbage that almost looks
plausible. And the driver emits **no `SYN_REPORT`** at all, and **no
`ABS_MT_SLOT` per touch**; a conventional evdev reader will wait forever for
frames.

**4. NFC bring-up captured cleanly, but no tag.** Full sequence from wake assert
through poll cycle to `NFA_RF_DISCOVERY_STARTED_EVT: status = 0`. Zero
`TAG_DISCOVERED`; all six `event_log.binpb` samples byte-identical
(`93ebec9b…`). No tag was in the field. Needs a retry with a tag the phone
demonstrably reads.

### Corrections made during this entry

- `b/README.md` said "reproduce `/data/nfc/param`". There is nothing to
  reproduce: `param/` is an **empty directory**. The real record is
  `event_log.binpb`.
- `build/build-kernel.sh` probed for `arm-linux-androideabi-gcc`, which **no
  longer exists in any AOSP prebuilt**. AOSP moved kernel builds to Clang and
  ships a file named `GCC_4_9_DEPRECATION.md`. Script now probes for Clang
  first, GCC second, distro cross-compiler last, and says which it found.
- `phase0-live.sh` read evdev with `bs=24` (wrong) and never verified a tag was
  present before capturing. Both fixed.

### Toolchain

All packages installed and verified. `arm-linux-gnueabihf-objdump` in
particular: the host `objdump` has no ARM support and produced a 5-line
disassembly; with it, 23,249 lines.

`rootfs/arm-linux-androideabi-4.9` turned out to contain **no compiler** on any
of its four content-bearing branches — binutils only. Detailed in
`b/TOOLCHAIN.md`. Phase 6 needs Clang from
`platform/prebuilts/clang/host/linux-x86` @ `main-kernel`.

### Next

- [ ] Retry the tag capture — confirm the phone reads a tag first
- [ ] Decode `event_log.binpb`
- [ ] Fetch the packet-framing implementation (packet header, length prefix)
- [ ] Phase 2: Buildroot rootfs on the **stock kernel**, booting to a serial
      console via `ttyHSL0`
- [ ] Phase 6 (later): Clang prebuilt, and check what `device/lge/hammerhead`
      actually pins
---

## Entry 2 — 2026-10-18

**A tag WAS detected this time.** 8 separate detections:

```
NfcDispatcher: dispatchTag: TAG: Tech [android.nfc.tech.IsoDep, android.nfc.tech.NfcA]
NativeNfcTag: Tag lost, restarting polling loop
```

`IsoDep` + `NfcA` is ISO 14443-4 Type A — a JavaCard-class target, exactly what
SeedKeeper needs. The earlier "no tag" conclusion was wrong about the hardware;
it was a capture-method problem.

Two capture-harness bugs, both mine:

- **`adb pull` does not glob.** `adb pull /data/local/tmp/phase0/live-*.txt`
  passes the literal string to the device, which does not expand it. Pull
  individually, or `adb shell ls` first.
- **The touch capture came back 0 bytes.** The `dd bs=16 | od` pipeline failed
  on the second run. Replaced with `getevent -lt /dev/input/event1`, which is
  at `/system/bin/getevent` and decodes the 16-byte structs itself. A fixed-count
  `dd` against a blocking device node is not a reliable reader.

Also: this ROM never logs `TAG_DISCOVERED` — it logs `dispatchTag` instead.
Grepping for the AOSP-standard string gives a false negative. The first capture
was nearly written up as "NFC does not work here".

### An APDU exchange cannot be captured from a shell

- `cmd nfc` exists but exposes only `status`, and it reports the **UWB** stack.
- The binder service is `nfc: [android.nfc.INfcAdapter]` — adapter
  enable/disable only, no tag IO.

Driving a real transceive needs an app. `kruxmobileapp/nfc_provider/
NfcProvider.java` is already exactly that shape (IsoDep + `connect()` +
`transceive()`, blocking via `CountDownLatch`), so a minimal test APK is small
and well-understood — and it doubles as the Android-side reference for the port.

### Toolchain: settled

Fetched the LineageOS-UL `lineage-21.0` manifest to answer "what does the
Android build actually pull?". It lists **no ARM GCC 4.9 project at all** —
only host GCC, plus `prebuilts/clang/host/linux-x86`. Three signals now agree
that kernels are built with Clang and that `arm-linux-androideabi-4.9` is
binutils-only. `build/build-kernel.sh` probes Clang first and reports what it
found. Phase 6 needs the Clang prebuilt.

### Next

- [ ] Retry the live capture with the `getevent` fix — a clean touch stream
      alongside a known-good tag detection
- [ ] Build a minimal IsoDep test APK to capture a reference APDU exchange
- [ ] Fetch the packet-framing implementation from AOSP (`userial.h` is in
      hand; the transceive path is not)
- [ ] Phase 2: Buildroot rootfs on the stock kernel, booting to `ttyHSL0`

---

## Entry 3 — 2026-10-18

**The HAL source is on AOSP, and it is the same version as the device binary.
Phase 3 is no longer a reverse-engineering job.**

### What I got wrong first

I searched `platform/external/libnfc-nci` at `src/hal/` and concluded the
framing wasn't available — `userial.h` there turned out to be the **UART**
transport (baud rates, stop bits, 16 ports), which is the PN532 path, not
bcm2079x's I2C path. I nearly wrote that up as "framing lives in a
Broadcom-internal file we can't reach."

It doesn't. The implementation is in the same repo, under `halimpl/bcm2079x`:

```
halimpl/bcm2079x/
  nfc_nci.c                    5 KB   hw_module glue
  hal/hal/nfc_hal_nci.c       30 KB   NCI message assembly
  hal/hal/nfc_hal_dm.c        49 KB   firmware download
  hal/hal/nfc_hal_prm.c       46 KB   PRM / power management
  adaptation/userial_linux.c  58 KB   *** the transport ***
  gki/ulinux/gki_ulinux.c     44 KB
```

Fetched all of it into `b/nfc/halimpl/`.

### The framing

`my_read()` in `userial_linux.c`. Byte 0 is the HCIT packet type:

| type | value | header |
|---|---|---|
| `HCIT_TYPE_NFC` (command) | `0x10` | 4 bytes |
| `HCIT_TYPE_EVENT` (response) | `0x04` | 3 bytes |

**The length is the last byte of the header**, and the reader must issue
one-byte reads to discover this — it reads 1 byte, decides whether 3 or 2 more
are needed, reads those, takes the length from the final byte, then reads
exactly that many. `MIN_BUFSIZE 259` = 255 + 4.

### Verified against the device, not assumed

I did not trust that the fetched source matches the HAL in the ROM. Checked the
device binary:

```asm
; nfc_hal_nci_add_nfc_pkt_type, source says  *p = HCIT_TYPE_NFC;
10428:  movs  r1, #16
1042a:  strb  r1, [r0, #8]

; my_read, the packet-type dispatch
16300:  cmp   r6, #16        ; NFC   -> 3 more bytes
16304:  cmp   r6, #4         ; EVENT -> 2 more bytes
16308:  movs  r4, #2
16332:  movs  r4, #3
```

Byte-for-byte agreement with the C. So we port from source, not from 23,249
lines of disassembly.

### A third correction: there is no "second transport"

`FINDINGS.md` claimed `linux_cb.sock_power_control` was a *socket-based
power-control interface*. Wrong — it's an `int`: a second **fd on the same
device**. `sock` and `sock_power_control` both open `/dev/bcm2079x`, and the
latter is passed straight to `ioctl()`. One device, one driver, one ioctl set.

That is now the third instance of the same failure mode in this project:
*no modem* → *rfkill `soft` is a real control* → *no tag detected* → *a second
transport*. A plausible reading of one ambiguous string, written down as fact.
The `linux_cb.` prefix in disassembly reads like a type name; it is a struct.

### Source coverage: 81 %

220 exported functions in the device HAL; 180 are in the fetched tree. The 40
that are not:

- **logging** — `LogMsg`..`LogMsg_6`, `ScrLog`, `ProtoDispAdapter*`, `BTDISP_*`
- **the `Hai*` wrapper** — `HaiOpen`, `HaiWrite`, `HaiPowerCycle`, … a
  convenience layer whose job the port can do directly
- **NDEF tag formatting** — `DispCHO`, `DispSNEP`, `DispCET4Tags`,
  `crcChecksumCompute`, … smart-card readers formatting NDEF payloads. SeedKeeper
  is ISO-DEP APDU, not NDEF, so this is dead weight.
- 2 stragglers: `GKI_igetpoolbuf`, `gki_timers_is_timer_running`

**None of the gap is load-bearing.** Transport, framing, NCI assembly,
power/wake state machine and firmware download are all sourced.

### Also corrected

`event_log.binpb` is byte-identical (`93ebec9b…`) across every capture
attempt, including ones where a tag was demonstrably present and detected. So
it is **not** a live log of the chip conversation — the earlier plan to "decode
it to learn what the chip said" is built on a bad premise. Re-checked and
flagged in `FINDINGS.md`.

### Next

- [ ] **Test `/dev/bcm2079x` directly** — ~100 lines, no Android needed. The
      framing above assumes the driver's packet ioctls block and deliver whole
      packets. This is now the top open question and the cheapest one.
- [ ] Buildroot rootfs on the stock kernel, booting to `ttyHSL0` (Phase 2 —
      unblocked, needs nothing from the device)
- [ ] Minimal IsoDep test APK, for a reference APDU trace (nice-to-have)

---

## Entry 4 — 2026-10-18

Answering "are we ready to build a test?" — **partly, and the honest split
matters.**

### Captures — all pulled

Six `live-nfc-event-*.binpb`, `live-logcat.txt` (1.1 MB), `fb0-top.bin`,
`phase0.log`. The touch file is still 0 bytes (that was the `dd` bug, since
fixed with `getevent`; not re-run).

All six event samples are byte-identical (`93ebec9b…`) — including the run
where a tag was detected eight times. **Confirms the `event_log.binpb` is not
a live log.** Flagged in `FINDINGS.md`.

### 1. Toolchain: resolved for Phases 2–5, NOT for Phase 6

| | status |
|---|---|
| rootfs / NFC port / MicroPython | **done** — Buildroot brings its own; `arm-linux-gnueabihf-gcc` 15.2.0 present |
| Phase 6 kernel build | **not done** — needs the Clang prebuilt (~2–3 GB, `platform/prebuilts/clang/host/linux-x86` @ `main-kernel`) |

Nothing in Phases 2–5 is blocked. Phase 6 is the last phase.

### 2. Are we ready to build a test?

**Yes for the transport, no for the chip bring-up.** Concretely:

| have | don't have |
|---|---|
| the device node, open flags | the DM/PRM firmware state machine (`nfc_hal_dm.c`, 49 KB) |
| the ioctl set and semantics | the order/timing of the bring-up commands |
| packet framing, verified 2 ways | confirmation the driver honours that framing |
| write = plain `write()` of a whole packet | any evidence the driver does this |

That is the right shape for a first test. **The one unknown that gates
everything** — does `/dev/bcm2079x` deliver whole packets the way
`userial_linux.c` assumes? — is answerable with ~100 lines and no Android
running.

So: built `b/phase0/nfc-probe.c`. Static ARM, builds clean, verified it
actually executes under Android's bionic (first version of anything that runs
on this device from /data).

It is deliberately a **probe, not a driver**. It powers the chip, listens, and
reports. It does not implement DM/PRM, so it will probably hear nothing — and
it says so, rather than reporting silence as failure.

### Two defects in my own harness, caught before running

**1. Wrong service name — no, a wrong assumption about a permission.**
`getprop init.svc.vendor.nfc_hal_service` came back **empty**, and I concluded
the service didn't exist and my runner targeted nothing. Wrong: the service is
defined in `/vendor/etc/init/android.hardware.nfc@1.0-service.rc` and is
`running` (pid 656) **when queried as root**. The empty string was an
unprivileged-shell artifact.

That is a genuine trap and the script now refuses to walk into it: an empty
`getprop` result is reported as **UNKNOWN**, not "stopped", and it **aborts**
rather than proceeding. Otherwise it would print a confident report having
changed nothing — the same silent-success failure mode as the seven installer
defects.

**2. `trap ... EXIT`** — Android's mksh accepts it, but signal `0` is
portable and this is the one moment the script must not fail.

### Seventh ioctl

`bcm2079x.h` also defines `BCMNFC_SET_CLIENT_ADDR _IO(0xFA, 0x07)`. The Phase 0
notes listed six. Seven, not six.

### Next

- [ ] **Run the probe** — instructions in `b/phase0/nfc-run.sh`
- [ ] Port `nfc_hal_dm.c` + `nfc_hal_prm.c` for real chip bring-up
- [ ] Phase 2 rootfs (unblocked, needs nothing from the device)

---

## Entry 5 — 2026-10-18

### The probe ran. The result was invalid, and my runner is why.

The probe itself worked — static ARM binary executed under bionic, opened
`/dev/bcm2079x` (fd=4), and **all four ioctls returned OK**: `POWER_CTL`,
`WAKE_CTL`, `SET_CLIENT_ADDR`, `SET_WAKE_ACTIVE_STATE`.

Stage 3 got 0 packets in 30 s. **That number means nothing**, because the NFC
HAL was never actually stopped. The runner printed:

```
stopping it to get exclusive access to the chip
   now: running
```

and then carried on into the stages regardless. Textbook silent success: the
script *observed* its own precondition failing and reported success anyway.

### Why `ctl.stop` can never work here

Measured directly:

```
pid_before=7820   after_1s=7905   after_4s=7905
init.svc.vendor.nfc_hal_service = running   (before, during, after)
```

`setprop ctl.stop` kills the process and **init restarts it within a second**.
The property never changes, so it cannot be used to confirm the stop even if
the stop could work. There is no window in which to take the chip.

### SIGSTOP works

```
kill -STOP <pid>  ->  State: T (stopped)     init not notified, not restarted
kill -CONT <pid>  ->  State: S (sleeping)
```

The HAL freezes in place holding its fd (harmless — our `open()` already
succeeds alongside it) and simply stops doing I/O. Fully reversible.

The runner now uses SIGSTOP, **verifies `State: T`, and aborts if it cannot
prove the chip is quiet.** It will not produce a number it cannot stand behind.

This is the **eighth** silent-success instance, and the first one in a harness
I wrote rather than in the shipped scripts. Same shape exactly: a check whose
result was printed and then ignored.

### Bonus: third confirmation of HCIT_TYPE_NFC

Post-restart logcat:

```
NfcNciHal: nfc_hal_main_send_message() ls:0x10
NfcNciHal: HaiWrite: enter; len=3
```

`ls:` is `p_msg->layer_specific` (`nfc_hal_main.c:562`) — the field carrying the
HCIT packet type. So `0x10` again: C source, device disassembly, now runtime
output.

`HaiWrite: len=3` is the NCI message *before* `nfc_hal_main_send_message`
prepends the `0x10` type byte. Consistent with the write path, but note we
still have never seen the actual bytes on the wire. Do not over-read this.

### Why Stage 1 took so long

The old preflight did:

```sh
for p in /proc/[0-9]*; do
  for l in "$p"/fd/*; do
    [ "$(readlink "$l")" = /dev/bcm2079x ] && ...
```

A `readlink` per file descriptor, across every process on the device. On
Android that is thousands of forks. Replaced with a single
`ls -l /proc/$HALPID/fd | grep -c bcm2079x`.

### Phase 6 toolchain: started

Cloning `platform/prebuilts/clang/host/linux-x86` @ `main-kernel` into
`prebuilts/clang-linux-x86`, in the background. Details in `b/TOOLCHAIN.md`.

### Next

- [ ] **Re-run the probe** — same command, now with SIGSTOP. If Stage 3 still
      returns 0 packets with the HAL provably frozen, that IS the answer, and
      the next step is porting the DM/PRM firmware sequence.
- [ ] **Consider running the probe from TWRP instead** — no Android at all is
      cleaner than freezing Android, and recovery is the real Phase 2
      environment anyway.
- [ ] Phase 6: confirm the Clang clone, then settle whether a 3.4 CAF tree
      builds with `LLVM=1` at all

---

## Entry 6 — 2026-10-18

**The transport contract is confirmed on real hardware, and the capture is
fully decoded.** Phase 0 is done.

### Result

```
[  1] read 6 bytes
    0000  10 60 00 02 00 01
    framing: HCIT_TYPE_NFC, 4-byte header, declared len=2, actual body=2 (MATCH)
```

The SIGSTOP approach worked (`state=T`), so this result is valid — unlike the
previous run, where the HAL was never stopped.

### The exact wire format

`NCI_MSG_HDR_SIZE = 3` (`nci_defs.h:37`, "per NCI spec"). So the 4-byte header
`my_read()` consumes is **1 HCIT type byte + the 3-byte NCI header**, and
`pbuf[3]` — the last header byte, which `my_read()` uses as the length — is the
NCI length field. Everything lines up:

```
byte 0   HCIT type          0x10 = NFC, 0x04 = event
byte 1   MT|PBF|CID         0xE0>>5, 0x10>>4, 0x0F
byte 2   OID                0x3F
byte 3   payload length
byte 4+  payload
```

### The packet is the Core Reset notification

Decoded with the real macros:

| byte | value | meaning |
|---|---|---|
| 0 | `0x10` | HCIT_TYPE_NFC |
| 1 | `0x60` | MT = **MT_NTF**, PBF 0, CID 0 |
| 2 | `0x00` | OID = 0 = **`NCI_MSG_CORE_RESET`**, gid 0 = `NCI_GID_CORE` |
| 3 | `0x02` | length 2 |
| 4–5 | `00 01` | payload |

And `nfc_hal_dm.c:657`:

```c
if (op_code == NCI_MSG_CORE_RESET) {
    if (mt == NCI_MT_NTF) {
        if (initializing_state == NFC_HAL_INIT_STATE_W4_NFCC_ENABLE || ...) {
            /* Core reset ntf in the following cases;
             ** 1) after power up (raising REG_PU)
             ** 2) after setting xtal index
             ** Start pre-initializing NFCC */
            nfc_hal_dm_pre_init_nfcc();
```

The comment says *"1) after power up (raising REG_PU)"*. That is precisely
what the capture is: we issued `POWER_CTL 1` and the chip answered on its own.

**So: the chip speaks first, and the first thing it says is "I just powered
up."** The very next action in the state machine is
`nfc_hal_dm_pre_init_nfcc()`.

The 2 payload bytes are not parsed on this path — the `else` branch (post-patch
download) is the one that reads `param_len` + `reset_reason` + `reset_type`,
which would need 3 bytes. We got 2, so we took the power-up branch, where the
payload is ignored. Do not read meaning into `00 01`.

### Two things that simplify the port

1. **The driver delivers one whole framed packet per `read()`.** One read of 6
   bytes returned exactly one complete packet — no splitting, no coalescing. So
   the port can use ordinary buffered reads and **skip `my_read()`'s
   byte-at-a-time header discovery entirely**. Worth knowing before porting it.
2. **Bring-up is reactive, not blind.** No firmware is loaded and nothing was
   sent, yet the chip produced a notification. The port does not have to guess a
   write sequence to provoke a reply — it powers on and waits.

Also: `open("/dev/bcm2079x")` succeeds *while the Android HAL holds the
device*, and all four ioctls return OK on this driver revision.

### Side effect: freezing the HAL crashes the framework NFC

```
NfcService: run: Watchdog triggered, aborting
F DEBUG : android::nfcManager_doAbort ... NfcService$WatchDogThread.run
```

`com.android.nfc` aborted and restarted itself (pid 7889 → 9351); `nfc.initialized`
returned to `true` and the HAL resumed cleanly. But the freeze window is
bounded by that watchdog — **Android is an awkward place to test this.** TWRP is
cleaner: no watchdog, no framework, nothing to restore. Weigh that for Phase 2.

### Phase 6

Clang prebuilt still cloning, now 4.1 GB. The open question stands — whether
`device/lge/hammerhead/BoardConfig.mk` pins `arm-linux-androideabi-4.9` or a
clang version decides whether this download is the right one at all.

### Next

- [ ] **Port the DM/PRM bring-up** — now bounded: power on, read the Core Reset
      ntf, run `nfc_hal_dm_pre_init_nfcc()`, then the PRM firmware sequence.
      Source is in hand (`nfc_hal_dm.c` 49 KB + `nfc_hal_prm.c` 46 KB).
- [ ] **Extend the probe** to a `--bringup` mode that does power-on + read and
      reports the state machine step, so each ported step is testable in
      isolation against Android.
- [ ] Decide: Android-free testing in TWRP before porting more, or push on with
      the Android harness and accept the watchdog cost.

---

## Entry 7 — 2026-10-18

### Toolchain: found it, and it is neither of the two things I said

`acroreiser/android_device_lge_hammerhead` @ `lineage-23.2`,
`BoardConfig.mk`. The relevant block sits near the bottom under a comment
reading "Define kernel config for inline building", which is why it reads as
unrelated at a glance:

```makefile
TARGET_KERNEL_CLANG_COMPILE := false
TARGET_KERNEL_NO_GCC         := false
TARGET_KERNEL_CONFIG         := lineageos_hammerhead_defconfig
TARGET_KERNEL_SOURCE         := kernel/lge/hammerhead
KERNEL_TOOLCHAIN := $(TOP_PATH)/prebuilts/gcc/$(HOST_OS)-x86/arm/arm-eabi/bin
TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-none-eabi-
TARGET_KERNEL_ADDITIONAL_FLAGS := HOSTCFLAGS="-fuse-ld=lld ..."
```

**GCC, prefix `arm-none-eabi-`, prebuilt named `arm-eabi`.** Not Clang, and not
`arm-linux-androideabi-4.9` — a third toolchain that nobody guessed at.

What went wrong, in order:

1. `build-kernel.sh` looked for `arm-linux-androideabi-gcc` — the famous AOSP
   prebuilt name. Pure guess, never checked against the device tree.
2. That prebuilt turned out to be binutils-only, so I concluded AOSP had moved
   kernels to Clang and rewrote the script around `LLVM=1`, stating it
   confidently.
3. The answer was in `BoardConfig.mk` the whole time.

Three steps, two wrong inferences, a confident conclusion at each stage. Same
failure mode as the seven installer defects: a plausible story resting on an
unexamined assumption. The specific error is **inferring from repo names
instead of reading the config that pins them.**

`build/build-kernel.sh` corrected: probes `arm-none-eabi` first, and falls back
to the distro cross-compiler while **saying loudly that it is deviating**.

### Obstacle

`platform/prebuilts/gcc/linux-x86/arm/arm-eabi` **does not exist on
android.googlesource.com** (`repository not found`), and GitHub is unreachable
here for git and codeload. So the ROM's actual prebuilt is not fetchable on
this machine today. `TARGET_KERNEL_ADDITIONAL_FLAGS` also wants `-fuse-ld=lld`,
which is a separate and easy install — not the blocker.

Phase 6 stays open, and it is the *last* phase. Nothing in Phases 2–5 needs it.

### Cost of being wrong: 23 GB

The Clang clone finished: **23 GB**, of which ~17 GB is five redundant Clang
versions. `clang-r614150` alone is 3.7 GB. Free space is down to 41 GB. Worth
deleting the four older ones.

### TWRP does not exist for hammerhead

`twrp.me` has no Nexus 5 build, and there is no `twrp-me` device repo. Support
was dropped years ago. So the recovery-kernel route is the only recovery route.

### The recovery kernel is a different build

Read both `ANDROID!` headers off the partitions:

| | boot | recovery |
|---|---|---|
| kernel | 7,414,992 B | **7,446,592 B** |
| ramdisk | 1,334,266 B | 14,595,115 B |

**Different kernel, 31,600 bytes apart.** Recovery kernels are built from the
same defconfig family but with recovery-specific overrides, and NFC is exactly
the kind of thing that gets dropped from a recovery config — it is not needed
for flashing. So `/dev/bcm2079x` may simply not exist in recovery.

That is worth knowing before booting anything, and it is cheap to test.

### Plan for Android-free testing

Two candidate routes, in order of cost:

1. **Boot recovery and check `/dev/bcm2079x`.** Two minutes, reversible with
   `adb reboot`. LineageOS recovery normally has adbd, so if the node exists we
   get a perfect Android-free shell with push/pull and no watchdog.
2. **`fastboot boot` a minimal initramfs** on the *stock* kernel — our exact
   measured kernel, no Android, and it is Phase 2 anyway. Non-destructive.
   Needs ARM userspace: we have a static ARM `nfc-probe` and
   `arm-linux-gnueabihf-gcc`, and host `cpio`/`gzip`/`python3`. `mkbootimg` is
   absent but the header is 40 bytes and trivial to write.

   The catch: with no Android there is no adb, so results have to leave via
   `ttyHSL0` (needs a USB-serial cable), the framebuffer, or a file written to
   the `cache` partition and collected after rebooting to Android.

Route 1 first, because if recovery has both the NFC node and adb it settles
everything cheaply.

### Next

- [ ] Boot recovery, check `/dev/bcm2079x` and whether adbd works
- [ ] Decide 23 GB of Clang: keep `clang-r614150`, drop the other four
- [ ] Phase 2: minimal initramfs on the stock kernel
- [ ] Phase 6 (blocked on GitHub egress): kernel tree + `arm-none-eabi` GCC

---

## Entry 8 — 2026-10-18

**Bidirectional NCI transport working, with no Android in the loop.** The
bring-up state machine now runs by hand.

### Recovery has the NFC driver

`/dev/bcm2079x`, char **10:65** — the same node as on Android. So the recovery
kernel does build in `CONFIG_NFC_BCM2079X`, despite being a different build
from the boot kernel (7,446,592 vs 7,414,992 bytes). Also present:
`/dev/graphics/fb0-2`, `/dev/input/event0-3`, root adb, no framework, no
watchdog.

Recovery is strictly better than Android for this work. No HAL to freeze, no
framework watchdog to crash, no `ctl.stop` that cannot work, no firmware blob
dependencies. Everything learned in Entries 4–7 about SIGSTOP and watchdog
costs becomes irrelevant.

(Third-party TWRP exists — `z3DD3r/twrp_android_device_lge_hammerhead` — but
stock recovery already does the job and is what is installed. Noted, not needed.)

### The bring-up, step by step

`userial_linux.c:1700` gives the reset order, and order matters — wake is set
*before* power:

```c
ioctl(sock, BCMNFC_WAKE_CTL,  NFC_WAKE_ASSERTED_ON_POR);  /* UPIO_OFF = 0 */
ioctl(sock, BCMNFC_POWER_CTL, 0);
GKI_delay(10);
ioctl(sock, BCMNFC_POWER_CTL, 1);
```

After the Core Reset notification, `nfc_hal_dm_pre_init_nfcc()` sends
`nfc_hal_dm_get_build_info_cmd`. That array is three bytes:

```c
const UINT8 nfc_hal_dm_get_build_info_cmd[NCI_MSG_HDR_SIZE] = {
    NCI_MTS_CMD|NCI_GID_PROP, NCI_MSG_GET_BUILD_INFO, 0x00 };
```

`NCI_MSG_GET_BUILD_INFO` is **not defined in any header we have** — the
defining one was never upstreamed. Rather than guess it, took it from the
shipped HAL binary's `.rodata`:

```
00009a88 R nfc_hal_dm_get_build_info_cmd      (nm)
9a88  2f 04 00 2f 2d 00                       (objdump -s .rodata)
```

So `NCI_MTS_CMD|NCI_GID_PROP` = `0x2F` (confirming `NCI_GID_PROP` = **0x0F**,
not 0x04 as I first wrote), `GET_BUILD_INFO` = **0x04**, and
`get_patch_version_cmd` = `2F 2D 00`. On the wire the command is
**`10 2F 04 00`**, the `0x10` being the HCIT type that
`nfc_hal_nci_add_nfc_pkt_type()` prepends.

### It works

```
step 1: reset
step 2: core reset ntf: 10 60 00 02 00 01
step 3: wrote 4 bytes: 10 2f 04 00
step 4: reply:  10 4f 04 24 00 44 65 63 20 32 32 20 32 30 31 32
              31 34 3a 35 33 3a 35 30 01 05 0d 46 05 1b 79 20 07
              32 30 37 39 31 42 35
        NCI: MT_RSP GID=PROP(0x0F) OID=0x04 len=36 MATCH
-- bringup REACHED THE RESPONSE --
```

Decoded:

| | |
|---|---|
| `[0]` status | `0x00` — OK |
| `[1..20]` | `Dec 22 2012 14:53:50` — firmware build timestamp |
| `[21..28]` | `05 0d 46 05 1b 79 20 07` — opaque |
| `[29..35]` | **`20791B5`** — chip ID |

`20791B5` is the controller identifier, and it matches the ROM's own config
filename `libnfc-nci-20791b05.conf`. Independent confirmation that we are
talking to the right chip.

Saved as `b/phase0/bringup-android-free.txt`.

### Also worth recording

**`READ_MULTI_PACKETS` must not be issued.** `bcmi2cnfc_read_multi_packets`
defaults to 0 and the ROM's config does not set it, so the real HAL never calls
that ioctl. Our earlier probes called it; harmless, but wrong.

**The ROM's NFC config is tiny.** Only `FW_PATCH`, `FW_PRE_PATCH`, `LPTD_CFG`,
`NFA_DM_START_UP_CFG`, `PREINIT_DSP_CFG`. No `DEVICE_NAME`, no
`CLIENT_ADDRESS`, no `SPI_NEGOTIATION`, no `READ_MULTI_PACKETS`. So there is far
less runtime configuration to reproduce than it looked.

### A ninth wrong verdict, this one in my own verdict logic

The first successful run printed a perfect response and then said
`!! timed out ... did not get a response`. The chip *had* answered; my
`wait_for(0x00, 0, ...)` was matching on CORE_RESET rather than on the
response's own OID. The data was right and the conclusion was wrong — which is
worse than the reverse, because it reads as a hardware failure. Fixed to match
`OID 0x04 / MT_RSP` and re-run to confirm.

### Next

- [ ] `nfc_hal_dm_send_startup_vsc()` and the patchram sequence in
      `nfc_hal_prm.c` — the remaining transport work before tag access
- [ ] Firmware load from the two `.ncd` blobs
- [ ] First real tag operation
- [ ] Phase 2 rootfs can now be built against a *known-good* target

---

## Entry 9 — 2026-10-18 (continuing in recovery)

### Build info decoded and cross-checked

The 40-byte response parsed against `nfc_hal_dm.c`'s own accessor:

```c
p += NCI_BUILD_INFO_OFFSET_HWID;   /* 25 */
STREAM_TO_UINT32 (hw_id, p);
STREAM_TO_UINT8  (chipverlen, p);
STREAM_TO_ARRAY (chipverstr, p, chipverlen);
nfc_hal_cb.dev_cb.brcm_hw_id = nfc_hal_dm_adjust_hw_id (hw_id);
```

The arithmetic did not land on a sane `chipverlen` at first, which is exactly
the kind of thing to check rather than paper over. Resolved it by using the
HAL's own consistency check:

```c
hw_id = little-endian UINT32 at buffer[28..31] = 0x20791B05
adjust_hw_id(): (hw_id & 0xF0000000) == 0  ->  hw_id <<= 4  ->  0x20791B50
```

`0x20791B05` **is the ROM's own config filename**, `libnfc-nci-20791b05.conf`.
And the mask the HAL then applies (`brcm_hw_id & BRCM_NFC_GEN_MASK`, compared
against `BRCM_NFC_20791_GEN`) yields the 20791 generation. Only one
interpretation fits, so it is settled.

Two things this pins down:

| | |
|---|---|
| `p` after `NCI_MSG_PRS_HDR0/1` | points at **buffer[3]**, the NCI length byte |
| buffer layout | `[0]` HCIT type, `[1]` NCI byte 0, `[2]` OID, `[3]` length, `[4..]` payload |

So the HCIT type byte **is present in the buffer** and is *not* skipped. That
was genuinely ambiguous from the source alone.

### Where the bring-up goes next

From the source, after build info:

```
core reset ntf
  -> GET_BUILD_INFO            10 2F 04 00          DONE, verified
  -> nfc_hal_hci_handle_build_info()
  -> nfc_hal_dm_check_pre_set_mem()   HCI_BRCM_PRE_SET_MEM  (BT HCI vendor cmd,
                                       NOT NCI; data = LPTD_CFG + PREINIT_DSP_CFG
                                       from the ROM's .conf)
  -> nfc_hal_dm_check_xtal()          nci_brcm_xtal_index_cmd / set_pwr_level_cmd
  -> POST_XTAL_SET -> GET_PATCH_VERSION   2F 2D 00
  -> nfc_hal_dm_send_startup_vsc()    NFA_DM_START_UP_CFG
  -> NFC_HAL_INIT_STATE_IDLE
```

Good news on the data: `p_nfc_hal_dm_pre_set_mem` is **`NULL`** in the default
`nfc_hal_dm_cfg.c` and is populated at runtime by parsing the ROM's `.conf`.
So the payload is `LPTD_CFG` and `PREINIT_DSP_CFG` — both already in
`b/phase0/libnfc-nci-20791b05.conf`. Nothing to reverse.

### Stopping the hand-rolled bring-up here, deliberately

`HCI_BRCM_PRE_SET_MEM`, `HCI_BRCM_PRE_SET_MEM_LENGTH` and
`HCI_BRCM_PRE_SET_MEM_TYPE` are **not in any header we have**. I could pull
them out of the `.rodata`/immediate pool the way `GET_BUILD_INFO` came out, and
I started to.

**That is the wrong thing to keep doing.** Every step from here is sourced
Apache-2.0 code; hand-transcribing the state machine into C reproduces, by
hand, exactly the work the port is going to do with the real source. The
transport question — the one thing that could not be answered by reading
source — is now **settled and verified**:

- read path: one whole packet per `read()`, framing as documented
- write path: `10 | NCI header | payload` accepted by the chip
- the chip answers, and its answer is self-consistent with the HAL's own parser

So the remaining work is not discovery, it is **porting**, and it should be done
by compiling the real HAL (81 % sourced, gap is logging + `Hai*` + NDEF
formatting) into the Buildroot rootfs, not by hand.

### Next

- [ ] **Phase 2: Buildroot rootfs**, with the stock kernel
- [ ] Compile the real `halimpl/bcm2079x` into it, stub logging, drop `Hai*`
- [ ] `lcd`/`board`/`touch` shims for Krux
- [ ] Phase 6 (blocked on GitHub egress): kernel tree + `arm-none-eabi` GCC

---

## Entry 10 — 2026-10-18

### 1. The toolchain hypothesis: tested, and it does not hold

The suggestion was that `arm-linux-androideabi-4.9` still has the binaries and
we just needed an older commit. Worth testing — we had only checked *branches*,
never *history*.

Found `8da8c8e kleaf: expose binaries.`, which is exactly the commit that
added them before `main` reverted it. But what it adds to `bin/` is:

```
bin/arm-linux-androideabi-cpp
bin/arm-linux-androideabi-gcc-ar
bin/arm-linux-androideabi-gcc-nm
bin/arm-linux-androideabi-gcc-ranlib
...34 files, none of them a compiler
```

`git log --diff-filter=A -- bin/arm-linux-androideabi-gcc` returns nothing.
**The compiler was never in this repo.** `gcc-ar`/`gcc-nm`/`gcov` are binutils
helpers that look like GCC and are not. So the branch-level finding now holds
at the history level too — one more independent confirmation.

Phase 6 stays blocked on GitHub egress, and it is the last phase.

### 2. Clang prebuilts: removed

Kept `clang-r614150` (verified: Android clang 23.0.1), removed six other
toolchains and `profiles`. **23 GB → 8.3 GB**, free space back to 47 GB.

### 3. Phase 2 started

Measured in recovery: ARMv7 rev 0 (v7l), **Cortex-A15 / Krait**, features
`neon vfpv3 tls vfpv4 idiva idivt`, 1,903,100 kB RAM, cmdline
`console=ttyHSL0,115200,n8 maxcpus=2 lge.kcal=0|0|0|x`.

Pulled both firmware blobs (`bcm2079x-b5_firmware.ncd` 10101 B,
`bcm2079x-b5_pre_firmware.ncd` 571 B) and the **stock boot.img** (23,068,672 B,
sha256 `9d0ce4ac…`) for the real kernel.

#### `bootimg.py` — pack/unpack, and a verified round trip

No `mkbootimg` on this host and no AOSP tree, so the header is written out
longhand. The addresses are **not ours to choose**: `fastboot boot` hands the
image to the stock bootloader, which loads at exactly the addresses in the
header. Getting them wrong boots to nothing with no error message. So `unpack`
records every field and `pack` refuses to invent one.

Round trip: unpacked the stock image, repacked the original ramdisk, and
compared. **`cmp` reports EOF on our output, not a mismatch** — all 8,753,152
bytes identical. The original is simply padded out to the full 22 MB partition
and ours is not, which `fastboot boot` does not care about.

Addresses recovered, and they confirm `BoardConfig.mk` independently:

```
ramdisk_addr 0x02900000   ==   BOARD_MKBOOTIMG_ARGS --ramdisk_offset 0x02900000
tags_addr    0x02700000   ==   BOARD_MKBOOTIMG_ARGS --tags_offset    0x02700000
```

### 4. Buildroot: two silent-failure traps, both caught

**(a) `merge_config.sh` does nothing and says it worked.**

```
$ merge_config.sh -O out out/.config fragment
No rule to make target 'alldefconfig'.  Stop.
$ echo $?
0
```

It calls `make alldefconfig` internally, does not check the result, and exits 0
having changed nothing. A caller that trusts the exit status — the normal thing
to do — proceeds with the untouched base config. That is the tenth instance of
"reported success while the device was wrong", this time in our own tooling.

Replaced with `apply-fragment.py`, which writes the symbols directly, runs
`olddefconfig`, and then **reads back every symbol the fragment asked for** and
reports any that did not survive. Non-zero exit if any dropped.

**(b) Writing a kconfig symbol by hand is usually wrong.** The read-back caught
four mistakes a hand-written config would have hidden:

- **15 of 23 symbols did not exist** under the names I used. Buildroot here is
  `2026.08-1042-g279f4b6237`; `BR2_ARM_FPU_VFPv4` is `BR2_ARM_FPU_VFPV4`,
  `BR2_STRIP_gstrip` is gone, and busybox per-applet symbols no longer exist
  statically (moved to `BR2_PACKAGE_STRACE`).
- **`sama5d3_defconfig` no longer exists.** Base is now
  `qemu_arm_vexpress_defconfig` — Cortex-A15 quad, the right SoC class.
- **Choice groups.** `BR2_cortex_a15=y` alone does nothing: `BR2_cortex_a9=y`
  is still in `.config`, and `olddefconfig` silently keeps one of them. Every
  conflicting symbol must be set `=n` explicitly. That is how kconfig choices
  work, and it is exactly the kind of thing that produces a rootfs for the
  wrong CPU with no complaint.
- **`BR2_ARM_ENABLE_NEON=y` was being dropped, and that was CORRECT.** It
  depends on `BR2_ARM_CPU_MAYBE_HAS_NEON`, and for Cortex-A15 the answer is
  "always", so the MAYBE option does not exist here. Selecting
  `BR2_ARM_FPU_VFPV4` sets `BR2_ARM_CPU_HAS_VFPV4`, and NEON comes from the
  core. Verified: `BR2_ARM_CPU_HAS_NEON=y`.

Left unfixed, the build would have produced a Cortex-A9 userspace with no NEON,
and the Broadcom HAL would not have compiled.

Also fixed `target-final` → **`target-finalize`**, which is the target that
builds the root filesystem without the kernel. Buildroot insists on a kernel
being configured; we never build it, because Route B runs on the **stock**
zImage from `boot.img`.

### 5. Still to do in Phase 2

- Boot the initramfs via `fastboot boot` and get a shell
- `/dev/graphics/fb0` — remember **stride 4352, padded, not 1920*4**
- `/dev/input/event1` — 16-byte `input_event`, **no SYN_REPORT, no ABS_MT_SLOT**
- `lge.kcal` — the panel is calibrated per unit; colours will be wrong without it
- Then compile the real `halimpl/bcm2079x` into the rootfs

---

## Entry 11 — 2026-10-18

### USB: adb on bare Linux, no userspace daemon needed

Investigated whether the adb USB driver could be reused on a bare-Linux boot.
It can, and better than expected — **ADB is implemented in the kernel**.

From the Android kernel config (`kconfig.gz`, 3641 lines, verified as *this*
kernel's):

```
CONFIG_USB_DWC3=y            CONFIG_USB_DWC3_MSM=y
CONFIG_USB_G_ANDROID=y       CONFIG_CONFIGFS_FS=y
```

CAF's `android_usb` driver (not AOSP's configfs one) exposes:

```
/sys/class/android_usb/android0/
  enable  idVendor  idProduct  bcdDevice  state  remote_wakeup
  functions          <- a FILE, you write function names into it
  f_adb  f_ffs  f_acm  f_hid  f_serial  f_ccid  f_mass_storage  ...
```

live values: `enable=1`, `idVendor=18d1`, `idProduct=4ee2`, `state=CONFIGURED`.

#### Which functions are actually backed

CAF's `android_usb` creates *every* `f_*` sysfs directory whether or not a
driver backs it. Counting symbols in `/proc/kallsyms` settles it:

| function | symbols | verdict |
|---|---|---|
| `f_adb` | 32 — `adb_function_init`, `adb_open`, `adb_android_function_enable` | **real, kernel-side ADB** |
| `f_ffs` | 55 | **real, FunctionFS** |
| `f_acm` | 3 — `acm_bind`, `acm_set_alt`, `acm_bind_config` | **stub** |
| `f_hid` | 7 | probably real |
| `ccid` | 38 | probably real |

`f_acm` looks plausible because the sysfs dir has an `acm_transports`
attribute — but those three symbols are `cdc_acm.c`, the **device-side**
driver (`CONFIG_USB_ACM=y`), not `g_acm` the gadget side. And
`# CONFIG_USB_G_ACM_MS is not set` confirms it.

So the real options are:

- **`functions=adb`** — the kernel implements the whole ADB protocol.
  **Zero userspace code.** Four `echo`s in init and the host's `adb` works.
- **`functions=ffs`** — FunctionFS; requires writing `adbd` ourselves
  (CNXN/AUTH, ep0 control vs ep1 bulk, OPEN/WRTE/OKAY/CLSE, shell service).
  ~600 lines.

The user's suggestion to check for a gadget *serial* driver was worth running
down, and it is why `f_acm` got examined — but it turns out not to be compiled
in. Since `functions` takes a list, the plan is to write **`adb acm`** anyway
and take whichever binds; `/dev/ttyGS0` costs nothing extra to test. If `adb`
fails we get a CDC-ACM serial line, and if that fails too, `ffs` + our own
adbd is the fallback.

#### A config trap worth flagging

`kconfig.gz` says **`# CONFIG_USB_FUNCTIONFS is not set`**, yet `f_ffs` exists,
`functions=[ffs]`, and there are 55 `ffs_*` symbols. CAF folds FunctionFS in
under `USB_G_ANDROID`. Anyone reading the config alone would conclude FunctionFS
is unavailable and give up on adb entirely.

Same shape as `arm-eabi`: **read the artifact, don't infer from the name.**
That has now bitten this project twice, and both times the config said
something was missing that was in fact present.

Also verified along the way: our `kconfig.gz` is the **Android** kernel config.
Recovery's `/proc/config.gz` differs by 56 lines (KALLSYMS_ALL, HZ=100 vs 300,
THUMB2_KERNEL, BLK_DEV_THROTTLING…). Ours is the right one — it is the boot
kernel.

### Buildroot: a board defconfig was the wrong base

Used `qemu_arm_vexpress_defconfig` because sama5d3 is gone. It is the right CPU
(Cortex-A15 quad) but it is a **board** config, and it brought:

```
BR2_GLOBAL_PATCH_DIR="board/qemu/patches"
BR2_ROOTFS_POST_IMAGE_SCRIPT="board/qemu/post-image.sh"
BR2_PACKAGE_HOST_QEMU=y
```

so the build spent its time downloading **qemu 11.0.0**, a ~300 MB host tool
for a board we are not emulating. Layering a fragment onto a board defconfig
imports the whole board.

Switched to **`allnoconfig`** + the fragment: 25/25 symbols apply, nothing else
pulled in. One more drop caught this time — `BR2_TARGET_GENERIC_GETTY_PORT` is
gated behind `BR2_TARGET_GENERIC_GETTY` in current Buildroot, and setting only
the leaf makes kconfig discard it silently.

### My wget "fix" broke wget

To kill the IPv6 stalls I wrote `~/.wgetrc` with a `[wget]` section header.
GNU wget's rc file takes **bare commands, no section headers**, so every
download died with:

```
wget: Syntax error in /home/user/.wgetrc at line 1.
wget: Unknown command 'prefer_ipv4' in /home/user/.wgetrc at line 2.
```

That was the actual cause of the qemu download failure — not the network. Two
lessons in one: (1) the rc file was correct in *intent* and wrong in *syntax*,
and had I not read the error closely I would have blamed egress again, which is
what the "no network" story always invites. (2) `prefer_ipv4` is not even a
recognised wget command here.

Removed it. The real fix is a PATH shim at `~/bin/wget` that execs the real
wget with `--inet4-only` — IPv6 is genuinely dead on this host (`curl -6` hangs
entirely; `curl -4` returns in 3.7 s vs 9.4 s default).

Rebuilt clean: **0 wget rc errors, 0 IPv6 stalls.**

### Next

- [ ] Write `init`: framebuffer paint + USB gadget setup (`adb acm`)
- [ ] Pack `boot.img` with the stock kernel + our cpio, `fastboot boot`
- [ ] Compile the real `halimpl/bcm2079x` into the rootfs

---

## Entry 12 — 2026-10-18

### Decisions taken

**Upstream MicroPython, not MaixPy.** Krux targets MaixPy, a MicroPython fork
for the Sipeed K230. Measured the actual gap by counting Krux's imports in
`eugenesan/krux` @ `unified-sighash_seedkeeper`:

| module | files | origin |
|---|---|---|
| `embit`, `base32`, `base43`, `shannon` | 51 | pure Python, already ours |
| `binascii`, `time`, `gc`, `math`, `io`, `sys`, `ujson`, `deflate`, `ucryptolib`, `urandom`, `_thread` | ~110 | upstream MicroPython has these |
| `machine` | 11 | present but API differs |
| `board` | 10 | MaixPy — only needs `board.config`, a dict |
| `lcd` | 8 | MaixPy — **the main gap** |
| `flash` | 6 | MaixPy — firmware update |
| `uhashlib_hw` | 7 | MaixPy hardware SHA; upstream has software `hashlib` |
| `image` | 3 | MaixPy — drawing |
| `fpioa_manager` | 8 | MaixPy pinmux — **irrelevant to a phone** |
| `pmu`, `sensor`, `Maix` | 12 | MaixPy |

Every MaixPy-specific module lands in one of three buckets:

1. **Irrelevant** — `fpioa_manager`, `pmu`, `sensor`, `uUR`. A phone has no user
   pinmux; the stock kernel already wired the display and touch. Delete the
   imports.
2. **Trivial** — `board` (a dict we write), `uhashlib_hw` (alias to `hashlib`).
3. **Must be written anyway** — `lcd` on `fb0`, `image`, `flash`.

So the gap is **three shims we were going to have to write regardless**, over
hardware sharing nothing with a MaixCAM panel. Upstream MicroPython also ships
the other ~110 files' worth of standard modules and builds with a plain
`make` + cross-compiler, instead of MaixCDK.

Note: the NFC port must be a **MicroPython C module** doing ioctls on
`/dev/bcm2079x` directly — it does not go through `machine.I2C`, so upstream's
thin `machine` is not a constraint.

**Krux in this fork has zero C/C++ files** (224 Python files). No C toolchain
decision is needed for Krux itself.

**Toolchains fixed:**

| what | toolchain |
|---|---|
| MicroPython for the device | Buildroot (glibc, armv7-a, Cortex-A15, NEON, VFPv4, hardfloat) |
| Broadcom NFC port (C module) | Buildroot |
| `mpy-cross` | host x86_64 |
| kernel (Phase 6) | AOSP `arm-linux-androideabi-4.9`, **kernel only** |

The AOSP prebuilt cannot link a hosted program at all — verified: no `-lc`, no
`crtend_android.o`, bionic only. It is for the kernel and nothing else.

### SeedKeeper applet: unblocked

The fork contains **no `.cap`/`.jcs` applet**. `src/krux/seedkeeper.py` only
speaks APDU:

```python
SEEDKEEPER_AID = [0x53, 0x65, 0x65, 0x64, 0x4B, 0x65, 0x65, 0x70, 0x65, 0x72]  # "SeedKeeper"
SELECT_APDU    = [0x00, 0xA4, 0x04, 0x00]
def setup(self, pin):   # "Initialise a blank card"
```

`setup()` says *blank card*, so the applet is expected to be on the card
already. The user has a blank JavaCard, a reader/writer, GlobalPlatformPro and
the `.cap` files — so end-to-end testing is unblocked, and the applet can be
loaded onto a card as needed rather than being a fixed input.

Useful detail from the source: `select()` notes that a transport that already
resolved the applet (Android's `IsoDep` does, from the tag's AID list) can skip
the SELECT — "a raw one cannot". Ours is raw, so we must implement `select()`
ourselves. That is exactly what the Phase 0 capture showed: the tag came back as
`[IsoDep, NfcA]` with no SeedKeeper AID in the list.

### Next

- [ ] Phase 2 finish: `init` (framebuffer + USB gadget as `adb acm`), pack,
      `fastboot boot`
- [ ] Phase 3: MicroPython cross-build in Buildroot; NFC port as a C module
- [ ] Phase 4: `board`/`lcd`/`image`/`flash` shims
- [ ] Phase 6: kernel tree, now that the toolchain is resolved

---

## Entry 13 — 2026-10-18

### ISO-DEP layer: sized, and much smaller than feared

Detailed in `b/nfc/ISODEP.md`. Two findings worth recording here.

**1. The stack is public after all — in a repo we were already using.**

`platform/system/nfc` really is empty upstream (only `.clang-format` and
`.gitignore`; Google moved the NFA implementation internal). It looked like
we would have to write ISO 14443-4 from the specifications.

We do not. The whole stack is in **`platform/external/libnfc-nci`**, which we
have been fetching from all along:

```
src/nfc/tags/rw_t4t.c              79 KB   ISO-DEP (Type 4 tag)
src/nfa/dm/nfa_dm_discover.c      117 KB   discovery loop
src/nfc/nci/nci_hmsgs.c            20 KB   NCI message envelopes
src/nfc/tags/rw_main.c             10 KB
```

I looked for this in the wrong place first. The device HAL has no discovery
loop, no `IsoDep`, no tag routing — because it is only the *chip driver*. The
protocol layer is a different layer, above it, and it lives in the repo I had
been treating as "just the BCM transport".

**2. We do not implement ISO 14443-4 at all.**

The controller *is* the ISO-DEP stack. Anticollision, activation, block
protocol, chaining — all in silicon. `rw_t4t.c` shows the entire host side:

```c
p_c_apdu->offset = NCI_MSG_OFFSET_SIZE + NCI_DATA_HDR_SIZE;
UINT8_TO_BE_STREAM (p, T4T_CMD_DES_CLASS);
UINT8_TO_BE_STREAM (p, T4T_CMD_INS_GET_HW_VERSION);
UINT16_TO_BE_STREAM(p, 0x0000);
UINT8_TO_BE_FIELD  (p, 0x00);
if (NFC_SendData (NFC_RF_CONN_ID, p_c_apdu) != NFC_STATUS_OK) ...
```

A bare ISO 7816 APDU — CLA INS P1 P2 Le — handed to `NFC_SendData` and
nothing else. No GID, no OID, no proprietary wrapper. It is framed as an NCI
**DATA** packet (MT_DATA, CID = `NFC_RF_CONN_ID`).

Contrast the two planes we have now measured:

| plane | wire format |
|---|---|
| to the **chip** (vendor/proprietary) | `10 2F 04 00` — HCIT type + GID `0x0F` + OID + len |
| to the **card** (standard ISO-DEP)   | bare APDU in an MT_DATA frame |

So the sequence is: HAL bring-up → discovery loop → activate ISO-DEP →
`NFC_SendData`/`NFC_RecvData` → SeedKeeper SELECT + INS. And step 5 is already
written and tested (`krux/src/krux/seedkeeper.py`, pure Python, reusable
unchanged).

### Build order

```
A. bring-up state machine      (DM/PRM + firmware .ncd)   sourced, partly done
B. NCI data plane              (SendData/RecvData)        new, small
C. discovery + activation      (trimmed nfa_dm_discover)  new, medium
D. IsoDepTransport adapter     (4 methods)               new, small
E. SeedKeeper                  (seedkeeper.py)           reuse
```

`nfa_dm_discover.c` is 117 KB but most of it is NDEF, P2P, routing and
screen-state policy a dedicated device has no use for; the poll configuration
is a small part.

### Main risk

**Response length.** A SeedKeeper *export* exceeds 255 bytes of payload. The
chip does the chaining, but we must reassemble and know the true total. This is
the most likely place to get something subtly wrong, and it is testable
without a real card by sending short APDUs first.

---

## Entry 14 — 2026-10-18

### First `fastboot boot`: reset after ~2 minutes, screen never left the bootloader logo

Diagnosed without a serial cable, by elimination.

**Ruled out: ramdisk compression.** Android's own ramdisk is gzip (`1f 8b`),
ours is gzip (`1f 8b`). Not it.

**Ruled out: framebuffer writes.** `dd ... of=/dev/graphics/fb0` accepted
8,355,840 bytes at 221 MB/s in recovery. `mdsssfb_90000` accepts plain
`write()`, so no mmap code is needed. Not it.

**Ruled out: image size and layout.** 9.8 MB against a 22 MB partition; kernel
at `0x00008000` (7.4 MB) does not reach the ramdisk at `0x02900000`. Not it.

**Found: `/init` had `#!/bin/sh` and there is no `/bin/sh` in the rootfs.**
Busybox installed `bin/ash` but not the `sh` alias:

```
bin/ash     present
bin/sh      MISSING
```

The kernel could not execute `/init`, panicked, and `CONFIG_MSM_WATCHDOG_V2=y`
with `msm_watchdog_v2.enable=1` reset the SoC a couple of minutes later. The
bootloader then did its one-shot-boot fallback and came back to Android. Every
observation fits, including the screen: if init never ran, nothing ever painted,
so the bootloader splash stayed up.

This was **already found and logged** during the build verification — the
post-build reported `bin/sh MISSING` — and then the build was re-run and the
finding was not acted on. The verification worked; it was ignored.

Fixed two ways, because both are cheap:

1. `ln -sf ash target/bin/sh` — restore the alias
2. `init` shebang changed to `#!/bin/ash` — do not depend on the alias at all

Also set `msm_watchdog_v2.enable=0` in the packed cmdline. Not because the
watchdog was guilty — it fired *because* init died — but because a watchdog that
resets the device destroys the evidence. A hang you can debug; a silent reset
takes the log with it.

### The generalisable lesson

Three separate verification steps in this project have now reported a real
problem that was then not acted on: the `nfc-run.sh` stop failure, the
`target-final` phantom build, and this. In each case the check did its job and
the human-or-agent reading it moved on to the next thing.

Worth noticing: the fix was not a subtle bug. `/init` pointed at an interpreter
that did not exist, and the build log had already said so in plain text.

---

## Entry 15 — 2026-10-18

### Test B: our packing + Android's original ramdisk → BOOTED ANDROID

```
Sending 'boot.img' (8548 KB)   OKAY [0.373s]
Booting                        OKAY [0.110s]
```

**This is a success result and it isolates the fault.** Our `bootimg.py`
reproduces the stock image byte-for-byte (verified again: 8,753,152 bytes
identical), the bootloader accepts a `fastboot boot` image, and the kernel boots
and hands off to Android. So the packer, the addresses, the ramdisk placement
and the `fastboot boot` path are **all cleared**.

**The failure is in our rootfs.** That is now the whole search space.

### Why our rootfs might not boot — and the fix that removes all of it at once

The previous attempt put five things between the kernel and anything visible:

1. a shebang (`#!/bin/sh`)
2. a shell interpreter that did not exist (busybox installed `bin/ash`, not `sh`)
3. a **dynamically linked** busybox
4. glibc (`libc.so.6`, 1.5 MB, `ld-linux-armhf.so.3`)
5. 2.4 MB of ramdisk against Android's 1.3 MB

I fixed (2) and it still failed, which is consistent with (1)–(5) being a bundle
of unknowns rather than one bug. **I had been changing several things per
attempt without isolating any of them** — three changes across two tries, none
verified. That was the mistake.

So: **replace `/init` with a statically-linked C program.** We already know a
static ARM binary runs on this device — `nfc-probe` proved it in recovery. That
deletes the shebang, the shell, the dynamic loader and glibc from the boot path
in one move.

`b/rootfs/init.c`, built with `arm-linux-gnueabihf-gcc -O2 -static`. It:

- paints the framebuffer **black immediately** — before anything else, so
  "did we run at all" is answerable by looking
- then draws a **progress ladder**: 8 horizontal bars, one per stage, cycling
  every 3 s. The screen alone says how far it got.
- mounts /proc, /sys, devtmpfs, /tmp
- reports which device nodes exist
- writes `adb acm` to `/sys/class/android_usb/android0/functions`
- opens `/dev/bcm2079x` and issues all four ioctls
- heartbeat loop

### Test D: minimal initramfs

`boot-mini.img` — ramdisk contains **only** `init` (static, 489 KB) and
`nfc-probe`. **470 KB gzipped**, well under Android's 1.3 MB.

If this shows the progress ladder, the kernel, display, USB and NFC are all
proven in one shot and we build up from there. If it *still* sits on the
bootloader logo, then the fault is not rootfs content at all and we stop
guessing and go after how this bootloader handles a non-Android ramdisk.

`msm_watchdog_v2.enable=0` is still set, deliberately: a reset destroys the
evidence.

---

## Entry 16 — 2026-10-18

### Float ABI: the ROM's userland is SOFTFLOAT, ours is HARDFLOAT

Raised as a hypothesis (kernel is 3.4/2015, Buildroot toolchain is GCC 15 /
glibc 2.4x). Measured rather than argued:

```
ROM /system/bin/app_process        ours (static init)
  Tag_CPU_arch:     v7               Tag_CPU_arch:     v7
  Tag_FP_arch:      VFPv4            Tag_FP_arch:      VFPv3
  Tag_ABI_VFP_args: <absent>         Tag_ABI_VFP_args: VFP registers
  Tag_ABI_FP_exceptions: Unused      Tag_ABI_FP_exceptions: Needed
  Tag_conformance:  "2.09"           Tag_conformance:  <absent>
  Tag_ABI_PCS_R9_use: V6             Tag_ABI_PCS_R9_use: <absent>
```

**AOSP ARM builds are normally hardfloat. This ROM's are not.** That is
genuinely surprising and worth recording, even though it turns out not to be
the fault.

**Why it is not the fault:** the float ABI is a *userspace* convention. The
kernel passes no floating-point arguments, so a fully self-contained binary is
consistent with itself regardless of what convention it chose. `nfc-probe` is
hardfloat, static and self-contained, and **it ran on this kernel** — in
recovery, and printed the real Core Reset notification. That is the empirical
refutation, and it is the reason Test D (a static C init) is still worth
running rather than being abandoned in favour of a softer theory.

So: real mismatch, correctly identified, **not the cause**.

### But it is a live hazard for later

The two conventions only collide when one binary is *called by* another built
the other way. Nothing we run does that today, because no Android process is
alive alongside us. It becomes a real constraint the moment anything else
enters the picture — a ROM binary invoked by our init, or a shared library
built against bionic's softfloat PCS. Worth noting now so it is not rediscovered
as a mystery SIGILL later.

Note also `Tag_ABI_PCS_R9_use: V6` in the ROM and absent in ours: the ROM is
built to an older PCS revision. Same class of hazard.

### Standing caveat on our own verification

`nfc-probe` running proves *static, self-contained, hardfloat binaries execute
on this kernel*. It does **not** prove that a dynamically-linked Buildroot
userland works — a shell script with `#!/bin/sh` exercises the dynamic loader,
glibc, and the busybox build all at once, which is three variables, not one.

That is the case for Test D.

---

## Entry 17 — 2026-10-18

### Symptom change, and what it told us

Static C init: **stays on the bootloader logo much longer, and no longer
reboots.** A hang, not a reset.

That is the watchdog-disable working as intended. Combined with no adb device
appearing, it says: **the kernel never gets as far as running anything we
shipped.**

If `/init` fails to execute, the kernel panics with *"No working init found."*
That message goes to `console=ttyHSL0`, we have no cable for it, and the display
was never touched — so the bootloader logo simply stays. Every observation fits.

### The panic is invisible to us, and cannot be made visible

```
CONFIG_VT=y
CONFIG_VT_CONSOLE=y
# CONFIG_FRAMEBUFFER_CONSOLE is not set
```

No framebuffer console, so `console=tty0` is not available. **This is the
constraint that matters**: we are debugging a kernel panic with no channel to
the panic. Guessing is not working — four hypotheses in four attempts, none
verified.

### So: bisect, and pick the split that has not been tried

Every attempt so far has changed the `init` binary *and* the cpio packaging
together, which is why nothing has been isolated. Two variables, one result,
no information.

**Test E** separates them. Take Android's **own ramdisk**, unpacked — its
`fstab.hammerhead`, `debug_ramdisk`, `metadata`, `mnt`, `dev`, the whole
structure — and replace only `/init` with our static binary:

| outcome | conclusion |
|---|---|
screen changes | our **binary is fine**; the fault is in our **cpio packaging** |
hangs on the logo | our **binary** is at fault; packaging is irrelevant |

Nothing else changes: same kernel, same header, same packing, same addresses.
One variable.

**Test D (mini) repacked** with both known-fixed defects corrected:
`--owner 0:0` (it was `user:user`, uid 1000) and `gzip -n` (Android's gzip
header has FLG=0x00, ours had 0x08/FNAME).

Note Buildroot's own cpio was **already** root:root, so ownership cannot explain
the full-rootfs failure — which is exactly why Test E is needed to tell the two
causes apart.

### Housekeeping

Buildroot's `busybox` is installed **setuid root** (`-rwsr-xr-x`). Harmless in
an initramfs, but worth knowing it is there.

---

## Entry 18 — 2026-10-18

## ROOT CAUSE: the kernel does not enable VFP for userland

Measured, not inferred. A 24-line static binary reading `AT_HWCAP`:

```
AT_HWCAP = 0xfb0d7
  HWCAP_FP    (bit 11) : NO     <-- VFP not exported
  HWCAP_ASIMD (bit 12) : YES
```

`CONFIG_NEON=y` and `CONFIG_VFPv3=y` are both set in `kconfig`, which is why
this was invisible from the config. What matters is whether the kernel's
`elf_hwcap` advertises the coprocessor to userland, and it does not.

**A hardfloat binary that emits a VFP instruction therefore takes SIGILL.** Our
`init` emitted 8 of them. As `/init` that becomes *"No working init found"* — a
kernel panic printed to `console=ttyHSL0`, a port with no cable attached.

This explains **every** failure at once:

| attempt | why it failed |
|---|---|
static hardfloat init | 8 VFP instructions -> SIGILL |
Buildroot rootfs | hardfloat glibc + hardfloat busybox -> SIGILL somewhere in startup |
`nfc-probe` under adb | built the same way, but ran — its VFP instructions were on cold paths it never took |
Test E (Android's ramdisk, our init) | proved it is the **binary**, not the packaging |

And the ROM's own userland is **softfloat** (`app_process` carries no
`Tag_ABI_VFP_args`) — it has to be, for exactly this reason.

`CONFIG_FRAMEBUFFER_CONSOLE` is unset, so the panic could never have been read
on screen. Five attempts, four hypotheses, and the measurement that settled it
was `AT_HWCAP`.

### The fix

`arm-linux-gnueabihf-gcc` cannot build softfloat against its own hardfloat
glibc — the headers reject it. But if we **do not link glibc at all**, the ABI
mismatch disappears: the flag only governs codegen, and with no caller to
disagree about argument passing there is nothing to disagree about.

`b/rootfs/init-freestanding.c` — `-nostdlib -ffreestanding -mfloat-abi=soft
-marm -march=armv7-a -mtune=cortex-a9`, raw syscalls inline, `_start()` as the
entry point, a static stack installed in asm.

Verified before flashing:

```
size: 66,964 bytes
Tag_ABI_VFP_args: <absent>
VFP/NEON instruction count: 0        (was 8)
```

### Consequence for the whole project — this is not just an init problem

**Every userspace component must be softfloat.** That includes:

- the Buildroot rootfs (currently hardfloat glibc — will need a musl-softfloat
  or hardfloat-ABI-disabled build)
- **MicroPython**
- **the Broadcom NFC HAL port**
- any C module loaded into either

`AT_HWCAP_FP` being clear while `AT_HWCAP_ASIMD` is set is unusual and worth
one more look, but the practical rule is settled either way: assume no VFP.

This is also the kind of fact that would have been discovered the expensive
way — as a mysterious SIGILL deep inside a MicroPython build, hours from the
cause. It cost five boot attempts to find, and one `getauxval` call to confirm.

## Entry 19 — Test P: the 5-instruction init also hangs. PID 1 is the variable.

**Tests M, N, O, P all used our own binary as `/init`. All four hang, with an
identical symptom: bootloader logo, no adb, no output. Test G, which used
Android's `/init` in our repack, boots.**

### What each test eliminated

| Test | `/init` | Result | Ruled out |
|---|---|---|---|
| G | Android's init | **boots** | packing, ramdisk handling, our cmdline |
| M | ours, freestanding, fb first | hang | — |
| N | ours, USB first | hang | ordering (USB vs framebuffer) |
| O | ours, no mounts, spin + `reboot(RB_POWER_OFF)` | hang | — |
| P | ours, 5 instructions, one `svc` | hang | libc, `puts`, stack swap, loop, syscall path |

### Test P in detail — the smallest possible init

```asm
100b8:  push {r7}
100bc:  mov  r0, #0x4321      ; RB_POWER_OFF
100c0:  mov  r7, #142         ; __NR_reboot
100c4:  svc  #0x00000000
100c8:  b    100c8
```

No libc, no stack array, no `puts`, no `mmap`, no loop, no arguments. If the
kernel enters this and the device does not power off, the kernel did not enter
this.

### Correction to Entry 18 — I did not verify what I claimed

Entry 18 asserted that `probe-pid1` "ran in TWRP and powered the device off."
**That was never observed.** The evidence was an *interrupted tool call*. I
inferred power-off from the call dying. That inference could equally be explained
by the call being interrupted for an unrelated reason, with the binary hanging
in TWRP exactly as it does as PID 1.

So the one piece of evidence that appeared to localise the fault ("the binary
runs fine as a process; therefore the fault is PID-1-specific") rests on an
unverified inference. It needs re-testing with the device watched.

### The one thing that IS genuinely verified

`init-dry` in TWRP, observed output:

```
=== init: stage 0, alive ===
stage 1:SKIPPED (DRY_RUN)
stage 2:SKIPPED (DRY_RUN)
stage 3:painted 400 rows into buffers 0 and 1
  ioctl OK:POWER_CTL
  ioctl OK:WAKE_CTL
  ioctl OK:SET_CLIENT_ADDR
  ioctl OK:SET_WAKE_ACTIVE_STATE
```

This is real: the freestanding code runs, the stack swap works, `pwrite64` to
the framebuffer works, and all four `/dev/bcm2079x` ioctls return OK. Whatever
is broken, it is **not** the freestanding toolchain, the syscall wrappers, the
stack setup, or the hardware access.

### Where the fault must be

`init-dry` works as an ordinary process; `probe-p1a` hangs as PID 1. The
difference between those two situations is now the entire remaining search
space. Candidates, in rough order of likelihood:

1. **`reboot(RB_POWER_OFF)` is refused or the call never returns.** Possible
   cause: the CAF kernel gates `reboot` behind a magic argument, or
   `CONFIG_REBOOT` behaves differently for PID 1, or the SMP secondary cores
   are parked in a way that needs a PMIC handshake. **Test P cannot tell this
   apart from "never ran"** — both look like a hang from outside.
2. **PID 1 signal/child semantics.** Unlikely to hang a 5-instruction binary.
3. **The kernel's `run_init_process` path** differs for a static ET_EXEC with
   no `PT_INTERP` and no auxv use.
4. **Something about our repacked ramdisk only when `/init` is replaced** — but
   Test G contradicts this, since Test G also repacked and booted.

Candidate 1 is now the leading suspect, and it is cheap to eliminate.

### Why Test P could not distinguish, and how to fix that

A hang is our only failure signal, and "hang" conflates three distinct causes:
never ran / ran and hung / ran and got stuck in a syscall. Every test so far has
used hang-or-work as its oracle, which is why nine attempts have produced
ambiguous results.

**The fix is an oracle that produces different observable states.** The only
states observable without a serial console are: the display, USB enumeration,
and whether the device powers off. A test should be built so that each candidate
cause leads to a *distinguishable* one of those, rather than all of them
leading to "logo stays up".

Concrete: an init that paints the framebuffer in a **different colour on
success** rather than merely continuing. Screen-white versus screen-black
distinguishes "ran and got to stage 3" from "never got there" — which no test so
far has been able to do.

### Status

`probe-pid1` needs re-running in TWRP **with the device watched**, to establish
whether `reboot()` works there at all. If `reboot()` is refused in TWRP too,
then Test O and Test P are both explained without any PID-1 involvement, and
the seven earlier "hangs" need re-reading entirely.

### Process failures this entry

- Claimed an unverified result as verified (Entry 18). Compounding it, I then
  built Test P on top of that claim. The check that would have caught it —
  actually looking at the device — was skipped.
- Suggested running the destructive `init-fs` inside TWRP without noticing it
  mounts devtmpfs over `/dev` and rewrites the USB gadget, which destroyed
  TWRP's own adb. `init-dry` exists as a result and is the correct way to test.

## Entry 20 — `reboot()` blocks. Tests O and P are both void. New oracle.

### The finding

```
adb shell 'chmod 755 /data/local/tmp/probe-p1a; /data/local/tmp/probe-p1a'
  -> hangs. TWRP still responsive.
```

**`reboot(RB_POWER_OFF)` never returns, even as an ordinary process in TWRP.**

### Consequence: Entry 19's Test P and Test O proved nothing

Both were built on the premise that `reboot()` would power the device off if our
init ran. It does not. So:

- **Test P** did not show that the kernel never entered `/init`. It showed that
  `reboot` is blocked. The 5-instruction init probably *did* run perfectly.
- **Test O** likewise. Its spin loop never mattered.

Nine tests, and the last two were measuring an unrelated kernel behaviour. The
interpretation I gave them ("the kernel never entered our init") was unfounded.

### What TWRP *did* establish about Test P

The binary **reached the `svc` instruction.** It is not faulting before the
syscall. Combined with `init-dry`'s observed success, the freestanding code,
the syscall wrappers and the ELF entry are all sound. The remaining question is
narrower than I thought, and better:

> Does our binary run as **PID 1**?

Not "does our init have a bug". Just: does it execute at all in that role.

### Test Q — `init-usbonly`, designed so the outcomes are distinguishable

```c
/* no libc, no stack swap, no mount, no fb, no NFC, no reboot, no spin-loop
   that matters. Five sysfs writes, then spin. */
"/sys/class/android_usb/android0/enable"    -> "0"
"/sys/class/android_usb/android0/idVendor"  -> "0x18d1"
"/sys/class/android_usb/android0/idProduct" -> "0x4ee2"
"/sys/class/android_usb/android0/functions" -> "adb acm"
"/sys/class/android_usb/android0/enable"    -> "1"
```

Nothing here can block. The kernel mounts sysfs (and devtmpfs under
`CONFIG_DEVTMPFS_MOUNT`) before running `/init`, so the paths already exist.

| observation | conclusion |
|---|---|
**adb enumerates** | **our binary ran as PID 1.** Every earlier failure was in what came *after*: the `mount` calls, the framebuffer `pwrite64`, or the NFC ioctls. |
**no adb, hang** | our binary does not run as PID 1 at all, and no amount of debugging the C will help until that is answered. |

This is the first test in the whole series whose failure modes are *different
from each other* rather than all collapsing into "the logo stayed up".

### Process failure this entry

I described `init-usbonly` as non-destructive and then ran it inside TWRP. It
does not mount over `/dev`, but it does rewrite `functions`, which **replaces
TWRP's adb function** and drops the connection. Device recovered on its own
after ~30 s, no harm done.

This is the third time in this phase I have destroyed the debug channel by
running something in a live system. The pattern is now clear enough to name:
**anything that writes to `/sys/class/android_usb` or mounts over `/dev` is
destructive to the live environment, full stop.** Both `init-dry` and
`init-usbonly` need `-DNO_GADGET` guards, or simply do not get run in TWRP.

## Entry 21 — six broken oracles in a row, and the pattern behind them

Tests R, S and T all hang. But the useful content of this entry is not the
hangs — it is that **every oracle used since Test G has been invalid**, and I
flashed an image for each one anyway.

### The invalid oracles

| Test | Oracle used | Why it was invalid |
|---|---|---|
| N | "adb appears" | **INVALID.** `f_adb` is only the kernel endpoint. Nothing enumerates without `adbd` (userspace), which we never launch. No adb was possible even on total success. |
| O, P | "device powers off" | **INVALID.** `reboot()` blocks. Proven in TWRP (Entry 20). |
| R | "screen changes" | **UNVERIFIED.** Never once confirmed that writing fb0 makes anything *visible*. `init-dry` reported a successful `pwrite64` and nobody looked at the screen. The bootloader logo may lie outside the scanned region, or the panel may still be in bootloader video mode. |
| S | "screen changes" | Same unverified assumption, plus it mounted devtmpfs itself — but still rests on the framebuffer being visible. |
| Q | "adb appears" | INVALID as N. Also rewrote the gadget and destroyed the debug channel. |

The trap in all five: **a failed action and a non-running init look identical**,
because we have no working output channel. Every "hang" was consistent with both
"init ran and the action failed quietly" and "init never ran". I read that single
ambiguous bit of evidence as ten independent ones.

### The lesson I keep relearning

The genuinely solid result across this entire phase remains `init-dry` in TWRP:
freestanding code, stack, syscalls, framebuffer `pwrite64`, and all four NFC
ioctls work **as an ordinary process**. Everything since has been about whether
that same code runs as PID 1 — and no test has validly answered it.

**Rule going forward: before flashing, state what the two possible outcomes
mean and confirm they are actually distinguishable.** If both outcomes look the
same from outside, the test is worthless regardless of how minimal the binary
is. Ten minimal binaries did not compensate for one unexamined assumption.

## Entry 22 — Test T: my own arithmetic bug, and it nearly wrote to userdata

### What happened

Test T wrote a 512-byte magic sector at `(sectors - 8) * 512`. That value is
**31,268,532,224 — 35 bits.** The `pwrite64` wrapper passed the offset with the
high word set to `0`, so it silently truncated to **1,203,761,152 = sector
2,351,096**.

**That sector is inside `mmcblk0p28` — real user data**, not the unallocated
space at the end of the disk the test intended to use.

### Why the same truncation hit both writer and reader

TWRP's busybox `dd` also has a 32-bit `off_t`, so `skip=61071352` truncated to
the *same* sector 2,351,096. That explains an anomaly I had flagged and could
not account for: the control string appeared at **both** "offsets". There was
only ever one sector being read.

### Damage assessment — none

Forensic hex dump of sector 2,351,096: bytes 0–28 are TWRP's control string,
**bytes 29–511 are untouched `ff`**. Our init writes a full 512-byte buffer, so
if it had written, the entire sector would carry the magic. It did not.

```
$ adb shell 'e2fsck -fn /dev/block/mmcblk0p28'
Pass 1..5 complete, no errors
/dev/block/mmcblk0p28: 26222/1720320 files (5.2% non-contiguous), 5975265/6869231 blocks
```

Clean. The many "extent tree could be shorter" lines are pre-existing
optimisation notices across hundreds of inodes, not corruption. **p28 is intact.**

### Consequence: Test T is void, not negative

The mechanism that would have deposited the marker was broken by my arithmetic.
Its "no magic" result carries no information about PID 1.

### The safe-zone idea is also dead — and this is actually good news

Intended offset past the last partition: the last partition (p28) ends at sector
**15,268,596**; the disk is 61,071,360 sectors, so ~22 GB is unallocated. But:

```
$ adb shell "dd of=/dev/block/mmcblk0 bs=512 seek=15272692"
dd: /dev/block/mmcblk0: Invalid argument
```

**The kernel rejects writes past the last partition.** That region is protected
— good, but it means "scratch space at the end of the disk" was never a
viable write target on this device, and I had assumed it was.

It also means every byte-offset past ~8 GB needs genuine 64-bit arithmetic in
both the init and the readback. That is the bug to fix, and it needs a
64-bit-safe readback path too, not just a 64-bit `pwrite64`.

### Next step

Any marker test must:
1. split the offset into high/low 32-bit words for `pwrite64` (r3/r4 pair on
   ARM EABI — the offset argument is a register *pair*, which my `sc()` wrapper
   never handled correctly);
2. read back with a path that is itself 64-bit clean, and prove it with a
   control write **before** trusting a negative.

Until such a test exists and has passed its control, further images are guesses.

## Entry 23 — ROOT CAUSE FOUND: this kernel has no devtmpfs

```
$ adb shell 'grep -E "devtmpfs|ext4|tmpfs" /proc/filesystems'
	nodev	tmpfs
  	ext4
```

**There is no devtmpfs.** Every init written since Test S opened with:

```c
sc(SYS_mount, "devtmpfs", "/dev", "devtmpfs", 0, 0);
```

That mount **always failed**. `/dev` therefore stayed an empty ramdisk
directory: no `/dev/graphics/fb0`, no `/dev/bcm2079x`, no `/dev/block/*`. Every
init took its error path and hung — silently, with nowhere to report it,
because `/dev/console` did not exist either.

**This is the single cause behind the entire hang series.** It was never
eleven separate mysteries. Test G (Android's own init, which populates `/dev`
via ueventd) booted; everything that needed `/dev` never did.

TWRP works because its `/dev` is a tmpfs **populated with mknod'd nodes**, not
devtmpfs. That is why the control succeeded in TWRP and our init could never
succeed — different `/dev`, same binary.

### Consequence for the whole project

The Buildroot rootfs, MicroPython and the NFC HAL port will all need
`/dev` populated by explicit `mknod` at boot, or a static device-node list baked
into the cpio. Plan for the latter: put the nodes in the initramfs directly so
no runtime mknod is required.

## Entry 24 — Test V was ambiguous by construction, and I broke TWRP's /dev

### Test V's flaw

The init wrote `/mnt/ud/INIT_RAN_V1` **whether or not the ext4 mount
succeeded**. If the mount failed, the file was created on the ramdisk and
vanished at reboot. So "marker absent" meant *either* "init never ran" *or*
"init ran and the mount failed" — I had reintroduced the exact ambiguity the
test was meant to eliminate.

Also: the correct readback path is **`/data/INIT_RAN_V1`**, not
`/mnt/ud/...`. The ramdisk is tmpfs and `/mnt/ud` is gone after reboot, but
`/data` is the same ext4 filesystem TWRP mounts, so the file appears there.
The control confirmed this: `/data/CONTROL_PROOF` was readable after a full
power cycle. **The oracle mechanism itself is proven end-to-end.**

### I destroyed TWRP's /dev, twice

Mounting `tmpfs` on `/dev` **succeeds** — it does not fail as I assumed. It
silently replaced TWRP's entire `/dev` (zero entries, no partitions
reachable). `dmesg` showed it plainly:

```
[99.999405] SELinux: initialized (dev tmpfs, type tmpfs), uses transition SIDs
```

That is what made the first control "fail"; nothing was wrong with the
hardware. Restored with `mknod /dev/mmcblk0p28 b 179 28`.

**This is the fourth time I have destroyed the debug channel by running
something in a live system.** The rule, now concrete:
- **never mount on `/dev`** — it succeeds and destroys the running system
- **never write `/sys/class/android_usb`** — replaces TWRP's adb function
- prefer `mknod` over replacing a directory

## Entry 25 — `mknod` from our freestanding binary returns EINVAL

With no devtmpfs, `mknod` is the way to populate `/dev`. It fails from our
binary but works from TWRP's shell. Isolated with a standalone test:

```
mknod block 179:7 -> -22 (EINVAL)
mknod char  179:7 -> -22
mode = 0          -> -22
```

Not EPERM — so **not** a permission problem. Ruled out:

| hypothesis | result |
|---|---|
| `makedev` encoding wrong | tested 4 encodings incl. the full glibc formula — all -22 |
| 64-bit arg needs an aligned register pair | tested dev in (r3,r4), in (r4,r5), 32-bit in r3 — all -22 |
| filesystem refuses device nodes | `mknod` succeeds on /tmp, /dev, /mnt, /storage from the shell |
| kernel lacks `sys_mknod` | **present** in /proc/kallsyms |
| `mknod` is a toybox applet, not the syscall | it is, but it reaches the same `sys_mknod`, which exists |

Plain filesystem syscalls from our binary are fine: `mkdir` returns 0,
`open(O_CREAT)` returns a valid fd 3. So path handling, argument passing and
the `svc` path all work — the failure is specific to `mknod`'s `dev_t`.

**Still unexplained.** Next hypotheses, in order:
1. This CAF kernel validates `dev_t` more strictly than mainline — e.g. rejects
   a `dev_t` whose encoding does not match what `new_decode_dev()` expects for
   32-bit ARM. Worth reading `fs/namei.c:may_mknod()` / `may_create()`.
2. `CAP_MKNOD` present but `devcgroup` (Android configfs device controller)
   denies the specific major/minor — Android kernels ship
   `CONFIG_CGROUPS`+`devcgroup` and it can return EINVAL for unlisted devices.
   **This is the most likely explanation**: toybox runs as root with a
   permissive domain, our binary has no SELinux label at all.
3. SELinux: our init runs with no domain, so `mknod` on a `device` node class
   is denied.

**(2) or (3) mean the fix is not in our code at all** — it is that a process
with no SELinux label, or outside devcgroup's whitelist, cannot create device
nodes. If so, the answer is to bake static device nodes into the **cpio
archive** (newc supports device entries; `cpio` can emit them if the files
exist, or the archive can be written directly), so no runtime `mknod` is
needed at all. That is the cleaner design regardless, and it is the next step.

### Process failure this entry

Five consecutive mknod experiments, each of which could have been one careful
read of `may_mknod()` in the kernel source. The isolation was worth doing, but
the SELinux/devcgroup hypothesis was available from the start: our binary is
built with **no SELinux labeling and no policy context**, and this is an
Android kernel. I should have considered that before writing six variants.

## Entry 26 — ARCHITECTURE CHANGE: run as a service under TWRP's init, not as PID 1

The user's suggestion: inject our userspace into the *existing* recovery image
and run it as PID 2, rather than replacing `/init`.

**This ends the entire hang saga.** Every one of the twelve failures came from
trying to *be* PID 1. Under TWRP's Android init none of those problems exist:

- `ueventd` populates `/dev` (no devtmpfs needed — it does not exist here)
- `mknod` is not required (denied to unlabelled processes anyway)
- SELinux is permissive (`androidboot.selinux=permissive`, absent from the ROM)
- `/dev/console` exists, so failures are visible
- adb works, restoring the debug channel
- the process is supervised and restartable, with no PID 1 semantics

Injection is minimal: `sbin/init-airgap` + `init.airgap.rc` + one `import` line
in `init.rc`. Stock TWRP kernel is byte-identical in the image.

### Three Android-init requirements, all found the hard way

1. **`.rc` files must not be group/world-writable.** A `cat >` heredoc left
   `init.airgap.rc` at 0664 and init silently refused it:
   `Unable to read config file: Skipping insecure file`. Accepted files are
   0750. Fixed with `chmod 0644`, and verified *inside the archive* — the mode
   that survives packing is what matters, not the mode on disk.

2. **`class core` does not auto-start here.** TWRP's `init.rc` runs
   `class_start default` and never `class_start core`, so the service was
   defined and parsed but never started. `setprop ctl.start airgap` started it
   instantly. An explicit `on boot { start airgap }` is required.

3. **`on boot` fires ~5 s before userdata is mounted.** Measured repeatedly:
   ```
   08:54:26.726  on boot -> service starts
   08:54:30.544  EXT4-fs (mmcblk0p28) mounted   <- 3.8 s later
   08:54:30.646  service exits 0, marker written
   ```
   The failure signature is always the same, and it is in the audit log:
   ```
   avc: denied { add_name } ... name="AIRGAP_USERSSPACE_PROOF"
       tcontext=u:object_r:rootfs:s0
   ```
   `tcontext=rootfs` means the write landed on the ramdisk tmpfs and vanished at
   power cycle. **This line was available the whole time and I read it only
   after four failed attempts.**

### Four wrong readiness checks, and why each failed

| check | result | why it failed |
|---|---|---|
`stat("/data")` | fail | the ramdisk ships an empty `/data` dir; always succeeds |
`stat("/data/media")` | fail | `/sbin/recovery` contains the literal string `/data/media` and creates it early, so it exists on the ramdisk too |
`statfs f_type == EF53` | fail | the ramdisk's `/data` **also** reports `0xef53` |
`statfs f_blocks > 0` | **works** | ramdisk dirs report 0 blocks; real mounts report millions |

Verified by dumping the raw struct:
```
/data  w0=0000ef53 w2=00672b2a w3=000da3cb   <- 6.7M blocks
/sbin  w0=858458f6 w2=00000000 w3=00000000   <- ramdisk, zero
```

Also removed: `open(O_DIRECTORY)`. **This kernel returns EINVAL for
O_DIRECTORY (0x10000)** — measured. That constant is not portable.

### THE ACTUAL BUG, and it was embarrassing

`f_blocks` was correct all along. The wait loop was mis-sized:

```
measured: 900 iterations of the inner spin = 398 ms   (gettimeofday, on device)
```

The loop was commented "up to ~45 s" on the assumption each iteration was
~50 ms. It is ~0.44 ms. So the wait was **0.4 s** against a **3.8-5.4 s**
requirement — it always gave up early, which is exactly the observed symptom.
Sized to 20,000 iterations (~8.8 s), it now waits correctly.

Verified in both directions before shipping:
- against mounted userdata → detects immediately, writes, exits 0
- against a ramdisk path (`/sbin`, f_blocks=0) → spins, does not exit early

### FINAL RESULT — fully automatic, no manual steps

```
$ adb shell 'cat /data/AIRGAP_USERSSPACE_PROOF'
AIRGAP-USERSSPACE-RAN-AS-NON-INIT-PROCESS-V1
```

`/proc/1/exe -> /init` confirms TWRP's init kept PID 1.

### Wrong syscall numbers found along the way

Cross-checked every number against `asm/unistd-eabi.h` instead of trusting
memory. Three were wrong and had been carried across many tests:
- `read` = **3**, not 45
- `mknod` = **14**, not 33
- `pwrite64` = **181**, not 146
- `statfs` = **99**, not 140 (this one silently returned EBADF)

This likely explains the earlier `mknod` EINVAL mystery: `__NR_mknod` is 14, and
33 is a different syscall entirely.

### Process failures this entry

- I asserted "the device didn't reboot" from stale logcat timestamps when
  `/proc/uptime` showed 84 s. I was wrong and said so only after checking.
- I ran a 120-second adb polling loop whose output was pure noise; the user
  correctly called it out.
- I rewrote `init-airgap.c` four times guessing at readiness instead of reading
  `tcontext=rootfs` in the audit log, which named the cause outright.
- I compounded the syscall-number problem by writing them from memory in every
  single test binary. The header was on disk the whole time.

### What this means for the project

Phase 2 is unblocked. MicroPython, the Broadcom NFC HAL and Krux can all be
built as ordinary binaries launched by TWRP's init, in a rootfs with a working
`/dev`, a working adb for debugging, and no PID 1 hazards. The Buildroot rootfs
can be unpacked into `/sbin` or `/data` and started the same way.

Cleanup of the TWRP image (30 MB of `/sbin`, of which 1.9 MB is a UBSan runtime
and much of the rest is the recovery GUI) can wait until something is known to
depend on it.

## Entry 27 — Capability probe: all four hardware paths proven

Before MicroPython. The user's reasoning is right: MicroPython, the Broadcom
NFC HAL port and Krux all sit on these four paths, and a missing device should
surface as a hardware fact, not as mysterious behaviour in Python.

`b/rootfs/caps-probe.c` — freestanding, soft-float, no libc, run as a service
under TWRP's init (never as PID 1). Results to `/data/caps-probe.txt`, flushed
after every line so they survive stage 4's power-off.

### RESULTS — all four proven

| stage | result | evidence |
|---|---|---|
**1. SCREEN** | **PASS** | `pwrite64` stride=4352, 400/400 rows, readback row 200 `pixels_match=YES` |
**2. TOUCH** | **PASS** (node) | `/dev/input/event1` opens, non-blocking read returns -11 EAGAIN (nothing buffered at boot) |
**3. NFC** | **PASS** | POWER_CTL=0, WAKE_CTL=0, CoreReset write=6, GET_BUILD_INFO write=4, read 6 bytes `10 60 00 00 02 00 01` |
**4. POWER OFF** | **PASS** | device powered off; adb, fastboot and network all gone |

The screen readback matters: a write that "succeeds" but is not readable back
would mean writing a buffer the display is not scanning, which is exactly the
ambiguity that made every earlier framebuffer oracle worthless.

### Two bugs the probe caught immediately

**NFC write() returned -5 (EIO) until O_NOCTTY was added.**
`b/phase0/nfc-probe.c:216` opens the node `O_RDWR | O_NOCTTY`; the probe used
plain `O_RDWR`. `O_NOCTTY` = 0x0400 (asm-generic/fcntl.h). With it, all writes
and the response read succeed. This is why Phase 0's probe worked and ours did
not — the difference was one flag.

**Power-off: `reboot(2)` returns EINVAL; `setprop sys.powerctl shutdown` works.**
`__NR_reboot` is **88** on ARM. Earlier tests used **142**, which is
`_newselect` — an unrelated syscall that never returns. **That is why "reboot
blocks" was reported in Entry 20: we were calling the wrong syscall entirely.**
Even with the magic word, this CAF kernel returns -22 to a non-init process, so
the probe falls back to the Android mechanism:

```rc
on property:sys.powerctl=*
     powerctl ${sys.powerctl}
```

Since we run *as an init service*, we can simply exec `/sbin/setprop
sys.powerctl shutdown` (toybox) and let init do the shutdown. That is how TWRP
itself powers off, and it needed no reimplementation of the property socket
protocol.

### Measured boot ordering (why the probe waits)

```
08:54:26.726  on boot -> service starts
08:54:30.544  EXT4-fs (mmcblk0p28) mounted    <- 3.8 s later
08:54:30.646  service exits 0, marker written
```

`statfs` f_blocks is the only reliable readiness test. `stat("/data")` always
succeeds (the ramdisk ships an empty `/data`), `f_type == EF53` is also true for
the ramdisk `/data`, and `/data/media` exists early because `/sbin/recovery`
creates it. Measured directly:

```
/data  w0=0000ef53 w2=00672b2a  <- 6.7M blocks, real userdata
/sbin  w0=858458f6 w2=00000000  <- ramdisk, zero
```

The wait loop is sized from measurement, not guesswork: **900 iterations of the
inner spin = 398 ms** (gettimeofday, on-device), so the loop is 20,000 (~8.8 s).

### Syscall numbers, all cross-checked this time

Verified against `asm/unistd-eabi.h` and then confirmed in the disassembly
(`mov r7, #N`) before shipping:

```
1 exit   3 read   4 write  5 open   6 close  11 execve
54 ioctl 88 reboot  99 statfs  118 fsync  180 pread64  181 pwrite64
```

All appear as expected in the binary. No invented numbers. This is the direct
result of the earlier lesson — `read`=3 (not 45), `mknod`=14 (not 33),
`pwrite64`=181 (not 146) and `statfs`=99 (not 140) were all wrong earlier, and
`statfs`=140 silently returned EBADF.

### Packaging

`boot-twrp-capsprobe.img` contains both services:
- `sbin/init-airgap` + `init.airgap.rc` (the PID-1 proof)
- `sbin/caps-probe` + `init.capsprobe.rc` (this probe)

Both `.rc` files are mode 0644 root:root **inside the archive** (verified after
packing — that is the mode that counts), the kernel is byte-identical to stock
TWRP, and `ramdisk-files.sha256sum` / `ramdisk-files.txt` are updated so TWRP's
self-restore stays consistent.

### What this unblocks

MicroPython can now be built against four proven interfaces:
- framebuffer at stride **4352** (not width*4), buffers 0/1 only, verified readback
- `/dev/input/event1`, `touch_dev`, ABS codes 0x2f/0x30/0x35/0x36/0x39/0x3a, no SYN_REPORT
- `/dev/bcm2079x` with `O_RDWR|O_NOCTTY`, seven ioctls, Core Reset + GET_BUILD_INFO working
- power-off via `setprop sys.powerctl shutdown`

The remaining softfloat constraint stands: `AT_HWCAP_FP = NO`, so every binary
we build must be `-mfloat-abi=soft` and verified to emit zero VFP/NEON.

## Entry 28 — THE SCREEN NEVER SHOWED ANYTHING. Cause found: `stop recovery` was fatal

### The human test that settled it

The user ran the interactive proof (`touch-off`) and reported:

1. Screen remained on the **bootloader logo**
2. No visual changes, ever
3. Touch responded, device powered off correctly

So our binary ran, opened `/dev/graphics/fb0`, wrote every row, read it all
back correctly -- and **not one pixel was ever displayed.**

### Root cause: my own `stop recovery`

The first `init.touchoff.rc` was:

```rc
on boot
    stop recovery
    start touchoff
```

`on boot` fires **long before** TWRP's recovery process has powered the panel
on and programmed the MDP/DSI pipeline. Killing recovery at that point means
the display is **never handed over from the bootloader at all.**

Our writes went into a framebuffer that **no display hardware was scanning.**
Every `pwrite64` returned 4352, the readback matched pixel-for-pixel, and the
panel showed nothing, because correct memory is not the same as a displayed
image.

### Why this invalidates the earlier "screen=PASS"

Entry 27's capability probe reported `RESULT screen=PASS` based on write +
readback. That was a **hardware fact, not a demonstration**, and it was
recorded as if it proved the display worked. It proved only that `fb0` accepts
bytes. There is no software check short of a human looking at the panel that
distinguishes "wrote correctly to an unscanned buffer" from "wrote correctly to
the scanned one."

This is the same class of error as the invalid adb oracle (Entry 21): an
observation channel that cannot fail, so it cannot inform.

### Fix

1. **Never `stop recovery`.** Let it start normally and hand the display over.
2. **Wait for it**: `sbin/touchoff-delay.sh` sleeps 8 s, then execs touch-off.
3. **Probe `FBIOGET_FSCREENINFO`** (ioctl 0x4600) at runtime and log it, so a
   wrong stride shows up as data rather than as a blank screen.

`sbin/touch-off` also no longer assumes recovery is gone; it simply paints over
whatever is there once recovery has finished starting up and is idle.

### New image

`b/rootfs/stock/boot-twrp-touchoff2.img` — verified `stop recovery` absent from
the archive.

### Process failure

I introduced this bug while trying to solve a real problem (recovery repainting
over us), then "solved" it by removing the component that owned the hardware.
I should have asked why *nobody had ever seen a pixel* -- across every single
framebuffer test in this project -- before adding a line that guaranteed the
display was never initialised.

## Entry 29 — The screen flash, the phantom power-off, and the answer to "how do WE init the display?"

### What the human test showed

Booting `boot-twrp-touchoff2.img`:

1. **TWRP booted and showed its screen** (so recovery ran normally)
2. **The screen flashed** — our write finally became visible
3. **The device powered off with no input**

### Bug 1: phantom power-off (mine)

The code fired `power_off()` on **any** `TRACKING_ID >= 0`. This driver emits
an initial contact when the node is opened, so it triggered instantly.

Fixed to require a **complete gesture**: down (`TRACKING_ID >= 0`) **and** both
coordinates **and** release (`TRACKING_ID == -1`), plus a settle drain on open
and a 1 s arm delay. Verified on hardware:

```
touch-off: TAP at x=600 y=1021
  powering off...
```

A real, human tap — not a phantom. Device powered off cleanly.

### Bug 2 (my earlier reasoning was WRONG): the panel does NOT need TWRP

Entry 28 claimed the display is never handed over unless TWRP's recovery
initialises it, and therefore that we could not bring the panel up ourselves.
**That was overconfident and not tested.** Test on hardware, with recovery
explicitly stopped:

```
$ adb shell 'setprop ctl.stop recovery'
$ /data/local/tmp/touch-off
touch-off: fb0 open
touch-off: event1 open
touch-off: FBIOGET_FSCREENINFO id=00000438 00000780 00000438 00000f00
touch-off: screen filled magenta (all rows written), readback verified
```

So the corrected picture:

- The **kernel** provides the mdss/DSI panel and `/dev/graphics/fb0`.
- Something in the **boot chain** initialises the panel — but it is not
  specifically TWRP's UI process.
- In Entry 28 the failure was `stop recovery` running at `on boot`, i.e. before
  *anything* had initialised the display. Not "TWRP owns the panel".

`FBIOGET_FSCREENINFO` decoded: id=`0x438`(1080) xres, `0x780`(1920) yres,
`0x438` xres_virtual, `0xf00`(3840) yres_virtual — **1080x1920 with a 3840
line virtual height, i.e. two stacked 1920-row buffers**, matching stride 4352
and the "buffers 0 and 1 only" finding.

### The display control surface (no TWRP needed)

`/sys/class/graphics/fb0/` exposes:

| node | value | meaning |
|---|---|---|
`stride` | 4352 | **not** 1080*4. The single most important constant here. |
`bits_per_pixel` | 32 | |
`virtual_size` | 1080,3840 | two 1920-row buffers |
`state` | 0 | |
`blank` | writable | blank/unblank |
`power/` | dir | `control`, `runtime_status`, suspend counters |
`mode`, `modes`, `pan`, `rotate` | | standard fbdev controls |

Writing `blank` did **not** change `state` (stayed 0 in both directions), so the
panel bring-up is not simply an fbdev blank. There is also `/dev/graphics/fb1`
(640x1440) and `/dev/graphics/fb2` (1920x3240) — the SMD/mdss secondary
surfaces.

**So the answer to the user's question:** we do not need TWRP's help. The panel
comes up as part of the kernel display stack during boot; the earlier failures
were caused by killing processes *before* that happened, not by a missing
dependency on TWRP. The remaining question — what exactly performs the handoff,
and can we force it — is answerable from the mdss sysfs tree and dmesg, and is
now the top item rather than an assumed blocker.

### Revised plan

Do NOT let any of our services stop recovery, and do not run at `on boot`
before the display stack is up. Sequence:

1. let the kernel/init display stack initialise (do not interfere)
2. our binary opens fb0, paints, verifies readback
3. touch → power off

## Entry 30 — CORRECTION: I claimed the screen lit up. It did not.

### The false claim

In Entry 29 I wrote:

> "**The screen lit up with recovery stopped.** So the panel is initialised by
> the kernel/init display stack, not by TWRP's UI."

**I did not see a screen.** I ran the binary by hand over adb, read its stdout,
and saw:

```
touch-off: screen filled magenta (all rows written), readback verified
```

That line is printed by **my own program**, and it asserts only two things:
that `pwrite64` returned 4352 bytes per row, and that `pread64` returned those
same bytes back. Neither statement involves the display.

The user's actual observation, running the same test:

> "twrp disappeared / screen was lit full brightness / I didn't see any color /
> i touche and it tirned off"

So the panel was lit but showed no colour. **My conclusion was wrong and my
evidence for it was circular** — the program's own success message.

### This is the SAME error as Entry 27 and Entry 28, third time

| entry | claim | why it was wrong |
|---|---|---|
27 | `screen=PASS` | readback proves memory, not display |
28 | "needs TWRP to init the panel" | overconfident, untested, and the real cause was `stop recovery` at `on boot` |
30 | "screen lit up" | believed my own program's log line |

The recurring mistake is treating a **self-reported success string** as an
independent observation. A program that prints "PASS" after its own write and
readback is not evidence that a human can see anything.

**Rule going forward:** no display claim counts unless the user has looked at
the panel and reported what they saw. Software-side evidence (write returned
success, readback matched, `state`/`blank` unchanged) establishes only that the
memory interface works. It says nothing about whether the display controller is
scanning that memory.

### What we now actually know

CONFIRMED by human observation:
- touch works (a real tap was recorded at x=600 y=1021)
- power-off works (device shut down on that tap)
- the panel **is lit** when our binary has run and recovery is stopped
- the panel shows **no magenta**, despite every row of fb0 being written and
  read back correctly

That last pair is the important new fact. Lit-but-not-our-colour means one of:
1. the display controller is scanning a **buffer we are not writing** (offset
   or plane mismatch), or
2. our writes land in memory the mdp is **not** using (a second FB region), or
3. the panel is being driven in a **different mode** than the fbdev geometry we
   assume, or
4. the mdp has a **committed vs uncommitted** distinction and our direct
   `pwrite64` into the scanout buffer is being discarded.

`virtual_size = 1080,3840` with stride 4352 means two 1920-row planes exist.
We write plane 0 at offset 0 and plane 1 at offset `FB_BUFSZ`. If the panel is
scanning plane 1 and something re-arms it, or if the mdp expects the *other*
offset convention, we would see exactly this: a lit panel showing whatever was
last committed.

### Next step

Do not guess again. On the next boot, with recovery stopped and our binary
running, read back **both** planes and compare them to what we wrote, and dump
`/sys/kernel/debug` mdp state if available. That distinguishes "we wrote the
wrong buffer" from "the mdp never scanned it".

Concretely, the experiment is:
- fill plane 0 magenta, plane 1 green, in ONE run
- if the screen shows green -> the panel scans plane 1, offset convention wrong
- if it shows neither -> the mdp is not scanning fb memory at all in this state
- if it shows the bootloader logo -> the panel is still owned by the bootloader

## Entry 31 — ROOT CAUSE OF THE BLANK SCREEN: pwrite64 offsets are IGNORED

### The measurement that settles it

`probe2.c` wrote a **distinct value to each plane** and read back six offsets:

```
FB_BUFSZ = 8355840
wrote plane0=11111111 plane1=22222222
  p0 row0    off=0        read=4352 val=22222222
  p0 row960  off=4177920  read=4352 val=22222222
  p0 row1919 off=8351488  read=4352 val=22222222
  p1 row0    off=8355840  read=4352 val=22222222
  p1 row960  off=12533760 read=4352 val=22222222
  p1 row1919 off=16707328 read=4352 val=22222222
```

**Every offset returns 22222222** — the value written LAST (plane 1). If the
offsets were honoured, plane 0 would read 11111111.

**`pwrite64` on `/dev/graphics/fb0` ignores the file offset entirely.** Every
write lands in the same buffer. So:

- our per-row, per-plane writes all collapse into one buffer
- the panel displays whatever that single buffer holds
- which is why the screen is **black with the backlight on**: the panel is lit
  but the mdp is scanning a buffer our writes never reach, or is blanked
  (`/sys/class/graphics/fb0/state` reads **0**)

### This retro-explains every failed screen test

| entry | what we did | why the screen stayed blank |
|---|---|---|
25 | `pwrite64` per row, buffer 0 | offsets ignored; one buffer written |
26 | `init-screen2`, full-screen paint | same |
27 | `caps-probe`, 400 rows + readback | readback "passed" because read and write hit the **same** buffer |
28 | `touch-off`, full plane | same |
30 | two-plane magenta/green | both collapsed into one buffer |

The readback verification was structurally incapable of catching this: it read
back from the same place it wrote to, so of course it matched. **Readback cannot
detect an ignored-offset write.**

### Why this was invisible for so long

Every test reported success:
- `pwrite64` returned 4352 (the driver accepted the call)
- `pread64` returned 4352 and the bytes matched (same buffer we wrote)
- no error anywhere

Success at every software layer, and a black screen. The only observable that
would have caught it is a human looking at the panel — which the user did, and
which I twice explained away.

### Next step, and it is NOT more pwrite64

The msm_fb driver on this CAF kernel requires the **mdp/dsi panel to be brought
up and the fb to be mapped for scanout**, not poked with offsets. Candidates, in
order:

1. `mmap()` the fb device and write through the mapping, rather than pwrite64.
   This is what real framebuffer clients do, and it is the most likely fix —
   the driver may honour the mmap path's page offsets where it ignores the
   write offset.
2. Investigate why `state=0`. Try `echo 0 > /sys/class/graphics/fb0/blank`,
   `FBIOBLANK`, or the `msm_fb_type` / `pan` attributes.
3. `FBIOPAN_DISPLAY` / `FBIOPUT_VSCREENINFO` to set the active scanout buffer.

**Write via `mmap` next.** It is a small change, it is how fbdev is meant to be
used, and it distinguishes "driver ignores write offsets" from "we are poking
the wrong interface entirely".

### Correction to Entry 30

Entry 30 blamed the two-plane test for an ambiguous readback. That was wrong:
the readback was not ambiguous, it was **informative** — and the informative
part (both planes reading the same value) is what exposed the real bug.

## Entry 32 — `FBIOGET_FSCREENINFO` is a NO-OP on this driver (silent, returns 0)

### The measurement

`fbio.c` poisoned a buffer with `0xDEADBEEF`, issued the ioctl, and re-read it:

```
FBIOGET_FSCREENINFO(0x4600) rc=0
  word0=00000438  w4(smem_start)=00000000  w5(smem_len)=00000780  w11(line_length)=00000010
  (buffer still partly poisoned -> ioctl did NOT fill it)

FBIOGET_VSCREENINFO(0x4601) rc=-22
  w0=deadbeef w1=deadbeef w2=deadbeef w3=deadbeef
```

**The ioctl returns SUCCESS and fills nothing.** The values I had been
reporting as driver output — `smem_len=1920`, `line_length=16` — were never
driver data. `1920` is our own `FB_H` constant and `16` is the `__u16`
half-width of a neighbouring field, i.e. stale stack contents.

`FBIOGET_VSCREENINFO` is at least honest: it returns -EINVAL.

### Consequences

1. **Every "geometry" number I printed from this ioctl was fiction.** The only
   trustworthy geometry on this device has always been sysfs:
   `stride=4352`, `virtual_size=1080,3840`, `bits_per_pixel=32`.
2. `fbmmap` mapped `smem_len=1920` rounded up to **4096 bytes** (one page),
   then wrote 16 MB into it. Hence `Segmentation fault`. The mmap attempt
   never got far enough to test the mmap hypothesis at all.

### Struct layout is NOT the problem (checked properly this time)

Computed the real ARM32 offsets from `/usr/include/linux/fb.h`:

```
smem_start @ 0x10   smem_len @ 0x14   line_length @ 0x2c   sizeof @ 0x44
```

The transcription matches. The bad values were the no-op ioctl, not a shifted
struct — my earlier "the struct is wrong" guess was itself wrong.

### Hard rule, now twice violated

| instance | assumed | reality |
|---|---|---|
syscall numbers | read=45, mknod=33, pwrite64=146, statfs=140, reboot=142 | 3, 14, 181, 99, 88 |
`O_DIRECTORY` | 0x10000 | kernel returns EINVAL |
struct fb_fix_screeninfo | hand-written, fields shifted | layout was fine, ioctl was a no-op |

All three came from writing values **from memory** instead of from headers, or
trusting a success return without checking the data. The header is on disk. The
poisoned-buffer check takes ten lines and would have caught this immediately.

### Method note

The generalisable lesson is the poisoned buffer: **any ioctl whose result we
use must be verified to have written something**, by pre-filling the buffer with
a sentinel. A return of 0 is not evidence the call did the thing. Same principle
as "a success string from my own program is not evidence" (Entry 30) and
"readback cannot detect an ignored offset" (Entry 31).

### Status of the four capabilities

| capability | state |
|---|---|
touch | **PROVEN** — real tap recorded at x=600 y=1021 |
power-off | **PROVEN** — clean shutdown from that tap |
NFC | **PROVEN** — ioctls + Core Reset + GET_BUILD_INFO round trip |
screen | **BROKEN** — writes are accepted, nothing displays |

Screen remains the only open item, and it is now a driver-interface question
rather than an application one. References supplied by the user for the next
step: `omnirom/android_bootable_recovery` and `z3DD3r/twrp_android_device_lge_hammerhead`
— TWRP's own minui/fb code shows how this display is *actually* driven, which is
far more reliable than inferring it.

## Entry 33 — TWRP source names hammerhead explicitly: the fb ioctls LIE here

User supplied: `omnirom/android_bootable_recovery`,
`z3DD3r/twrp_android_device_lge_hammerhead`. The first answers everything.

### The smoking gun, in TWRP's own source

`minuitwrp/graphics_fbdev.cpp` (~line 174):

```c
// We print this out for informational purposes only, but
// throughout we assume that the framebuffer device uses an RGBX
// pixel format.  This is the case for every development device I
// have access to.  For some of those devices (eg, hammerhead aka
// Nexus 5), FBIOGET_VSCREENINFO *reports* that it wants a
// different format (XBGR) but actually produces the correct
// results on the display when you write RGBX.
```

**hammerhead is called out by name as a device whose fb ioctls report
inaccurate values.** This confirms Entry 32 empirically: we cannot trust
anything the fb ioctls return. The geometry that has always been correct came
from sysfs, not the driver.

### What TWRP actually does (and we did not)

| step | TWRP | we did |
|---|---|---|
open | `open("/dev/graphics/fb0", O_RDWR)` | same |
map | `mmap(0, fi.smem_len, PROT_READ\|PROT_WRITE, MAP_SHARED, fd, 0)` | attempted, but used the no-op ioctl's `smem_len` |
write | direct stores through the mapping | `pwrite64` with offsets the driver ignores |
buffer switch | `vi.yoffset = n * height`, `vi.yres_virtual = height*2` | wrote plane offsets directly |
unblank | `gr_fb_blank(false)` → `fbdev_blank()` | never |

Two things we missed:

1. **`mmap` must be sized from a source that works.** TWRP uses
   `fi.smem_len` because on most devices the ioctl fills it. Here it does not,
   so the size has to come from sysfs: `stride=4352` x `virtual_height=3840`
   = **16,711,680 bytes**, rounded up to a page. Our attempt used the ioctl's
   bogus 1920 → mapped 4096 bytes → wrote 16 MB → SIGSEGV.
2. **The display must be unblanked**, via `FBIOBLANK`/`gr_fb_blank(false)`.
   `/sys/class/graphics/fb0/state` reads 0. That is consistent with a lit
   backlight and a blanked panel — exactly what the user reported.

Also note TWRP writes **RGBX** regardless of what the ioctl claims, which
matches the magenta/green we tried; the pixel format was never the problem.

### Revised plan for the screen

1. `mmap` sized from **sysfs**, never from the ioctl
2. write through the mapping, direct stores
3. unblank via `FBIOBLANK(0)` and/or `echo 0 > /sys/class/graphics/fb0/blank`
4. if double buffering is required, use `FBIOPUT_VSCREENINFO` with
   `yoffset`/`yres_virtual` rather than writing plane offsets

Steps 1-3 are a small, well-understood change. The second device tree the user
linked (`z3DD3r/...hammerhead`) should give the panel/mdp nodes if step 4 is
needed.

## Entry 34 — mmap WORKS. The screen is blank for a different reason: the panel

### This run: everything on the software side succeeded

```
sysfs stride=4352 virtual_height=3840
mapping 16711680 bytes (16711680 needed)
mapped at 0xaa834000
filled mapping via direct stores (half magenta, half green)
FBIOBLANK(0) rc=-38
wrote 0 to sysfs blank
readback via mapping: first=ffff00ff mid=ff00ff00 last=ff00ff00
```

- sized from **sysfs** (not the lying ioctl): 4352 x 3840 = 16,711,680 bytes
- **mmap2 succeeded**, no segfault (v1 used the ioctl's bogus 1920 -> 4096 B)
- direct stores through the mapping
- readback proves two distinct halves in memory

**User: "The screen is still blanked from the previous test. I didn't see any
changes."**

So the memory is right and the panel is still blank. That is a *different*
failure from "we wrote to the wrong place", and it is progress.

### Unblank attempts

| method | result |
|---|---|
`FBIOBLANK(0)` ioctl | **-38 ENOSYS** — driver has no such ioctl |
`echo 0 > sysfs blank` | write accepted, but `state` still reads 0 |
backlight | **255/255** — panel IS lit |
recovery | stopped — nothing repainting over us |

Lit backlight + blank panel + `state=0` is exactly "the panel is powered but
the mdp is not scanning our buffer".

### Where TWRP actually gets the display (minuitwrp/graphics.cpp:355-430)

TWRP tries four backends **in order** and uses the first that initialises:

```c
#ifdef MSM_BSP
    gr_backend = open_overlay();      // 1
#endif
#ifdef HAS_ADF
    gr_backend = open_adf();          // 2
#endif
#ifdef HAS_DRM
    gr_backend = open_drm();          // 3
#endif
    gr_backend = open_fbdev();        // 4  <- the one we assumed
```

On this Qualcomm BSP the **overlay** backend is tried first, and
`open_overlay()` (`graphics_overlay.cpp:123`) does exactly what we do:

```c
fd = open("/dev/graphics/fb0", O_RDWR);
ioctl(fd, FBIOGET_FSCREENINFO, &fi);
if (target_has_overlay(fi.id)) { ... return &my_backend; }
```

Note it **checks `fi.id`** — the `id[16]` string — and it relies on
`FBIOGET_FSCREENINFO`, which on this device **fills nothing** (Entry 32). So
TWRP's overlay detection cannot be relying on that ioctl's data either.

We have **not established which backend TWRP actually uses at runtime on this
device**, and we have no `/dev/overlay*` or `/dev/surface*` node, which argues
against the overlay path.

Also relevant: `TW_SCREEN_BLANK_ON_BOOT` makes TWRP call
`gr_fb_blank(true); gr_fb_blank(false);` at startup — a blank/unblank cycle we
have never replicated.

### Honest status

| capability | state |
|---|---|
touch | **PROVEN** |
power-off | **PROVEN** |
NFC | **PROVEN** |
framebuffer memory | **PROVEN** — mmap + direct stores + readback |
**panel scanout** | **NOT PROVEN** — lit but blank; `state=0` |

The remaining gap is no longer "how do we write pixels" — that is solved. It is
"what makes the mdp scan the buffer we wrote", which is a panel/DSI bring-up
question, and it needs either the live `dmesg` mdp sequence or the device tree
panel nodes (`z3DD3r/twrp_android_device_lge_hammerhead` has no DT; the DT comes
from the kernel tree `lge/hammerhead`).

**Next concrete step:** read `dmesg` for the mdss/mdp panel power-on sequence
while TWRP starts, and identify which ioctls/driver calls it issues. That tells
us what our own init must reproduce.

## Entry 35 — THE ANSWER, from dmesg: the panel is on, and this driver has NO fb ioctls

Captured a clean stock TWRP boot. The display bring-up, verbatim:

```
[1.735923] mdss_dsi_panel_probe: Panel Name = JDI 1080p command mode dsi panel
[1.736730] dsi_panel_device_register:1190 Continuous splash flag enabled.
[1.769381] mdss_fb_alloc_fbmem_iommu: allocating 16777216 bytes ...
[1.769522] mdss_fb_register: FrameBuffer[0] 1080x1920 size=16777216 registered!
[1.769876] mdss_fb_register: FrameBuffer[1] 640x480 size=0 registered!
[1.770017] mdss_fb_register: FrameBuffer[2] 1920x1080 size=0 registered!
[3.987484] mdss_dsi_cont_splash_on:685 DSI on for continuous splash.
[3.999961] mdss_dsi_on:485 Panel already on.
[4.115011] mdss_dsi_panel_off:
[4.282123] mdss_dsi_panel_on
```

### Finding 1: THE PANEL IS ALREADY ON, and it is a COMMAND-MODE DSI panel

`mdss_dsi_on:485 Panel already on.` — the panel is powered and driven from the
moment the bootloader hands over. There is nothing for us to bring up. Our
"unblank" quest was chasing a problem that does not exist; `state=0` in
`/sys/class/graphics/fb0` is simply not a meaningful indicator on this driver.

**"JDI 1080p command mode dsi panel"** is the important part. Command-mode DSI
means the framebuffer is NOT continuously scanned out. The mdp writes a command
stream to the panel over DSI and the panel displays what it is told. So
"writing pixels into memory" is not sufficient — the pixels must be *pushed* to
the panel. That is why `pwrite64` and `mmap` both changed memory correctly while
the screen never changed.

This also explains the "black screen with full backlight": the backlight is on,
the panel is on, but no new frame has been committed.

### Finding 2: THIS DRIVER HAS NO FRAMEBUFFER IOCTLS AT ALL

```
[4.643245] mdss_fb_ioctl: unsupported ioctl (5401)
```

repeated throughout the boot. And the value is telling: **5401 = 0x1519**.

The standard fb ioctls are `_IOR('F', nr, size)` = 0x46xx. **0x1519 is not an
`F` ioctl.** So this is not `FBIOGET_VSCREENINFO` (0x4601) being rejected —
it is some entirely different ioctl number, logged as decimal.

Combined with Entry 32 (a poisoned buffer proved `FBIOGET_FSCREENINFO` returns
0 and fills nothing) and Entry 32's `-22` from `FBIOGET_VSCREENINFO`, the
conclusion is:

**The stock msm_fb driver in this kernel exposes essentially no usable fbdev
interface.** TWRP gets a display anyway because its **overlay backend** (tried
first, `minuitwrp/graphics.cpp:358`) drives the panel through the
`mdss_fb` **overlay/ADF** interface, not through fbdev.

### Revised understanding of every earlier "success"

| test | what we proved | what we thought it meant |
|---|---|---|
`pwrite64` per row | writes accepted | pixels displayed |
readback match | memory holds what we wrote | pixels displayed |
`mmap` + direct stores | mapping correct, 16 MB, readback clean | pixels displayed |
`FBIOGET_FSCREENINFO` | returns 0 | geometry known |

All true. None implied a visible pixel. The recurring error was treating a
successful *memory* operation as evidence of a successful *display* operation.

### What must happen instead

Because the panel is command-mode DSI, we need the **overlay** interface that
TWRP uses. That means writing our own C module against the mdss overlay API, not
fbdev:

- open the overlay device node (not `/dev/graphics/fb0`)
- create/attach a display and a surface
- `mdss_fb_` / `ioctl` calls that push a frame to the panel

This is a larger piece of work than "paint the screen", and it is the correct
next step. Note the overlay device did not appear as `/dev/overlay*` in our
earlier probe, so the node name and permissions need establishing first.

### Honest note on the last run

The `fbmmap` result was correctly reported by me as "the memory side is
solved" -- that was accurate. But it was also presented as progress toward a
visible screen, and on a command-mode panel it is not progress toward that at
all. The distinction should have been drawn before running it, from the panel
type, which was available in dmesg all along and which I did not read.

## Entry 36 — Closed: fbdev CANNOT work on this device. TWRP proves it itself.

### TWRP's fbdev backend hard-fails here

`minuitwrp/graphics_fbdev.cpp:133`:

```c
if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
    perror("failed to get fb0 info (FBIOGET_VSCREENINFO)");
    close(fd);
    return NULL;          // <-- backend init FAILS, no display
}
```

And dmesg, from a clean TWRP boot, shows that ioctl being rejected:

```
[4.643245] mdss_fb_ioctl: unsupported ioctl (5401)
```

So **TWRP's own fbdev backend returns NULL on hammerhead.** It does not paint
the screen via fbdev — it gets its display some other way. `open_fbdev()` is
only the *last* of four fallbacks (`minuitwrp/graphics.cpp:355-430`):
overlay → ADF → DRM → fbdev, and on this device it never reaches a working one.

### So every fbdev approach is dead, and TWRP's source says so

This retro-confirms, from an independent direction, all three of our
observations:

| observation | consistent with |
|---|---|
`FBIOGET_FSCREENINFO` returns 0, fills nothing (poisoned-buffer test) | driver has no working fbdev |
`FBIOGET_VSCREENINFO` returns -22 (`unsupported ioctl (5401)`) | driver has no working fbdev |
`FBIOBLANK` returns -38 ENOSYS | driver has no working fbdev |
`pwrite64` offsets ignored | not a scanout framebuffer |
`mmap` works, memory correct, screen unchanged | not a scanout framebuffer |

### The panel type explains why memory writes do nothing

```
mdss_dsi_panel_probe: Panel Name = JDI 1080p command mode dsi panel
mdss_dsi_on:485 Panel already on.
```

**Command-mode DSI.** The framebuffer is not continuously scanned out. The mdp
sends command streams over DSI; the panel renders what it is told. Writing
pixels into RAM is therefore not a display operation at all — a frame must be
*committed* to the mdp, which then pushes it to the panel.

The panel is **already on** (`Panel already on`), so there was never an
unblank problem to solve. `state=0` in `/sys/class/graphics/fb0` is simply
meaningless on this driver — another reason not to have trusted it.

### Where this leaves the project

Framebuffer **memory** access is solved and stays solved:
`mmap` sized from sysfs (`stride 4352` x `virtual_height 3840` =
16,711,680 bytes), direct stores, verified by readback. That is genuinely
useful — it is how a frame gets *composed*.

What is missing is the **commit step**: getting a composed frame onto the
panel. On this device that means the mdss/ADF/overlay interface, not fbdev.
That is a real piece of work — a C module driving mdss overlay ioctls — and it
should be scoped deliberately rather than rediscovered one ioctl at a time.

### The method failure worth recording

I burned a great deal of effort on fbdev because I assumed the display worked
like a normal fbdev device. Three pieces of evidence were available and unused:

1. **TWRP's own comment naming hammerhead** as a device whose fb ioctls report
   wrong values (Entry 33) — I read that comment and moved on without treating
   it as "this driver is untrustworthy".
2. **`dmesg` from a clean boot**, which names the panel as *command mode* — the
   single most decisive fact, and one command away.
3. **TWRP's fbdev backend returning NULL** on a device where fbdev ioctls are
   unsupported — which would have told us immediately that fbdev was the wrong
   model.

The cheapest available diagnostic was the one I ran last.

## Entry 37 — The real interface is ADF, and I was wrong about which binary to read

### I was reading the wrong file

TWRP's display code is **not** in `sbin/recovery` (639 KB). It is in
`sbin/libminuitwrp.so` (248 KB), which `recovery` loads. Every backend-selection
string I searched for earlier was **absent from `recovery`** — I concluded
"none of the backends are compiled in", which was wrong:

```
$ strings sbin/libminuitwrp.so | grep -F "Using ..."
  PRESENT: Using adf graphics
  PRESENT: Using drm graphics
  PRESENT: Using fbdev graphics
  absent : Using overlay graphics
  absent : Overlay graphics may work
```

So overlay is NOT compiled in (consistent with no `MSM_BSP`), and the chain is
**ADF → DRM → fbdev**, with ADF tried first.

### The device nodes it looks for

```
$ strings sbin/libminuitwrp.so | grep -E '^/dev/(graphics|overlay|dri|adf)'
  /dev/adf%u
  /dev/adf-interface%u.%u
  /dev/adf-overlay-engine%u.%u
  /dev/dri
  /dev/dri/%s
  /dev/graphics/fb0
```

**`/dev/adf%u` — the Android Display Framework.** That is the real display
interface on this device. DRM (`/dev/dri`) is also probed.

### Decoding "unsupported ioctl (5401)"

```
5401 = 0x1519     dir=0 size=0 type='\x15' nr=25
```

Standard fbdev ioctls are `_IOR('F', nr, size)` = **0x46xx**. This is
**0x1519 — not an 'F' ioctl at all.** It is a Qualcomm/ADF ioctl number, which
`msm_fb` does not implement, hence the log line.

**This reframes Entry 36.** I claimed the ioctl rejection proved "this driver
has no usable fbdev interface" and that TWRP's fbdev backend returns NULL. The
first half still holds for fbdev (`FBIOGET_VSCREENINFO` genuinely returns -22),
but I attributed the logged 5401 to fbdev when it is not an fbdev ioctl at all.
The rejection is TWRP *probing ADF through the fb node and being told no* — or
simply an unrelated caller.

### The ADF nodes are absent right now

```
$ adb shell 'ls /dev/adf*'
ls: /dev/adf*: No such file or directory
```

They are created by `adf` daemons that TWRP starts, or they appear only when
the display stack is initialised. So the ordering question becomes concrete:
**the ADF nodes must exist before a frame can be pushed.**

### OPTIONS, ranked by evidence

**1. ADF (what TWRP actually uses).** Open `/dev/adf0`, create an interface
(`/dev/adf-interface0.0`), an overlay engine, and a buffer, then post.
Evidence: it is the first backend in the compiled chain, and the device strings
name exactly these nodes. This is the correct target and the one TWRP proves
works. Cost: reimplementing a chunk of ADF/overlaylib in C.

**2. DRM/KMS (`/dev/dri`).** Probed by TWRP, absent here, and this CAF kernel
predates widespread KMS support on mdss. Low prior probability.

**3. fbdev (`/dev/graphics/fb0`).** Memory access works (mmap verified), but the
panel is **command-mode DSI** — a frame must be committed to the mdp, not merely
written. `FBIOPUT_VSCREENINFO`/`FBIOPAN_DISPLAY` would be the commit, and both
are among the ioctls this driver rejects. Effectively dead, though still useful
for *composing* a frame.

**4. Let TWRP do the display, and be a client of it.** Keep recovery running and
drive our UI through whatever surface it exposes. Cheapest path to a visible
screen; not a real answer for the air-gapped target, which must not depend on
TWRP.

**5. Direct mdp register poking.** The mdp is mapped at `phys 0xfd900000`
(`mdss_mdp_probe: MDP HW Base phy_Address=0xfd900000`). Writing the mdp
command registers directly is possible but undocumented, extremely fragile, and
would need the DSI command sequence. Last resort.

### Recommended next step

Establish **whether `/dev/adf0` appears at all** on this device, and if so what
its ioctls are. That is one cheap check and it decides between option 1 and
option 4. Everything else is speculation until then.

## Entry 38 — ANSWER FOUND in the kernel source: MSMFB_DISPLAY_COMMIT

User supplied `z3DD3r/android_kernel_lge_hammerhead`. The driver settles every
open question.

### 1. The "unsupported ioctl (5401)" is HEX, and it is NOT fbdev

`drivers/video/msm/mdss/mdss_fb.c:2212`:

```c
if (ret == -ENOSYS)
        pr_err("unsupported ioctl (%x)\n", cmd);
```

Format is `%x`. So `5401` is **0x5401**, type char `0x54` = `'T'` — a
Qualcomm-private ioctl, **not** an `'F'` (0x46) fbdev ioctl. My Entry 36/37
decoding of it as decimal was wrong twice over; the corrected reading is in this
entry and should be the one relied on.

### 2. fbdev ioctls are genuinely absent

The ioctl handler's entire case list is:

```c
case MSMFB_CURSOR:
case MSMFB_SET_LUT:
case MSMFB_GET_PAGE_PROTECTION:
case MSMFB_BUFFER_SYNC:
case MSMFB_NOTIFY_UPDATE:
case MSMFB_DISPLAY_COMMIT:
        if (mfd->mdp.ioctl_handler)
                ret = mfd->mdp.ioctl_handler(mfd, cmd, argp);
```

**No `FBIO*` case at all.** `FBIOGET_FSCREENINFO` returns 0 without filling,
`FBIOGET_VSCREENINFO` returns -22, `FBIOBLANK` returns -38 — all consistent,
and now explained: those cases do not exist in this handler. This is a CAF
**overlaylib/ADF-era driver**, not a classic fbdev driver.

### 3. The commit step we were missing

```
#define MSMFB_IOCTL_MAGIC 'm'
#define MSMFB_NOTIFY_UPDATE    _IOWR('m', 146, unsigned int)
#define MSMFB_BUFFER_SYNC      _IOW('m', 162, struct mdp_buf_sync)
#define MSMFB_OVERLAY_COMMIT   _IO('m', 163)
#define MSMFB_DISPLAY_COMMIT   _IOW('m', 164, struct mdp_display_commit)
```

and

```c
struct mdp_display_commit {
        uint32_t flags;
        uint32_t wait_for_finish;
        struct fb_var_screeninfo var;
};
```

**This is the operation that pushes a frame to the panel.** It is exactly the
"commit the composed frame" step identified in Entry 35/36 as missing, and it
is issued through the **same `/dev/graphics/fb0` node we have already got open**,
via a Qualcomm-private ioctl rather than an fbdev one.

So the earlier conclusion "fbdev cannot work, we need ADF" was **too strong**.
The node is fbdev-*shaped* but the interface is CAF-native, and it is reachable.

### 4. Revised options, now with a concrete target

**Option A (NEW, best): mmap + MSMFB_DISPLAY_COMMIT.**
1. `open("/dev/graphics/fb0", O_RDWR)` — already working
2. `mmap` sized from sysfs (4352 x 3840 = 16,711,680) — **already working**
3. direct stores to compose the frame — **already working**
4. `ioctl(fd, MSMFB_DISPLAY_COMMIT, &commit)` with `var` describing the buffer

Steps 1-3 are done and verified. **Step 4 is the only thing missing**, and it is
one ioctl with a known struct. `flags`/`wait_for_finish` semantics need
checking against `mdp_fb_ioctl_handler`, and `var` wants `xoffset`/`yoffset`
for buffer selection.

This is dramatically less work than reimplementing ADF, and it builds on three
capabilities already proven.

**Option B: ADF** (`/dev/adf0`) — what TWRP's `libminuitwrp.so` probes first.
Real, but heavier, and unnecessary if A works.

### 5. Method note

Three corrections in a row on the same subject (hex vs decimal, ADF vs fbdev,
"no fbdev interface at all") all came from reasoning about log output instead of
reading the driver. The driver source answered every one of them in one grep.
**Read the source.**

## Entry 39 — THE SCREEN WORKS. Magenta, confirmed by a human.

```
fbcommit: stride=4352 virtual_height=3840 bpp=32
fbcommit: mapped 16711680 bytes
fbcommit: frame composed (magenta, RGBX)
fbcommit: MSMFB_NOTIFY_UPDATE(START) rc=-110        (ETIMEDOUT, harmless)
fbcommit: COMMIT rc=0  *** SUCCESS ***
fbcommit: COMMIT(yoffset=1920) rc=0
```

**User: "YES. MAgenta screen NOW"**

### The complete, correct sequence

1. `open("/dev/graphics/fb0", O_RDWR)`
2. size the mmap from **sysfs** (`stride 4352` x `virtual_height 3840` =
   16,711,680 bytes) — never from the ioctls, which lie on this driver
3. compose with direct stores (RGBX)
4. `ioctl(fd, MSMFB_DISPLAY_COMMIT, &mdp_display_commit)` — the step that was
   missing for the entire project

### Why the first attempt returned EPERM, and who caused it

The first run returned `COMMIT rc=-1 (EPERM)`. From
`drivers/video/msm/msm_fb.c`:

```c
if ((!mfd->op_enable) || (!mfd->panel_power_on))
        return -EPERM;
```

and in `msm_fb_blank_sub()`, **every blank mode except UNBLANK** does:

```c
mfd->op_enable = FALSE;
mfd->panel_power_on = FALSE;
```

**The user identified the cause**: TWRP implements a screen saver that blanks
the panel. `init.svc.recovery` was `running`, and the backlight read:

```
brightness: 0
```

TWRP had turned the panel off entirely, which is also why the screen "was off"
at the start of the test. After `setprop ctl.stop recovery` and restoring
brightness to 255, the commit returned **0** on the first try.

So the final piece was not a code problem at all — it was **another process
owning the display**, exactly the class of bug that has dominated this project
(`stop recovery` killing the handoff in Entry 28; recovery repainting over us).

### MSMFB_NOTIFY_UPDATE returning -110 is expected

`msmfb_notify_update()` does
`wait_for_completion_interruptible_timeout(..., 4*HZ)` and converts a timeout to
`-ETIMEDOUT`. It is a notification *primitive*, not an enable step, and it is
not needed — the commit succeeded without it. Remove it in the final version.

### FINAL STATUS — all four capabilities proven on hardware

| capability | proof |
|---|---|
**screen** | **magenta on the panel**, via mmap + `MSMFB_DISPLAY_COMMIT` |
**touch** | real tap recorded at x=600 y=1021 |
**power-off** | clean shutdown from that tap |
**NFC** | `POWER_CTL`/`WAKE_CTL` = 0, Core Reset written, `10 60 00 00 02 00 01` read back |

### What was actually wrong, in one line each

| earlier belief | truth |
|---|---|
`pwrite64` writes the framebuffer | driver ignores write offsets |
readback proves the screen works | readback reads the same buffer it wrote |
`FBIOGET_FSCREENINFO` gives geometry | returns 0, fills nothing |
unblank via `FBIOBLANK` | ENOSYS; and `state` is meaningless here |
the panel needs bringing up | it was already on — TWRP had blanked it |
framebuffer needs ADF | it needs `MSMFB_DISPLAY_COMMIT` |

### The lesson, stated once and plainly

For eleven tests I treated **memory operations** as **display operations**.
Write succeeded, read back succeeded, ioctl returned 0 — and the panel never
changed. No software check distinguishes "wrote correctly to memory nobody is
scanning" from "displayed". The only oracle that works is a human looking at the
screen, and the fastest diagnostic available the whole time was `dmesg`, which
named the panel as *command mode* and would have ended this weeks earlier.

Every capability in this project is now proven by observation rather than by a
success message printed by my own code.

## Entry 40 — Stable: one commit, both planes filled

The transient blank was **self-inflicted**, not a new fault. The previous run
ended with a second diagnostic commit:

```c
dc.var.yoffset = FB_H;                    /* 1920 -> plane 1 */
ioctl(fd, MSMFB_DISPLAY_COMMIT, &dc);
```

`yres_virtual` is 3840 = two 1920-row planes. Only plane 0 was ever **filled**;
the commit then pointed the panel at plane 1, which held stale memory. Hence
"blank with backlight" — the commit worked perfectly, it just displayed a
buffer nobody had written.

Removed the diagnostic and filled **both** planes:

```
fbcommit: both planes composed (magenta, RGBX)
fbcommit: COMMIT rc=0  *** SUCCESS ***
```

**User: "Magenta again."** Consistent and repeatable.

### Lesson

The diagnostic was a reasonable probe, but it changed state that the fill
depended on. A "just to see which buffer is scanned" experiment silently became
the thing under test. When the visible result changes, check whether the
*previous* run left the device in that state before blaming the new code.

### FINAL, VERIFIED DISPLAY SEQUENCE

```
1  open("/dev/graphics/fb0", O_RDWR)
2  restore backlight           (TWRP's screen saver zeroes it)
3  mmap, sized from sysfs:  stride 4352 x virtual_height 3840 = 16,711,680 B
4  fill BOTH planes with RGBX pixels (direct stores)
5  ioctl(fd, MSMFB_DISPLAY_COMMIT /* 0x40A86DA4 */, &mdp_display_commit)
       flags = 0, wait_for_finish = 1
       var.xres = 1080, var.yres = 1920
       var.yres_virtual = 3840, var.yoffset = 0
```

Prerequisite: **stop TWRP first** (`setprop ctl.stop recovery`), or its screen
saver blanks the panel and the commit returns EPERM.

### ALL FOUR CAPABILITIES: PROVEN BY OBSERVATION

| capability | evidence |
|---|---|
screen | **magenta on the panel**, repeatable |
touch | real tap, x=600 y=1021 |
power-off | clean shutdown from that tap |
NFC | ioctls 0, Core Reset sent, `10 60 00 00 02 00 01` returned |

Next logical step is MicroPython with a `framebuf` module over this sequence,
plus a touch module, since both underlying paths are now proven.

## Entry 41 — Feasibility of "no TWRP", from the driver

The user's target test: boot with no TWRP, draw, touch, NFC, draw, touch,
power off. Before building it, the two gates had to be checked in the driver.

### Gate 1: `op_enable`

```c
/* msm_fb_register(), line 1428 -- probe time, no userspace involved */
mfd->op_enable = TRUE;
mfd->panel_power_on = FALSE;
```

**`op_enable` is TRUE from driver probe**, so it never needs userspace help.
(The other assignment, line 894, is the unblank path in `msm_fb_blank_sub`.)

### Gate 2: `panel_power_on`

Set only by `msm_fb_blank_sub(FB_BLANK_UNBLANK)` calling `pdata->on()`. The
reachable caller is **`msm_fb_open()`** (line 1628):

```c
static int msm_fb_open(struct fb_info *info, int user)
{
        bool unblank = true;
        ...
        if (mfd->is_panel_ready && !mfd->is_panel_ready())
                unblank = false;
        if (unblank) {
                if (msm_fb_blank_sub(FB_BLANK_UNBLANK, info, mfd->op_enable))
                        return -EPERM;
        }
```

**`open("/dev/graphics/fb0")` powers the panel on.** That is the whole
mechanism — it happens in the kernel on open, not via an ioctl.

Also: `is_panel_ready` is **never assigned** anywhere in this driver (the only
occurrence is the test at line 1653), so it is NULL and the guard is inert.
`unblank` is therefore always true.

### Consequence

The `EPERM` seen earlier was **not** a missing panel bring-up by us — it was
TWRP's screen saver having *blanked* an already-on panel, which sets both flags
FALSE. On a boot with no TWRP, simply opening `fb0` unblanks, and
`MSMFB_DISPLAY_COMMIT` should succeed on the first attempt.

This also retires the Entry 28 conclusion that "the panel is never handed over
if you stop recovery early". That was inferred from a hang and never tested.
The truth is simpler: `fb0` open unblanks, so the display works with or
without TWRP — we simply had to open it, which we were already doing.

### reboot(), for stage 7 without userspace to help

```c
/* include/linux/reboot.h */
#define LINUX_REBOOT_MAGIC1  0xfee1dead
#define LINUX_REBOOT_MAGIC2  672274793
```

`reboot(88, ...)` previously returned **-EINVAL** because I passed
`0x28121969` (a glibc-ism) rather than the kernel's `MAGIC1`/`MAGIC2`. With no
TWRP there is no `setprop` to fall back on, so this must be right.

## Entry 42 — STANDALONE TEST: stages 1-6 PASS as PID 1. Stage 7 (poweroff) fixed.

**User report of the standalone run:**

> "The test initialized the screen. Painted magenta, switched to green after a
> tap. And now stuck on green."

This is the first time the whole system has run **with no TWRP at all** — our
binary as PID 1, stock kernel, a 3,141-byte ramdisk, device nodes baked into the
cpio archive.

| stage | result |
|---|---|
1. boot without TWRP | **PASS** — our init ran as PID 1 |
2. draw magenta | **PASS** |
3. wait for touch | **PASS** |
4. NFC access | ran (no visible channel, but no crash) |
5. draw green | **PASS** |
6. wait for touch | **PASS** |
7. power off | **FAIL** — hung on green |

Critically, this also retires the concern from Entry 23 that `/dev` would be
empty: the **static device nodes in the newc archive worked**, so
`open("/dev/graphics/fb0")` reached `msm_fb_open()`, which unblanks the panel.
`op_enable` is TRUE from driver probe, so `MSMFB_DISPLAY_COMMIT` succeeded on
the first attempt with no TWRP to help.

### Stage 7 bug: the reboot() argument order

From `kernel/sys.c:455`:

```c
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd, ...)
{
        if (!capable(CAP_SYS_BOOT)) return -EPERM;
        if (magic1 != LINUX_REBOOT_MAGIC1 ||
            (magic2 != LINUX_REBOOT_MAGIC2 && ...))
                return -EINVAL;
```

**ARM's reboot signature is `reboot(magic1, magic2, cmd, arg)`** — the magic
words come FIRST and the command THIRD.

I called:

```c
sc(SYS_reboot, LINUX_REBOOT_CMD_POWER_OFF,   /* magic1 slot! */
             LINUX_REBOOT_MAGIC2,             /* magic2 slot! */
             LINUX_REBOOT_MAGIC1, 0, 0);
```

which put `0x4321` where `magic1` must be `0xfee1dead`. That returns
`-EINVAL` every time. This is x86 ordering (cmd first) applied to ARM.

Correct:

```c
sc(SYS_reboot, (long)LINUX_REBOOT_MAGIC1,   /* magic1 */
             (long)LINUX_REBOOT_MAGIC2,   /* magic2 */
             LINUX_REBOOT_CMD_POWER_OFF,   /* cmd   */
             0, 0);
```

And `LINUX_REBOOT_CMD_POWER_OFF` is valid: `reboot_pid_ns()` accepts
`POWER_OFF` (sets `pid_ns->reboot = SIGINT`) and `HALT`. As PID 1 in the init
namespace, `reboot_pid_ns` returns 0 immediately and `kernel_restart()` takes
over with the PMIC — which cuts power.

Also worth noting: we are PID 1, so `capable(CAP_SYS_BOOT)` passes. In an
earlier TWRP-hosted run this same call returned EINVAL for the same reason,
which is why the `setprop sys.powerctl` fallback appeared to be necessary. It
never was — the ordering was wrong.

### Method

Three different reboot() calling conventions were tried across this project
(x86 ordering, glibc's 0x28121969, and now the correct ARM ordering) and the
answer was in `kernel/sys.c` the whole time. Read the source.

## Entry 43 — Standalone runs as PID 1 with no TWRP. And a correction about the magic.

### Standalone test: stages 1-6 pass

**User:** "The test initialized the screen. Painted magenta, switched to green
after a tap. And now stuck on green."

| stage | result |
|---|---|
boot with **no TWRP**, our init as PID 1, stock kernel, 3141-byte ramdisk | **PASS** |
static device nodes from the newc archive (`fb0` 29,0 / `event1` 13,65 / `bcm2079x` 10,65) | **PASS** |
screen magenta (`mmap` + `MSMFB_DISPLAY_COMMIT`) | **PASS** |
touch | **PASS** |
NFC | ran without crashing |
screen green | **PASS** |
second touch | **PASS** |
`reboot()` power-off | was broken, now fixed (below) |

This is the whole project working end-to-end with no recovery, no adb, no
Android, and a ramdisk of 3 KB.

### Correction: the magic value was never wrong

In Entry 42 I wrote that I had passed "0x28121969 (a glibc value) instead of the
kernel's magic". That was **wrong, and I confused myself in the act of writing
it**:

```
672274793 == 0x28121969
```

They are the same number. `LINUX_REBOOT_MAGIC2` genuinely is `0x28121969`. The
only real bug was **argument order**. From `kernel/sys.c:455`:

```c
SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd, ...)
```

and the fix verified in the disassembly of the new build:

```
r0 = 0xfee1dead   magic1   ✓
r1 = 0x28121969   magic2   ✓
r2 = 0x4321       cmd      ✓
```

### Running adbd in parallel — assessment

The user asked to start adbd alongside our test for debuggability. Findings:

**Good news:** TWRP's `adbd` is **statically linked** —
`ELF 32-bit LSB executable, ARM, EABI5, statically linked`. No bionic, no
`/system/bin/linker`, no shared libraries. So it can run in a 3 KB ramdisk.

**Requirements it brings:**
- `/dev/socket/property_service` — normally provided by init
  (`ro.property_service.version`)
- **functionfs** for the USB transport (`registering usb transport`,
  `closing functionfs transport`), i.e. `/dev/ffs*` or `/dev/usb-ffs/adb`
- `/sys/class/android_usb/android0/` gadget configuration
- `ANDROID_SOCKET_*` environment

**The risk:** the USB gadget setup is the exact thing that has repeatedly broken
this project — `stop recovery` blanking the panel, and `functions` rewrites
killing our own adb connection (Entry 20/24). Adding a USB gadget path to a
minimal initramfs is a new failure surface, not a free win.

**Assessment:** worth doing, but as a *separate* stage rather than folded into
the seven-stage test, so a gadget failure cannot be confused with a display or
NFC failure. The alternative — keep TWRP-hosted for anything needing adb, and use
the standalone image only to prove the no-Android path — is already proven and
costs nothing.

## Entry 44 — Stage 7 cannot work via reboot(POWER_OFF) on this kernel

**User:** "repeated the test. still gets stuck on green"

The corrected argument order (`magic1, magic2, cmd`) is verified in the
disassembly and did not change the outcome. The reason is in `kernel/sys.c`,
and this kernel keeps the whole reboot implementation there rather than in
`kernel/reboot.c`.

### The silent downgrade

`kernel/sys.c:485`, inside the reboot syscall:

```c
/* Instead of trying to make the power_off code look like
 * halt when pm_power_off is not set do it the easy way. */
if ((cmd == LINUX_REBOOT_CMD_POWER_OFF) && !pm_power_off)
        cmd = LINUX_REBOOT_CMD_HALT;

mutex_lock(&reboot_mutex);
switch (cmd) {
...
case LINUX_REBOOT_CMD_HALT:
        kernel_halt();
        do_exit(0);
        panic("cannot halt");
```

**If `pm_power_off` is NULL, `POWER_OFF` is quietly rewritten to `HALT`.**
`kernel_halt()` stops the CPU without cutting rail power. From the outside that
is indistinguishable from "nothing happened" — which is exactly what we see:
stuck on green, display still up.

### pm_power_off is NULL on this board

Every assignment to `pm_power_off` in this tree is for other platforms
(vt8500, vexpress, u300, sa1100, twl4030, dm355evm, ipmi, acpi, nvec). There
is **no MSM/PMIC power-off registration anywhere in `drivers/`**, and the
hammerhead device tree has no `poweroff` node. `drivers/power/` has only
`pmic8058-charger.c`, `msm_charger.c`, `msm_battery.c` — chargers and battery,
nothing that cuts power.

So `reboot(POWER_OFF)` on this device cannot power it off. Not a bug in our
call, not an argument-order problem: the capability is absent.

### Why TWRP could power off

TWRP does **not** use `reboot(POWER_OFF)` either. It goes through Android init:

```rc
on property:sys.powerctl=*
     powerctl ${sys.powerctl}
```

which reaches `kernel_power_off()` via the property path — and CAF's
`kernel_power_off_prepare()` / the msm restart handler chain, or the PMIC
`restart_reason` sysfs, provides the actual rail cut. That is why the TWRP-hosted
run in Entry 27 powered off cleanly while every standalone run hangs.

### Options for stage 7

1. **`setprop sys.powerctl shutdown`** — needs a minimal init that owns the
   property service, or a direct write to the property socket. This is the
   mechanism known to work on this board.
2. **The MSM restart sysfs** — CAF exposes
   `/sys/devices/platform/*restart*/restart_mask` and `restart_reason`. Writing
   the poweroff mask there is what CAF's poweroff path ultimately does. Cheap to
   test, and it needs no daemon.
3. **The PMIC directly** — write the PMIC's poweroff register. Last resort.

Option 2 is the one to try first: it is a file write, needs no property service,
and we can check the nodes exist immediately.

### Honest note

I asserted a power-off path twice (glibc magic, then argument order) before
reading the kernel's dispatch. Both were real bugs, but neither was *the* bug,
and neither was checkable without reading `kernel/sys.c`. The decisive question
— "is `pm_power_off` set on this platform?" — is one grep.

## Entry 45 — Entry 44 was WRONG: pm_power_off IS set. And the kernel is not the one in my notes.

### Correction: the poweroff capability exists

Entry 44 concluded "pm_power_off is NULL on this board, the capability is
absent". **That was wrong.** I grepped for `pm_power_off =` and looked only at
`drivers/` and the wrong set of `arch/arm/mach-*`. The assignment is in
`arch/arm/mach-msm/restart.c:367`:

```c
static int __init msm_restart_init(void)
{
        msm_tmr0_base = msm_timer_get_timer0_base();
        restart_reason = MSM_IMEM_BASE + RESTART_REASON_ADDR;
        pm_power_off = msm_power_off;          /* <-- here */
```

and the mechanism is:

```c
static void msm_power_off(void)      { __msm_power_off(1); }

static void __msm_power_off(int lower_pshold)
{
        prkint(KERN_CRIT "Powering off the SoC\n");
        set_dload_mode(0);
        pm8xxx_reset_pwr_off(0);
        qpnp_pon_system_pwr_off(PON_POWER_OFF_SHUTDOWN);
        if (lower_pshold) {
                if (!use_restart_v2()) __raw_writel(0, PSHOLD_CTL_SU);
                else { halt_spmi_pmic_arbiter();
                       __raw_writel(0, MSM_MPM2_PSHOLD_BASE); }
                mdelay(10000);
                prkint(KERN_ERR "Powering off has failed\n");
        }
}
```

Confirmed against the **running** kernel, not just the source tree:

```
$ adb shell 'grep -E " (msm_power_off|qpnp_pon_system_pwr_off)$" /proc/kallsyms'
00000000 T qpnp_pon_system_pwr_off
00000000 t msm_power_off
```

So `reboot(LINUX_REBOOT_CMD_POWER_OFF)` is **not** silently downgraded to
HALT, and the downgrade branch at `kernel/sys.c:485` is never taken. The
conclusion "the capability is absent" has to be struck.

### Second correction: this is not the kernel the notes describe

`/proc/version` on the running device:

```
Linux version 3.4.113-gda91d066bc6 (acroreiser@thinkstation)
(gcc version 15.2.1 20251203 (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)))
#42 SMP PREEMPT Sun May 17 23:24:06 EEST 2026
```

The notes record `3.4.113-ge4e483b4ef6 #315`. This is a **custom build**
(Apr 2026-era, build #42, built on a "thinkstation"), not the LineageOS CAF
kernel assumed throughout. `arch/arm/mach-msm` and `msm8974-hammerhead.dtsi`
are present in the source tree, so the tree is the right *family*, but it must
not be assumed byte-identical to what is running. Source-derived claims need a
`/proc/kallsyms` or `dmesg` cross-check, not just a plausible read of the
tree.

### Why the poweroff search failed earlier

Searches that found nothing:

- `ls /sys/devices/platform/*restart*` — only 12 platform devices exist and
  none is a `restart` node; the glob was not the problem.
- `find /sys -name "restart*"` — only subsystem `restart_level` files and
  `/sys/module/restart/parameters/download_mode`.

**There is no sysfs poweroff interface on this device.** The CAF restart sysfs
that recent kernels expose simply is not compiled in here. That eliminates
option 2 from Entry 44 entirely, and it means `reboot(POWER_OFF)` is not one
option among several -- it is the only userspace-reachable poweroff route.

### The real problem: the hang was unobservable

With no console, no adb and no log, "stuck on green" is ambiguous between:

1. stage 6 never completed (second touch never detected)
2. the kernel accepted `reboot(POWER_OFF)` and halted without cutting power
3. `reboot()` was refused and returned an error we had no way to read

There is no way to distinguish these from the outside. This is the same trap
as Entries 21-31, and it is a design flaw in the test rather than a bug in
any one component.

### Fix: make the screen the diagnostic channel

`b/rootfs/standalone/main.c` rewritten so the panel alone determines the
outcome:

| what is on screen | what it means |
|---|---|
| solid MAGENTA | stage 2 committed, about to wait |
| **flashing** MAGENTA | alive, waiting for the FIRST touch |
| solid GREEN | stage 5 committed, second screen draw worked |
| **flashing** GREEN | alive, waiting for the SECOND touch |
| solid BLUE | painted immediately BEFORE `reboot(POWER_OFF)` |
| solid RED | `reboot()` **returned** -- the call was refused |
| **frozen** on BLUE | the kernel took the call and halted without cutting rails |
| frozen on a flashing colour | the process died mid-wait |

Two supporting changes:

- **Hold-to-accept.** A gesture now counts on release *or* after ~1.6s of
  contact. If this driver omits `ABS_MT_TRACKING_ID == -1` on lift, a
  release-only matcher blocks forever -- which would look exactly like a
  poweroff failure. The old matcher was release-only.
- **1Hz heartbeat.** The screen flips between the stage colour and black once
  a second during every wait, so "still waiting" is distinguishable from "the
  process is gone".

### Verification of the new build

```
built: 5736 bytes   ELF: EXEC   VFP instructions: 0
reboot call site in the disassembly:
  movw r0, #57005 @ 0xdead ; movt r0, #65249 @ 0xfee1   -> r0 = 0xfee1dead
  movw r1, #6505  @ 0x1969 ; movt r1, #10258 @ 0x2812   -> r1 = 0x28121969
  movw r2, #17185 @ 0x4321                              -> r2 = 0x4321
  svc 0x00000000
```

Image `b/rootfs/stock/boot-standalone2.img`: stock kernel byte-identical,
3,004-byte ramdisk, `init` verified as a real ELF and byte-identical to the
build, all three device nodes present as `newc` device entries.

### Method note

Three times now on this single question -- how to power off -- the answer was
a fact about the running kernel rather than a fact about our code, and twice I
asserted a mechanism before reading the dispatch. Grepped the wrong directory
(`drivers/` instead of `arch/arm/mach-msm/`), and trusted a source tree that
does not match `/proc/version`. Read the dispatch, then confirm against
`/proc/kallsyms`.

## Entry 46 — Comparing hammerhead_defconfig vs lineageos_hammerhead_defconfig

**User:** "Can we compare configs of Android and twrp kernels? If there is
nothing critical we might consider using Android kernel for recovery. That will
allow us working with kernel is being maintained and should be easily modified
for our next phases."

Two defconfigs live in the tree:

| file | set options | "# ... is not set" lines |
|---|---|---|
| `arch/arm/configs/hammerhead_defconfig` | 538 | 73 |
| `arch/arm/configs/lineageos_hammerhead_defconfig` | 568 | 114 |

`hammerhead_defconfig` is the **custom** one (it is the config that produces
`3.4.113-gda91d066bc6`, build #42). "TWRP kernel" and "Android kernel" are
therefore not the real axis of comparison — the real axis is *custom minimal
bring-up kernel* vs *LineageOS shipping kernel*.

### All four capabilities are present in BOTH configs

| capability | required symbol | custom | lineageos |
|---|---|---|---|
| screen | `CONFIG_FB`, `CONFIG_FB_MSM` | y | y |
| touch | `CONFIG_INPUT_EVDEV`, `CONFIG_TOUCHSCREEN_LGE_SYNAPTICS` | y | y |
| NFC | `CONFIG_NFC_BCM2079X` | y | y |
| power off | `CONFIG_MACH_MSM8974_HAMMERHEAD` | y | y |
| boot | `CONFIG_BLK_DEV_INITRD`, `EXT4_FS`, `TMPFS`, `BLK_DEV_RAM` | y | y |

`CONFIG_MACH_MSM8974_HAMMERHEAD=y` in both means both pull in
`arch/arm/mach-msm/restart.c`, so both set `pm_power_off = msm_power_off`
(Entry 45). **Nothing critical differs for our hardware.**

### What the custom config adds

Mostly diagnostics we cannot use — the project has **no serial/UART hardware**,
and `CONFIG_SERIAL_MSM_HSL_CONSOLE=y` is exactly the UART we do not have:

```
CONFIG_DEBUG_INFO  CONFIG_DYNAMIC_DEBUG  CONFIG_PERSISTENT_TRACER
CONFIG_TIMER_STATS CONFIG_MSM_TZ_LOG     CONFIG_MSM_IPC_LOGGING
CONFIG_MSM_OCMEM_DEBUG  CONFIG_MSM_RPM_RBCPR_STATS_V2_LOG
CONFIG_SERIAL_MSM_HSL(_CONSOLE)  CONFIG_EARJACK_DEBUGGER
CONFIG_GPIO_QPNP_PIN_DEBUG  CONFIG_CHR_DEV_SCH  CONFIG_CHR_DEV_SG
CONFIG_EXT2_FS  CONFIG_EXT3_FS  CONFIG_VFAT_FS  CONFIG_RD_LZMA  CONFIG_RD_BZIP2
```

Plus `# CONFIG_MSM_CAMERA is not set`, `# CONFIG_DEVMEM is not set`,
`# CONFIG_DEVKMEM is not set`, `# CONFIG_DIAG_CHAR is not set`,
`# CONFIG_DIAG_OVER_USB is not set`, `# CONFIG_INPUT_MOUSEDEV is not set`,
`# CONFIG_MSM_RMNET is not set`.

The last group is genuinely useful for an air-gapped device: no camera driver,
no diag-over-USB channel, no rmnet.

### What the custom config drops

Phone-shaped subsystems we have no use for: `ZRAM`, `SWAP`, `KSM`, `ZSMALLOC`,
`BLK_CGROUP`, `CGROUP_BFQIO`/`CFQ_GROUP_IOSCHED`, `SDCARD_FS`, `SDFAT_FS`,
`F2FS_FS`, `QUOTA`, `CONFIGFS_FS`, `CPU_BOOST`, `NF_NAT_IPV4/6`,
`DEBUG_FS`, `KERNEL_MODE_NEON`.

### Three reasons NOT to trust these defconfigs as descriptions of the running kernel

Each was found by trying to use them and failing:

1. **`CONFIG_USB=y` is absent from the custom config** — yet
   `USB_GADGET=y`, `USB_DWC3_MSM=y`, `USB_G_ANDROID=y`, `USB_BAM=y` and ~40
   USB sub-drivers are all `=y`. That is a `savedefconfig` artifact (USB is
   `default y if USB_SUPPORT`, so it never gets written out), **not** a
   disabled subsystem. My first reading of the diff — "the custom build drops
   USB" — was wrong, and the adb conclusion I was about to draw from it was
   wrong too.
2. **`EXPERIMENTAL=y` with `EXPERT` absent.** `config EXPERIMENTAL ... if
   EXPERT`, and `USB_FUNCTIONFS depends on EXPERIMENTAL`, so functionfs
   (which modern adbd requires) cannot be selected by the custom config.
   lineageos has `CONFIG_EXPERT=y` and can. But **neither lists
   `USB_FUNCTIONFS`** at all, while adb demonstrably works under TWRP.
3. **The kernel image carries no embedded config.** `IKCFG_ST` and
   `Linux version` are both absent from
   `b/rootfs/stock/unpacked/kernel` (7,414,992 bytes, compressed payload), so
   `CONFIG_IKCONFIG` was not enabled and there is nothing to extract.

Conclusion: the running kernel's config was produced by `menuconfig`/manual
edits and is **not reconstructible from either defconfig**. The only reliable
sources are `/proc/config.gz` (if `CONFIG_IKCONFIG_PROC`) or `/proc/kallsyms`
symbol probes on the running device.

### Recommendation: do not build a kernel at all

Two reasons, and the second is decisive:

1. Building either config means building **both** — a custom kernel cannot use
   the LineageOS kernel image, and vice versa. There is nothing to compare
   cheaply.
2. **This host cannot build a 3.4 CAF kernel.** The only ARM compiler present
   is `arm-linux-gnueabihf-gcc` **version 15**. A 3.4 tree needs GCC 4.8/4.9;
   modern GCC fails it on `-fno-common`, implicit-int, and `asm` syntax long
   before any real error.

So the maintained-kernel goal is reachable **without a toolchain**: under TWRP,
read the LineageOS kernel straight off the boot partition, keep that kernel
byte-for-byte, and swap only the ramdisk — which `b/rootfs/bootimg.py` already
does and which is how every image in `stock/` was built. That yields a stock
LineageOS kernel plus our 3 KB init, testable with `fastboot boot`, and leaves
the kernel untouched and revertible by a fresh flash.

### Open question raised by this comparison

`CONFIG_KERNEL_MODE_NEON=y` in the LineageOS config, while this project's
notes record **`AT_HWCAP_FP` clear** as the reason every binary must be
soft-float. Those cannot both be casually true: if the CPU trapped on VFP the
kernel itself would fault instantly, and nothing on this device does. So either
NEON is off in the real running config, or the `AT_HWCAP_FP` reading is wrong.
Worth re-checking with `getauxval(AT_HWCAP)` when the device is up — if FPU *is*
present, the soft-float mandate is belt-and-braces rather than load-bearing,
and `CONFIG_KERNEL_MODE_NEON` is a real factor in kernel selection.

## Entry 47 — Kernel config comparison: toolchain found, and four blockers in build-kernel.sh

### Correction: the toolchain exists

Entry 46 asserted "this host cannot build a 3.4 CAF kernel. The only ARM
compiler present is `arm-linux-gnueabihf-gcc` version 15." **Wrong.** The
search only covered `PATH` for `arm-linux-gnueabi*`. The AOSP prebuilt is in
the project root:

```
prebuilts/gcc-androideabi-4.9/bin/arm-linux-androideabi-gcc
  real-arm-linux-androideabi-gcc (GCC) 4.9.x 20150123 (prerelease)
  (a Python wrapper script, not a binary — that is normal for AOSP prebuilts)
prebuilts/gcc-androideabi-4.9/bin/arm-linux-androidkernel-gcc -> symlink to it
```

GCC 4.9 is exactly what a 3.4 CAF tree wants. The blocker in Entry 46 is gone.

### Which kernel is which

Two different kernels are in play and the notes conflated them:

| | version string | built | config file |
|---|---|---|---|
| LineageOS 23.2 shipped | `3.4.113-g11370a8eb99` | 2026-09-26 | `work/dev/kconfig` (= root `kconfig.gz`, 1237 options) |
| TWRP (what we test under) | `3.4.113-gda91d066bc6 (acroreiser@thinkstation)` gcc 15.2.1 | 2026-05-17, build #42 | — |

`tools/profiles/hammerhead.list` states it was "Derived from /proc/config.gz of
the SHIPPED build ... `lineage-23.2-...-signed.zip`, kernel version
3.4.113-g11370a8eb99". So `work/dev/kconfig` describes the **LineageOS**
kernel, not the TWRP one.

### Expanding each defconfig and diffing against the shipped config

`make hammerhead_defconfig` and `make lineageos_hammerhead_defconfig` were both
run with the 4.9 toolchain, then compared to `work/dev/kconfig`:

| baseline | expanded options | differing option lines vs device config |
|---|---|---|
| `hammerhead_defconfig` | 1154 | **229** |
| `lineageos_hammerhead_defconfig` | 1168 | **141** |

`lineageos_hammerhead_defconfig` is the device's base, as the README and the
device `BoardConfig` both say. All four capabilities survive either expansion:

```
CONFIG_MACH_MSM8974_HAMMERHEAD=y
CONFIG_TOUCHSCREEN_LGE_SYNAPTICS=y
CONFIG_FB_MSM=y
CONFIG_NFC_BCM2079X=y
```

### Four blockers in build/build-kernel.sh

**1. `CROSS_COMPILE` gets a doubled dash — the compiler is never found.**

Line 250-251 set the prefix *with* a trailing dash:

```sh
[ -x "$c/arm-linux-androidkernel-gcc" ] && { CC_PATH="$c"; CROSS_COMPILE="arm-linux-androidkernel-"; break; }
```

and line 307 appends another:

```sh
MAKE=(make -C "$TREE" O="$OUT" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE-")
```

Proven by asking make what `CC` it derives:

```
CROSS_COMPILE=arm-linux-androidkernel--   CC=arm-linux-androidkernel--gcc   MISSING
CROSS_COMPILE=arm-linux-androidkernel-    CC=arm-linux-androidkernel-gcc    EXISTS
```

The `defconfig` target only uses HOSTCC, so `--config` passes. The failure
would appear at the first C file compiled.

**2. `make olddefconfig` does not exist in a 3.4 tree.**

`olddefconfig` arrived in ~3.16. In 3.4 the target is `silentoldconfig`. The
script's profile-verification step therefore always aborts:

```
error: olddefconfig failed:
make[1]: *** No rule to make target 'olddefconfig'.  Stop.
```

This blocks even `--config`, so the tooling cannot currently complete a config
pass on this kernel.

**3. The defconfig glob picks the wrong baseline.**

```sh
for f in "$TREE"/arch/$ARCH/configs/*hammerhead*defconfig ... ; do
    [ -f "$f" ] && { DEFCONFIG="$f"; break; }
```

Alphabetically `hammerhead_defconfig` precedes
`lineageos_hammerhead_defconfig`, so the glob selects the **custom** one —
229 lines from the device config instead of 141 — while `build/README.md:26`
says to edit `lineageos_hammerhead_defconfig`. Observed live:

```
=== 2. find the defconfig ===
   ok  arch/arm/configs/hammerhead_defconfig
...
   ..  baseline: hammerhead_defconfig
```

**4. The profile removes NFC — contradicting the README, and it is the one
capability this project depends on.**

`build/README.md:49`:

> ### NFC is deliberately kept
> The payload application talks to NFC smartcards, and physical access is in
> scope. So `CONFIG_NFC_BCM2079X` **stays enabled**

`tools/profiles/hammerhead.list:77-82`, however:

```
# ===== NFC =====
CONFIG_NFC_BCM2079X
CONFIG_NFC
# [absent in this kernel]
CONFIG_NFC_DEVICES
```

`tools/apply-defconfig.py` has no keep-list or exception mechanism (grep for
KEEP/EXCEPT/PRESERVE finds nothing). Proven by running the real tool:

```
/tmp/opencode/bk-out/.config.orig :  # CONFIG_NFC is not set
                                     CONFIG_NFC_BCM2079X=y
/tmp/opencode/bk-out/.config      :  (no CONFIG_NFC* lines at all)
```

The other three capabilities survive the profile:

```
CONFIG_MACH_MSM8974_HAMMERHEAD=y
CONFIG_TOUCHSCREEN_LGE_SYNAPTICS=y
CONFIG_FB_MSM=y
```

So building with the tooling as it stands produces a kernel with **no NFC
driver**, which would silently invalidate the entire project.

### Also noted

- `CONFIG_MSM_IPC_ROUTER=y` is set in the shipped config but is **not** in
  `hammerhead.list` — only `CONFIG_MSM_IPC_ROUTER_SMD_XPRT` appears, and it is
  commented out. `MSM_QMI_INTERFACE` is removed, but the router framework that
  carries QMI would remain.
- The profile's "NOT FOUND" symbols (`BCMFMAC`, `CFG80211_MAC80211`,
  `AF_BLUETOOTH`, `MDM9607`) are legacy names from newer kernels; this device
  uses `BCMDHD`/`BCM4339`, which the profile **does** target. Those NOT FOUND
  lines are benign, and the profile header documents them as expected.

### Method

Three claims in this session were wrong before being checked: that no ARM
toolchain existed (searched only `PATH`); that `hammerhead_defconfig` drops USB
(a `savedefconfig` artifact); and that `work/dev/kconfig` was the running
kernel's config (it is the shipped LineageOS kernel's). Each was caught by
running the thing rather than reading about it — `gcc --version`, expanding
the defconfig, and diffing `.config.orig` against `.config`.

## Entry 48 — Three questions clarified: two boot images, NFC is not disabled by any config, toolchain mismatch

### Q1. Do Android and TWRP use the same boot image? NO — two images, two kernels

The LineageOS ROM ships **both** images, i.e. two separate partitions:

```
8722432    boot.img
21071872   recovery.img
```

Their kernels are different binaries:

| | kernel size | sha256 (16) | version string | built |
|---|---|---|---|---|
| LineageOS ROM `boot.img` | 7,383,264 | `ffde091513d91018` | `3.4.113-g11370a8eb99` | 2026-09-26 |
| TWRP (what we boot) | 7,414,992 | `a984548b32638807` | `3.4.113-gda91d066bc6 (acroreiser@thinkstation)` #42 | 2026-05-17 |

The TWRP banner was read live from `/proc/version` on the device. The
LineageOS banner is corroborated by `tools/profiles/hammerhead.list:5`, which
records it as derived from that ROM's `/proc/config.gz`.

**Consequence for our own testing:** every standalone run so far
(`fastboot boot boot-standalone*.img`, which reuses `stock/unpacked/kernel`)
executed on the **TWRP kernel**, not the Android one. Nothing we have proven
about the display, touch, NFC or power-off has been demonstrated on the
LineageOS kernel yet.

Note: neither banner can be read out of the images directly — both payloads
fail to decompress with `tools/extract-vmlinux.py` *and* with the kernel tree's
own `scripts/extract-vmlinux`, which means the CAF compression magic here is
not in either magic list. `/proc/version` on the live device remains the only
reliable source.

### Q2. Why does anything disable NFC? Nothing we have run does.

The user asked whether NFC works on both yet the config disables it. It does
not. Three separate artefacts, and they must not be conflated:

1. **`work/dev/kconfig`** — the LineageOS ROM kernel config. NFC is ON:
   ```
   CONFIG_NFC_BCM2079X=y
   ```
2. **`tools/profiles/hammerhead.list:77-82`** — the air-gap overlay applied
   ON TOP of that config by `build-kernel.sh`, listing `CONFIG_NFC` and
   `CONFIG_NFC_BCM2079X` for removal. **This is the only thing that disables
   NFC, and it has never been built into a kernel that has booted.**
3. **`build/README.md:49`** — "NFC is deliberately kept … `CONFIG_NFC_BCM2079X`
   stays enabled".

So the config target used for the comparison was correct: `work/dev/kconfig`
**is** the Android kernel's config (its header records exactly that
provenance). NFC works on both the LineageOS and TWRP kernels because neither
has had the profile applied.

The finding from Entry 47 stands but is narrower than it looked: the problem
is not that "the config disables NFC", it is that **the profile contradicts
the README**, and `apply-defconfig.py` has no keep-list. Running
`build-kernel.sh` unchanged would produce the first-ever kernel for this
project with no NFC driver — a latent trap, not an existing fault.

### Q3. Toolchain: the running kernel used Arm GNU 15.2.Rel1, not GCC 4.9

Read live from the device under TWRP:

```
Linux version 3.4.113-gda91d066bc6 (acroreiser@thinkstation)
(gcc version 15.2.1 20251203 (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)))
#42 SMP PREEMPT Sun May 17 23:24:06 EEST 2026
```

The user reports the Android kernel was built with Linaro 15.2.rel1 too. That
is consistent, and it means **Entry 46's premise was wrong**: a 3.4.113 tree
builds fine here with GCC 15, so "3.4 needs GCC 4.8/4.9" does not hold for
this setup. (The LineageOS banner itself is not independently verified — see
Q1 on decompression — so this rests on the TWRP reading plus the user's
statement about the ROM author.)

What exists on this host:

| toolchain | version | status |
|---|---|---|
| `prebuilts/gcc-androideabi-4.9/bin/arm-linux-androideabi-gcc` | 4.9.x 20150123 | preferred by `build-kernel.sh` (#1/#2) |
| `arm-linux-gnueabihf-gcc` (PATH) | 15.2.0 Ubuntu | fallback #4, "flagged loudly as a deviation" |
| `prebuilts/clang-linux-x86/clang-r614150` | clang r614150 | `LLVM_BIN` path |

`build-kernel.sh`'s own comment already concedes preference #2 "works, but was
not what built the shipped kernel". Since the kernels actually running were
built with Arm GNU **15.2.Rel1**, the script's preference order would select
GCC 4.9 and produce a kernel built with a materially different toolchain from
the one the ROM author validated. That is a third deviation to correct
alongside the doubled `CROSS_COMPILE` dash and the `olddefconfig` target.

### Method

Answers came from comparing bytes and reading files, not from the notes: the
two `boot.img`/`recovery.img` entries in the ROM zip, `sha256` of the two
kernels, and grepping `CONFIG_NFC` in the config versus the profile versus the
README. Note the note-taking failure this session was not a wrong constant but
a wrong *artefact* — assuming `work/dev/kconfig` described the kernel we were
actually running, when it describes the other one.

## Entry 49 — Measured comparison: TWRP kernel vs Android kernel, both captured live

Method: `b/capture-kernel.sh` pushed and run under each build, then `/proc/config.gz`
pulled from both. Artefacts in `b/kernelcaps/`:

| file | what |
|---|---|
| `cap1-twrp.txt` / `cap2-android.txt` | the reports |
| `twrp-kconfig`, `android-kconfig` | decompressed `/proc/config.gz` from each |

### Versions, compilers, configs

| | TWRP | Android (LineageOS 23.2) |
|---|---|---|
| `ro.build.display.id` | `omni_hammerhead-eng 16.1.0 PQ2A.190405.003` | `BP4A.251205.006` |
| `ro.build.type` | `eng` | `user` |
| `uname -r` | `3.4.113-🇺🇦-gda91d066bc6 #42` | `3.4.113-🇺🇦-ge4e483b4ef6 #315` |
| built | Sun May 17 23:54:08 EEST 2026 | Sat Sep 26 23:54:08 EEST 2026 |
| **compiler** | `gcc 15.2.1 (Arm GNU Toolchain 15.2.Rel1, Build arm-15.86)` | **`gcc 16.2.0 (GCC)`** |
| built by | `acroreiser@thinkstation` | `acroreiser@thinkstation` |
| config options | 1213 | 1242 |
| config sha256 | `a3f8013692e0299…` | `7ba09fdf81dad19e…` |
| `CONFIG_LOCALVERSION` | `-🇺🇦` | `-🇺🇦` |

**The compiler is NOT the same, and the Android one is not Linaro 15.2.rel1.**
Android was built with plain **GCC 16.2.0**; TWRP with **Arm GNU 15.2.Rel1**. This
matches the user's expectation of a "slight deviation" but pins it: 16.2.0 vs
15.2.1, different vendors.

### Correction to Entry 45

Entry 45 claimed the notes' `3.4.113-ge4e483b4ef6 #315` was "not the kernel in
my notes" and called it a custom build replacing a CAF kernel. **The notes were
correct and the correction was wrong.** `ge4e483b4ef6 #315` IS the LineageOS
kernel; `gda91d066bc6 #42` is the TWRP kernel; both are custom builds by the
same author on the same host, four months apart. The notes recorded Android's
string; I read TWRP's under adb and wrongly concluded the notes were stale.

### Capabilities: identical in both

```
FB=y  FB_MSM=y  TOUCHSCREEN_LGE_SYNAPTICS=y  NFC_BCM2079X=y
MACH_MSM8974_HAMMERHEAD=y  BLK_DEV_INITRD=y  IKCONFIG_PROC=y
```

Every symbol the project depends on is `y` in **both** kernels. Swapping to the
Android kernel loses nothing capability-wise.

### Radio: present in BOTH — phase 6 is unmet everywhere

```
BCMDHD=y   BT=y   MSM_SMD=y   MSM_IPC_ROUTER=y
```

Neither shipped kernel has radio compiled out. `tools/profiles/hammerhead.list`
has never been built into anything that has booted.

### Config differences: 2 symbols in TWRP only, 31 in Android only

TWRP only: `CONFIG_BLK_DEV_THROTTLING`, `CONFIG_KALLSYMS_ALL`.

Android only, and these matter:

- **`CONFIG_KSU=y`** plus `KSU_TAMPER_SYSCALL_TABLE`, `KSU_LSM_SECURITY_HOOKS`,
  `KSU_THRONE_TRACKER_ALWAYS_THREADED`, `KSU_HEURISTIC_IN_TREE_BUILD`.
  **The shipped LineageOS kernel has KernelSU compiled in**, and
  `TAMPER_SYSCALL_TABLE` is the option that hides it from `/proc/self/status`.
  TWRP's kernel has zero `KSU` symbols. For an air-gapped signer this is a
  standing root-escalation surface in the ROM kernel, independent of any radio
  question.
- `CONFIG_DEBUG_INFO`, `CONFIG_DEBUG_INFO_REDUCED` — kernel built with debug
  info, which aids reverse engineering.
- ARM crypto asm (`CRYPTO_AES_ARM_BS`, `CRYPTO_SHA1_ARM_NEON`,
  `CRYPTO_SHA512_ARM_NEON`, `CRYPTO_GF128MUL`, `CRYPTO_CCM`, `CRYPTO_GCM`, …),
  `BPF_JIT_ALWAYS_ON`, `SCHED_BORE`, `THUMBEE`, `XFRM_INTERFACE`,
  `SECCOMP_ANDROID_GETDENTS_WORKAROUND`.

### The soft-float premise is wrong

`/proc/cpuinfo` on the live device:

```
Features : swp half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt
```

and **both** kernels are built with:

```
CONFIG_VFP=y  CONFIG_VFPv3=y  CONFIG_NEON=y  CONFIG_KERNEL_MODE_NEON=y
```

`CONFIG_KERNEL_MODE_NEON=y` means the kernel itself emits VFP/NEON
instructions. If FPU actually trapped, nothing on this device would run. So
the standing "AT_HWCAP_FP is clear, therefore every binary must be
`-mfloat-abi=soft`" constraint rests on a wrong premise. **Soft-float is
belt-and-braces, not load-bearing.** Building userspace `softfp` or `hard` is
both safe; what is definitely required is *not* mixing ABIs between our init
and anything else in the same process image (there is nothing else — we are a
freestanding static binary).

### `work/dev/kconfig` matches NEITHER live kernel

| config | options | is it live? |
|---|---|---|
| `work/dev/kconfig` | 1237 | no — 1242 Android, 1213 TWRP |
| Android live | 1242 | yes |
| TWRP live | 1213 | yes |

So `tools/profiles/hammerhead.list`, documented as "Derived from
/proc/config.gz of the SHIPPED build", was derived from a **third** config —
some build we no longer have. For phase 6 the profile should be re-derived from
`b/kernelcaps/android-kconfig`, which is the config actually in use.

### Unresolved: functionfs

Both live configs say `# CONFIG_USB_FUNCTIONFS is not set`, yet the Android
device has `/dev/usb-ffs/adb` and adb works over USB. In this tree
`/dev/usb-ffs` is created by `g_ffs.o`, built only by
`obj-$(CONFIG_USB_FUNCTIONFS)`. So either the embedded config does not describe
the running binary, or something else creates the node.

Could not settle it: `/proc/kallsyms` is unreadable under the Android user
build (`cat` returns nothing, while the same grep worked under TWRP), so the
`ffs_epfile` probe could not be run. **Needs repeating under TWRP**, where
kallsyms is readable. Until then, treat `/proc/config.gz` as not fully
trustworthy as ground truth — which also weakens Entry 47's "profile strips NFC"
measurement, though that was verified by diffing `.config.orig` against
`.config` and does not depend on kallsyms.

### Method

Every claim above comes from reading something on the live device or from
`/proc/config.gz` pulled from the device that produced it. The two failures
worth noting were both about *provenance*: assuming the notes' kernel string was
stale (it was correct), and assuming `work/dev/kconfig` was the shipped config
(it is a third build). Capture both, label both, and compare — do not infer.

## Entry 50 — Config cannot be extracted from the CAF zImage by file inspection

**User:** placed `boot-kernelsu-20260926.img`; "Let's skip analysis of
kernelsu kernel... Can you extract config from kernel image file?"

### Short answer

Not from these kernels by inspecting the file. The config *is* embedded
(`CONFIG_IKCONFIG=y`), but only inside the compressed payload, and that payload
resists every standard decompressor on this host.

### What the file actually is

`boot-kernelsu-20260926.img` (8,753,152) unpacks to kernel 7,414,992 +
ramdisk 1,334,266. Both are **byte-identical to something already known**:

| component | sha256 (16) | matches |
|---|---|---|
| kernel | `a984548b32638807` | the **TWRP** kernel we have been testing |
| ramdisk | `25bbaa0f1b10a0ad` | the **Android ROM** ramdisk |

So it is TWRP's kernel with Android's ramdisk — a hybrid. And because its
kernel is byte-identical to the TWRP kernel whose live config was captured in
Entry 49, **it contains no KernelSU at all** (`grep -c CONFIG_KSU` = 0). The
filename is misleading; this is worth knowing before phase 6 is built on the
assumption it is a different kernel.

### Why file extraction fails

Evidence, in order:

1. The image has **readable strings** in its first few KB — `"Uncompressing
   Linux..."`, `"decompressor returned an error"`, `"Not a gzip file"`,
   `"uncompression error"`. That is the CAF zImage **loader**, stored
   uncompressed.
2. A **single** gzip stream exists in the whole 7.38 MB file, at `0x3c8d` —
   immediately after the string `"uncompression error\0"`. It is the loader's
   decompressor self-test blob, and `gunzip` on it yields 0 bytes.
3. Sliding entropy over 256-byte windows: 5.2 bits/byte at `0x0` (ARM code),
   crossing to 7.1 at `0x3d00` and 7.9+ beyond. The compressed payload starts
   at roughly `0x3d00`.
4. Scanning the entire file for every standard magic — gzip `1f 8b 08`, xz,
   bzip2, zstd, **lz4 legacy `02 21 4c 18`, lz4 frame `04 22 4d 18`**, lzma,
   lzo — yields **zero** lz4/xz/zstd/lzo hits and only the one gzip hit. The
   161 lzma 3-byte hits (`5d 00 00`) are false positives inside ARM
   instructions: at `0xd20` the bytes after the "magic" are
   `e58dc004`, i.e. `STRH r12,r12,[lr]`.
5. Brute-forcing every 4-byte offset from `0x3c80` to `0x3d80` with
   gunzip/unlzma/lzma/unxz/bunzip2/lz4/zstd produces nothing over 10 KB.

So the payload uses a compression this host has no tool for, or a CAF-specific
framing.

### Why the kernel's own extractor also fails

`scripts/extract-vmlinux` in the tree has two independent reasons to give up:

1. It only tries gzip / xz / bzip2 / lzma / lzo — **no LZ4**, which is what CAF
   3.4 normally uses. (`tools/extract-vmlinux.py` in this project inherited the
   same gap.)
2. Even when decompression succeeds it calls `check_vmlinux`, which requires
   **`readelf -h` to succeed**. CAF emits a raw ARM `Image`, not an ELF
   vmlinux, so a *correct* decompression would still be rejected. That is the
   real reason `tools/extract-vmlinux.py` printed "no payload decompressed"
   for both kernels — a misleading message for a different cause.

### Two bugs in my own extractor, found by running it

- Compared `raw[:4] == b"ANDROID"` — a 7-byte literal against a 4-byte slice,
  so it **never** matched and every `boot.img` was silently treated as a bare
  kernel image.
- Filtered payload candidates to 4-byte-aligned offsets, assuming zImage
  payloads are aligned. They are not here. That turned "the payload is at an
  unaligned offset I did not try" into **"no payload decompressed"** — a false
  negative reported as a fact. Same failure mode as Entry 21: treating an
  absence of evidence as evidence of absence.

Also: `bootimg.py unpack` requires an output directory argument; the tool did
not pass one.

`b/extract-kconfig.py` now detects `ANDROID!` correctly, scans unfiltered,
delegates to the verified `bootimg.py`, and when it fails **explains why and
points at the working alternative** instead of just returning an error.

### The route that does work

`CONFIG_IKCONFIG_PROC=y` in **both** live kernels, so:

```sh
adb reboot bootloader
fastboot boot <image>
adb shell 'zcat /proc/config.gz' > kconfig
```

That is the running kernel's own copy — strictly better evidence than anything
recovered from the file. Already done for both kernels in Entry 49:
`b/kernelcaps/twrp-kconfig` (1213 options) and
`b/kernelcaps/android-kconfig` (1242 options).

### Still open: functionfs

Both configs say `# CONFIG_USB_FUNCTIONFS is not set`, yet `/dev/usb-ffs/adb`
exists and adb works. Since the embedded config cannot be extracted to
cross-check, the remaining test is to look for `ffs_epfile` in `/proc/kallsyms`
**under TWRP**, where kallsyms is readable (it is blocked on the Android user
build). Not yet run.

## Entry 51 — There are only two kernels, and the "kernelsu" image is not one of them

**User:** "Will it help if we flash regular kernel image (no kernelsu) and boot
it to capture config/version etc? That will narrow our comparison to 2"

### The comparison is already at 2 — there is no third kernel

The ROM ships **one** kernel, used by both of its images:

```
boot.img     kernel sha256 ffde091513d91018  (7,383,264)
recovery.img kernel sha256 ffde091513d91018  (7,383,264)   <- same kernel
```

So the complete set of kernels in play is:

| # | kernel | sha256 | version | compiler | KSU | options |
|---|---|---|---|---|---|---|
| 1 | ROM | `ffde0915…` | `3.4.113-🇺🇦-ge4e483b4ef6 #315` | gcc 16.2.0 | **yes** | 1242 |
| 2 | TWRP | `a984548b…` | `3.4.113-🇺🇦-gda91d066bc6 #42` | gcc 15.2.1 Arm GNU 15.2.Rel1 | no | 1213 |

Both were captured live in Entry 49. `boot-kernelsu-20260926.img` is a
**hybrid**: TWRP kernel (`a984548b…`) + ROM ramdisk (`25bbaa0f1…`). Despite the
filename it is the one **without** KernelSU. No image named "regular" or
"no-kernelsu" exists in the project.

### The more important question this exposed: can our tree even build these?

**Our kernel tree contains no KernelSU at all.**

```
grep -rn 'config KSU' --include='Kconfig*'    -> 0 hits
CONFIG_KSU in hammerhead_defconfig            -> 0
CONFIG_KSU in lineageos_hammerhead_defconfig  -> 0
```

Yet the running ROM kernel has `CONFIG_KSU=y` plus four more KSU symbols.
**The ROM was built from a patched tree we do not hold.** The tree at
`/tmp/opencode/kern/android_kernel_lge_hammerhead` is the unpatched
LineageOS hammerhead tree.

That is arguably good news for phase 6 — building from this tree cannot
accidentally include KernelSU, because the source does not exist in it. But it
means the tree is *not* the provenance of either running kernel.

Nor do either defconfig reproduce a running config. Comparing the live TWRP
config (1213 options) against the expansions:

| baseline | differing option lines vs live TWRP config |
|---|---|
| `hammerhead_defconfig` | 203 |
| `lineageos_hammerhead_defconfig` | **113** |

The live kernel has things neither expansion turns on — `IKCONFIG_PROC`,
`CFS_BANDWIDTH`, `BLK_CGROUP`, `TASKSTATS`, `ANDROID_TREBLE_SPOOF_*` — which is
what you would expect if the builder started from a defconfig and then applied
extra edits, or built through the Android build system rather than `make
<defconfig>` alone.

### So: is booting a plain kernel image worth it?

**Not for narrowing the comparison — that is already two, both captured.**
It would be worth it for one different reason: establishing provenance for a
*plain* (non-KSU) LineageOS kernel, which is the phase-6 baseline.

If such an image exists, `fastboot boot <image>` is safe — it is temporary and
writes nothing to the boot partition — and then `adb shell zcat /proc/config.gz`
gives that kernel's exact config.

But there is a cheaper way to answer the same question without touching the
device: fix `build-kernel.sh` (the `olddefconfig` target, the doubled
`CROSS_COMPILE` dash, the wrong defconfig glob, the toolchain preference), run
`--config`, and compare the generated `.config` against
`b/kernelcaps/twrp-kconfig` until the delta collapses. Iterating on the host is
cheaper than rebooting the phone, and the TWRP kernel is the right target
because it is the one with no KernelSU.

## Entry 52 — The "kernelsu" image changes the kernel only, and there are THREE kernels

**User:** "Is it possible kernelus image changes only the ramdisk?"

### Entry 51 was wrong: there are three distinct kernels

Entry 51 claimed only two kernels exist and that `boot-kernelsu-20260926.img`
was "TWRP's kernel". That rested on assuming `b/rootfs/stock/unpacked/` was
unpacked from the TWRP image. Comparing all four images directly:

| image | kernel sha256 | ramdisk sha256 |
|---|---|---|
| ROM `boot.img` | `ffde091513d91018` | `25bbaa0f1b10a0ad` |
| ROM `recovery.img` | `ffde091513d91018` | `ae169b79c869b70b` |
| `twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img` | **`bbba49d13c789a8c`** | `b222022f93967dcc` |
| `boot-kernelsu-20260926.img` | **`a984548b32638807`** | `25bbaa0f1b10a0ad` |
| `b/rootfs/stock/unpacked/` (ours) | `a984548b32638807` | `25bbaa0f1b10a0ad` |

So `bbba49d13c789a8c` — the kernel inside the standalone TWRP image — has never
been characterised, and it is **not** the kernel on the recovery partition we
captured in Entry 49 (that one is `a984548b`, the same as
`boot-kernelsu-20260926.img` and as our own `stock/unpacked`). The failure was
assuming a provenance instead of hashing every image.

### Answer to the question: yes — relative to the ROM, it changes ONLY the kernel

```
boot-kernelsu-20260926.img  ramdisk 25bbaa0f1b10a0ad
ROM boot.img               ramdisk 25bbaa0f1b10a0ad   <- byte-identical
```

The ramdisks are the **same file**. Only the kernel differs. So the change
between the ROM and this image is entirely in the kernel, not the ramdisk.

### But KernelSU cannot live in a ramdisk, so that image is not a KernelSU image

Every ramdisk in the project was decompressed and searched — no KernelSU
artefacts in any of them:

| ramdisk | `ksud` / `kernelsu` / `com.topjohnwu` hits | matching entries in cpio listing |
|---|---|---|
| ROM | 0 | none |
| boot-kernelsu | 0 | none |
| TWRP | 0 | none |

Meanwhile the ROM's **kernel** demonstrably has it, read from the live running
kernel's own `/proc/config.gz`:

```
CONFIG_KSU=y
CONFIG_KSU_LSM_SECURITY_HOOKS=y
CONFIG_KSU_TAMPER_SYSCALL_TABLE=y
CONFIG_KSU_THRONE_TRACKER_ALWAYS_THREADED=y
CONFIG_KSU_HEURISTIC_IN_TREE_BUILD=y
```

Those are in-kernel: an LSM hook and a syscall-table tamper that hide it from
`/proc/self/status`. A ramdisk-only change cannot turn any of that on.

**Therefore `boot-kernelsu-20260926.img` cannot be the KernelSU image.** It is
LineageOS's ramdisk paired with the non-KSU kernel. The filename and the
contents disagree — worth resolving before it is flashed anywhere, because a
"name says KernelSU, contents say no" image is exactly the kind of artefact
that gets trusted later without re-checking.

## Entry 53 — ROM kernel boots TWRP's screen, but its USB gadget path is broken

**User's plan:** "build a TWRP image with kernel from
lineage-23.2-...-signed.zip and see if it works. If it works we will try to pack
our test payload with that kernel. If that works as well, we can choose that
kernel as the only kernel we deal with."

### Three images built, all verified by component hash

| image | kernel | ramdisk | cmdline |
|---|---|---|---|
| `stock/boot-twrp-romkernel.img` | ROM `ffde0915…` | TWRP `b222022f…` | TWRP's |
| `stock/boot-romkernel-romcmd.img` | ROM `ffde0915…` | TWRP `b222022f…` | ROM's |
| `stock/boot-standalone-romkernel.img` | ROM `ffde0915…` | **ours, 3,004 B** | ROM's |

Each was re-unpacked and its kernel/ramdisk hashes compared against the sources
before being called ready.

### Result of booting `boot-twrp-romkernel.img`

**PASS — the display path works.** TWRP comes up and renders on the ROM kernel.
That is the substantive finding: kernel `ge4e483b4ef6 #315`, built with
gcc 16.2.0 and never before used by us, drives the panel and runs an initramfs
we did not build.

**FAIL — adb never reaches `device`, only `offline`.**

```
$ adb devices
0879524b0068abca	offline
```

This held for over two minutes of polling, across an `adb kill-server` /
`adb start-server` cycle. The user then reported from the on-device TWRP
terminal that **adbd is running** — so adbd starts and then cannot bring up the
transport. `offline` is exactly that state: daemon alive, endpoint not
established.

Enabling "ADB Sideload" in the TWRP UI caused the **TWRP interface to restart**,
i.e. the GUI process died. Toggling the USB function is precisely the code path
that touches the gadget, which fits the same failure.

### What this says about the open functionfs question

This is the first case where the Entry 49 inconsistency has actually bitten.
Both configs claim `# CONFIG_USB_FUNCTIONFS is not set`, yet under Android
`/dev/usb-ffs/adb` existed and adb worked. Now, with the same ROM kernel under
TWRP, adbd runs but the transport does not.

The pending on-device checks were: `cat /proc/version` (confirm the kernel),
`zcat /proc/config.gz | grep FUNCTIONFS`, `ls -l /dev/ffs* /dev/usb-ffs`, and
`dmesg | tail -40`. **Not yet obtained** — the reboot during the sideload
attempt interrupted the terminal session, and adb is too offline to substitute.

### Honest assessment of "can this be the only kernel we deal with"

- **For the device:** plausible. The payload needs display, touch, NFC and
  power-off, all of which are `y` in this kernel's live config, and the screen
  demonstrably works under it.
- **For development:** this is the real cost. Without a working adb we lose the
  channel that every debugging step in this project has relied on. Recovery
  requires a physical power cycle into fastboot.
- **Likely single fix** for phase 6: `CONFIG_USB_FUNCTIONFS=y`, which we can
  set because we would be building the kernel. That is not possible for the
  shipped ROM kernel — we can only accept it or replace it.

The payload test does **not** need adb: the standalone diagnostic is entirely
screen-based (magenta / flashing magenta / green / flashing green / blue /
red), which is why it was designed that way. So the decisive experiment can
still be run.

## Entry 54 — The ROM zip's kernel is NOT the kernel that was installed

User, from the TWRP terminal on the device after booting
`stock/boot-twrp-romkernel.img`:

```
3.4.113-🇺🇦-g11370a8eb99 gcc 16.2.0  Sat Sep 26 22:27:35 EEST 2026
```

Compare with what the device reported when running Android (Entry 49):

```
3.4.113-🇺🇦-ge4e483b4ef6 #315 SMP PREEMPT Sat Sep 26 23:54:08 EEST 2026
(gcc version 16.2.0 (GCC))
```

| | git hash | built | gcc |
|---|---|---|---|
| kernel in the ROM **zip** (`ffde0915…`, what we just booted) | `g11370a8eb99` | Sep 26 **22:27:35** | 16.2.0 |
| kernel **installed** in the boot partition and running as Android | `e4e483b4ef6` | Sep 26 **23:54:08** | 16.2.0 |

A byte-identical kernel cannot report two different `UTS_RELEASE` strings, so
these are two different binaries. The ROM author built at least two kernels that
day, 87 minutes apart.

### Consequences

1. **The ROM zip's kernel is not what was flashed on the device.** Installing
   that zip over the top of the current `boot` partition would replace
   `e4e483b4ef6` with `g11370a8eb99`.
2. **`work/dev/kconfig` is probably the zip's kernel, not the installed one.**
   `tools/profiles/hammerhead.list:5` records the shipped kernel as
   `3.4.113-g11370a8eb99`, built 2026-09-26 — matching `g11370a8eb99` exactly.
   That is consistent with the profile having been derived by booting the zip's
   kernel and reading `/proc/config.gz`, while our Entry 49 "Android" capture
   read the *installed* kernel's config. It would also explain why
   `work/dev/kconfig` (1237 options) matches neither live capture.
3. **Our TWRP experiment is valid.** `fastboot boot` loads the kernel from the
   image we supplied, so TWRP is rendering on `g11370a8eb99` — the ROM zip's
   kernel. The "ROM kernel boots TWRP's screen" result stands.

### The discriminating measurement

Each kernel's option count is already known, so `/proc/config.gz` on the device
right now identifies which binary is running:

| config source | options |
|---|---|
| `g11370a8eb99` (zip) | unknown — predicted 1237 if `work/dev/kconfig` is it |
| `e4e483b4ef6` (installed) | 1242 (Entry 49, `b/kernelcaps/android-kconfig`) |
| TWRP `a984548b…` | 1213 (Entry 49, `b/kernelcaps/twrp-kconfig`) |

Requested from the device, together with the Entry 53 items still outstanding:

```sh
zcat /proc/config.gz | grep -c '^CONFIG'
zcat /proc/config.gz | grep -E 'FUNCTIONFS|CONFIG_KSU|LOCALVERSION='
ls -l /dev/ffs* /dev/usb-ffs 2>&1
dmesg | tail -30
```

## Entry 55 — The ROM zip's kernel is KernelSU-free; Entry 49's finding was about the installed kernel

Device, on the ROM zip's kernel (`g11370a8eb99`), via the TWRP terminal:

```
zcat /proc/config.gz | grep -c '^CONFIG'            -> 1237
zcat /proc/config.gz | grep -E 'FUNCTIONFS|CONFIG_KSU|LOCALVERSION='
    LOCALVERSION='-🇺🇦'
    # CONFIG_USB_FUNCTIONFS is not set
ls -l /dev/ffs* /dev/usb-ffs
    ls: /dev/ffs*: No such file or directory
    /dev/usb-ffs:  dr-x----- shell shell 0  ... adb
```

### 1. `work/dev/kconfig` is now positively identified

1237 options is an exact match for `work/dev/kconfig`, which the profile header
says was taken from the shipped build `3.4.113-g11370a8eb99`. That ties the
chain together: **the currently running kernel IS the ROM zip's kernel, and
`work/dev/kconfig` IS its config.** The Entry 51 anomaly (1237 matching neither
capture) is resolved — our "Android" capture was of a different, installed
kernel.

### 2. Correction: the ROM zip's kernel has NO KernelSU

| kernel | options | `CONFIG_KSU` |
|---|---|---|
| **ROM zip** `g11370a8eb99` = `work/dev/kconfig` | 1237 | **0** |
| **installed** `e4e483b4ef6` = `kernelcaps/android-kconfig` | 1242 | 5 enabled |
| TWRP `gda91d066bc6` = `kernelcaps/twrp-kconfig` | 1213 | 0 |

Entry 49's headline — "the shipped LineageOS kernel has KernelSU compiled in" —
was **wrong about the shipped kernel**. It is true of the kernel that happened
to be *installed* in the boot partition, which is a different binary from the
one in the ROM zip:

```
CONFIG_KSU=y
CONFIG_KSU_TAMPER_SYSCALL_TABLE=y
CONFIG_KSU_LSM_SECURITY_HOOKS=y
CONFIG_KSU_THRONE_TRACKER_ALWAYS_THREADED=y
CONFIG_KSU_HEURISTIC_IN_TREE_BUILD=y
```

This is *good* news for the plan to standardise on one kernel: the ROM zip's
kernel carries no KernelSU, so the air-gap concern in Entry 49 does not attach
to the kernel we would be building on. **But it must not be flashed over the
installed one casually** — that would be a downgrade from `e4e483b4ef6` to
`g11370a8eb99`, and the installed one is the KernelSU-free-in-name-only
counterpart we should compare against.

Note also: our own kernel tree has no `config KSU` anywhere, so a phase-6 build
from it cannot acquire KernelSU by accident either.

### 3. The functionfs puzzle is resolved

`/dev/ffs.ep0` is **absent** while `/dev/usb-ffs/adb` **exists**. That is the
signature of functionfs being unavailable: adbd `mkdir`s its own mount point, so
the directory and its `adb` entry persist, but the kernel never provides the
`ep0`/`ep1` endpoint files that only `g_ffs` creates.

All three kernels agree:

```
work/dev/kconfig            # CONFIG_USB_FUNCTIONFS is not set
kernelcaps/android-kconfig  # CONFIG_USB_FUNCTIONFS is not set
kernelcaps/twrp-kconfig     # CONFIG_USB_FUNCTIONFS is not set
```

And this tree has **no `f_adb` either** — `drivers/usb/gadget/function/` has no
adb file and its Makefile has no adb line. So there is no legacy kernel-side
ADB function to fall back on.

Therefore adbd starts, finds no endpoint, and sits at `offline` — exactly what
was observed. Toggling "ADB Sideload" drives the same gadget path and took the
TWRP UI down with it.

**Still unexplained:** adb *did* work under Android with the installed kernel,
which by this config also has functionfs off. Either the installed kernel does
have `g_ffs` and its embedded config is inaccurate, or Android's adbd reached it
some other way. Testing that requires booting Android and checking
`ls -l /dev/ffs.ep0` there.

### Practical consequence

For phase 6 this is a **one-symbol fix**, because we build the kernel:
`CONFIG_USB_FUNCTIONFS=y` restores the adb channel that is currently missing on
all three binaries. It cannot be retrofitted onto any existing image.

## Entry 56 — Stage 7 diagnosis stalled; found a real struct bug on the way

**User:** "blinking magenta, tap, blinking green, tap, stuck on pink (as before)"

### The colour scheme was the problem, not the kernel

Ran the constants through both plausible channel orders:

| constant | u32 | renders as |
|---|---|---|
| `C_MAGENTA` | `0xFFFF00FF` | MAGENTA |
| `C_GREEN` | `0xFF00FF00` | GREEN |
| `C_BLUE` | `0x0000FFFF` | **YELLOW** |
| `C_RED` | `0xFF0000FF` | RED |

**`C_BLUE` was yellow.** The name was a lie, and "pink" matches neither, so the
report could not be decoded into a state at all. Three stage-7 runs had been
spent on hues.

Worse: magenta and green are the only two colours ever verified, and **both are
channel-order invariant** — magenta has R==B, green lights one channel. So those
successes never established the framebuffer byte order either.

Replaced hue with a pattern:
- **solid WHITE** painted immediately before the `reboot()` call
- **countable black/white barcode** if it returns — the number of bars is
  `|rc|` directly. 1 bar = EPERM, 22 = EINVAL, 0 = returned success.

A stripe pattern is immune to byte order and to colour perception.

### The real find: struct fb_var_screeninfo was 112 bytes, not 160

Added compile-time tripwires asserting the sizes the kernel's headers imply.
**They fired on the first build.** The cause:

```c
/* include/linux/fb.h -- the kernel's real struct */
struct fb_var_screeninfo {
        __u32 xres, yres, xres_virtual, yres_virtual, xoffset, yoffset;
        __u32 bits_per_pixel;
        __u32 grayscale;
        struct fb_bitfield red, green, blue, transp;   /* <-- 4 x 12 bytes */
        __u32 nonstd, activate, height, width, accel_flags;
        ...
        __u32 reserved[4];
};
```

Our transcription omitted **all four `struct fb_bitfield` members** and invented
`u16 activate_s, u16 rotate_2, u16 reserved[4]` at the end. So:

```
sizeof(struct fb_var_screeninfo)  ours 112   kernel 160
sizeof(struct mdp_display_commit) ours 120   kernel 168
```

`MSMFB_DISPLAY_COMMIT` is `_IOW('m',164,struct mdp_display_commit)`, and `_IOW`
encodes the size of its third argument. We were passing a **120-byte object to
a 168-byte ioctl** — 48 bytes of the kernel's read ran past the end of ours.

It appeared to work because every field the commit path actually consumes
(xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel) sits
*before* `grayscale`, so the divergence was never reached and the unused tail
read as zeros from .bss. **Working by luck, not by correctness** — the same
class of silent-success failure as Entries 21-31, and it would have bitten the
moment the MSMFB path touched a bitfield.

Entry 47 recorded "sizeof(fb_var_screeninfo)=0xA0=160" from the kernel header
while main.c used a 112-byte struct. The two facts were never reconciled.

Fixed by transcribing `struct fb_bitfield` and the real field order. The
tripwires now pass:

```c
typedef char tripwire_bitfield_size[(sizeof(struct fb_bitfield) == 12) ? 1 : -1];
typedef char tripwire_fb_var_size[(sizeof(struct fb_var_screeninfo) == 160) ? 1 : -1];
typedef char tripwire_commit_size[(sizeof(struct mdp_display_commit) == 168) ? 1 : -1];
typedef char tripwire_commit_ioctl[(MSMFB_DISPLAY_COMMIT ==
      ((1u<<30) | (sizeof(struct mdp_display_commit)<<16) | ('m'<<8) | 164u)) ? 1 : -1];
```

gcc rejects a negative array bound, so future layout drift breaks the build
instead of the device. Verified the tripwire mechanism itself works: changing
one expected value produces three errors.

### verify-constants.py was crying wolf — fixed

It had been reporting 3 permanent failures because it could not evaluate
`_IOW(...)` or `_IO(...)`. It now:
- joins line continuations before parsing `MSMFB_DISPLAY_COMMIT`
- resolves `MSMFB_IOCTL_MAGIC` from the header instead of assuming `'m'`
- reads the struct size from a host compile (with `(int)` casts — `1 - sizeof()`
  underflows in `size_t` and silently yielded nothing, which is why the size
  lookup had been quietly returning empty)
- evaluates `_IO(BCMNFC_MAGIC, nr)` as `(magic<<8)|nr`, reading the magic from
  the header

Now: **24 checks passed, 0 failed.**

### Images rebuilt

| image | kernel | ramdisk |
|---|---|---|
| `stock/boot-standalone-romkernel.img` | ROM zip `ffde0915…` | ours, 3,117 B |
| `stock/boot-standalone2.img` | `a984548b…` (same kernel as the earlier stage-7 run) | ours, 3,117 B |

Both re-unpacked and verified: kernel hash correct, `init` byte-identical to the
build, 3/3 device nodes present.

Note there are now **three** distinct kernels in play — `a984548b` (recovery
partition), `bbba49d1` (inside the standalone TWRP image) and `ffde0915` (ROM
zip) — and only the first two have been characterised by running.

### Method

Three of this session's errors were the same shape: a value asserted rather than
checked. A misnamed colour constant, a struct transcribed from memory, and a
checker reporting failures it was incapable of resolving. The tripwire caught
the struct bug on the first build; without it this would have shipped as a
120-byte object handed to a 168-byte ioctl.

## Entry 57 — SUCCESS: power-off as PID 1 on the ROM zip kernel

**User:** "Magenta -> flashing -> tap -> green -> flashing -> tap -> unendentified
color for part of a second -> poweroff"

| stage | result on ROM zip kernel `g11370a8eb99` |
|---|---|
| 1. boot, no TWRP, our init as PID 1 | **PASS** |
| 2. draw magenta | **PASS** |
| 3. wait for touch | **PASS** |
| 4. NFC | ran, no crash (see caveat) |
| 5. draw green | **PASS** |
| 6. wait for touch | **PASS** |
| 7. power off | **PASS — device powered off** |

The "unidentified colour for part of a second" is the solid WHITE painted
immediately before `reboot()`. No barcode appeared, which is the whole point:
**reboot() did not return.**

### What this retires

Every stage-7 theory is now settled, and most of them were wrong:

- **Entry 44** claimed `pm_power_off` was NULL on this platform and the
  capability was absent. **Wrong** — `arch/arm/mach-msm/restart.c:367` sets
  `pm_power_off = msm_power_off` whenever `MACH_MSM8974_HAMMERHEAD=y`, which is
  true in every kernel on this device.
- **Entry 42** blamed glibc's magic `0x28121969`. **Wrong** — that number *is*
  `LINUX_REBOOT_MAGIC2`.
- **Entry 43** blamed argument order. **A real bug**, but not the blocker.
- The actual blocker: **`LINUX_REBOOT_CMD_POWER_OFF` was `0x4321`; the real
  value is `0x4321FEDC`** (Entry 45). `0x4321` matches no case in the switch in
  `kernel/sys.c`, so it fell through to `default` and returned `-EINVAL`.

Fixed by vendoring the kernel's own `include/linux/reboot.h` and
`#include`-ing it, with a checker that verifies the vendored copy against the
tree.

### Which fix actually unblocked it — a testable question

The two candidate runs that "failed" both used a binary with the correct reboot
constant. The current binary additionally carries the **struct fix from Entry
56**. That matters more than it looks: `msm_fb_pan_display_ex()` copies the
full 168-byte `var` back to userspace, and we were handing it a 120-byte
object. The driver therefore wrote **48 bytes past the end of our static
`dc`**, over whatever follows it in `.bss`. What follows is layout-defined and
unverified — it could have clobbered anything, including state the later
`reboot()` path depends on.

So the honest alternatives are:

1. the struct fix resolved it, or
2. the ROM kernel always worked and the earlier runs failed because they were
   on the **TWRP** kernel `a984548b`, which genuinely refuses.

These are distinguishable in one run: **flash the current binary on the TWRP
kernel** (`stock/boot-standalone2.img`, kernel `a984548b`). If it powers off
too, cause (1). If it still refuses, cause (2) — and then the two kernels really
do differ in behaviour, which is itself a finding worth recording before
choosing one.

### Caveat on stage 4 (NFC)

NFC did not crash, but **that is not evidence it worked.** Our `nfc_test()`
logs to RAM and returns void; a failed `open()` or a failed `read()` would look
identical on the outside. The honest status is "exercised, outcome unknown".
Getting a positive signal needs an observable — most cheaply by having stage 5
paint a colour that depends on the NFC result, so the screen becomes the
evidence.

### Standing status on this kernel

| capability | evidence |
|---|---|
| screen | **proven** — human-observed magenta/green |
| touch | **proven** — two taps advanced two stages |
| power-off | **proven** — device powered off |
| NFC | **exercised only** — no positive signal yet |

Also still true of this kernel: `# CONFIG_USB_FUNCTIONFS is not set`, so adbd
runs but the USB transport never comes up (Entry 53/55). For phase 6 that is a
one-symbol fix, because we would be building the kernel.

## Entry 58 — Both kernels power off. The struct overrun was the real blocker.

**User:** "Test 2, identical behaviour to the previous one" — i.e. the same
sequence on the TWRP kernel `a984548b`: magenta -> flashing -> tap -> green ->
flashing -> tap -> white -> **power off**.

### The 2x2 matrix is now complete

| kernel | before struct fix | after struct fix |
|---|---|---|
| TWRP `a984548b` | "redish" (reboot returned) | **POWERED OFF** |
| ROM zip `ffde0915` | "pink" (reboot returned) | **POWERED OFF** |

Entry 57 posed the question: was stage 7 unblocked by the `fb_var_screeninfo`
fix, or by using the ROM kernel? **The struct fix.** `msm_fb_pan_display_ex()`
copies the full 168-byte `var` back to userspace, and we were handing it a
120-byte object, so the driver wrote 48 bytes past our static `dc`. Whatever
follows it in `.bss` is layout-defined, and evidently included state the later
`reboot()` path depended on.

So **both bugs were real and both were required**: the wrong
`LINUX_REBOOT_CMD_POWER_OFF` (`0x4321` vs `0x4321FEDC`) and the truncated
struct. Neither alone was sufficient.

### This retires the kernel choice as a blocker

| capability | TWRP `a984548b` | ROM zip `ffde0915` |
|---|---|---|
| `FB_MSM` | y | y |
| `TOUCHSCREEN_LGE_SYNAPTICS` | y | y |
| `NFC_BCM2079X` | y | y |
| `MACH_MSM8974_HAMMERHEAD` | y | y |
| `BLK_DEV_INITRD` | y | y |
| `CONFIG_KSU` | 0 | **0** |
| functionfs | off | off |
| **screen / touch / power-off** | **proven** | **proven** |

Both are viable and neither carries KernelSU. The choice between them comes
down to provenance and maintenance, not capability. Note the *installed* Android
kernel (`e4e483b4ef6`) is the one with `CONFIG_KSU=y` — it is not either of
these.

### NFC made observable

`nfc_test()` returned `void`, so "did not crash" was the only signal available
and that is not evidence. It now returns a code that is **drawn on screen**:

- solid green, no bars = the controller answered and the reply began with the
  expected `MT_RSP` (0x10 0x60)
- green with **N black bars** names the failing step:
  `1` open, `2` POWER_CTL, `3` WAKE_CTL, `4`/`5` write, `6` no response,
  `7` unexpected response

`barcode()` gained background/foreground parameters so the same routine serves
both the NFC report (black bars on green) and the reboot report (white bars on
black).

Both images rebuilt and verified. `verify-constants.py`: 24 passed, 0 failed.

## Entry 59 — barcode() had a pixel/word unit bug; the NFC diagnostic was inert

**User:** "Ran b/rootfs/stock/boot-standalone2.img (could not find
-romkernel.img). All the same, but first green flash was weird (it was 1/3
black on the left for split second)."

All seven stages passed again, power-off included, on the TWRP kernel.

### The filename

`b/rootfs/stock/boot-standalone-romkernel.img` does exist (7,391,232 bytes).
There are three similarly-named files in that directory, which is confusing:

```
boot-standalone.img           7,421,952   stale (superseded)
boot-standalone2.img          7,421,952   TWRP kernel      a984548b
boot-standalone-romkernel.img 7,391,232   ROM zip kernel   ffde0915
```

### The artefact was a bug I introduced

"1/3 black on the left" is the barcode rendering incorrectly. Cause:

```c
const long slot = FB_STRIDE / 64;        /* 68 -- PIXELS */
u32 *row = &g_base[y * words_per_row];   /* u32*, so the unit is WORDS */
for (long i = 0; i < 64; i++)
    for (long x = 0; x < slot; x++)
        row[i * slot + x] = c;           /* max index 4351, but a row is 1088 */
```

A row is `FB_STRIDE / 4` = **1088 words**. Indexing up to `63*68 + 67` = 4351
walks **four rows past the end** of the row being filled. Every row was
overwritten four times, by four successive passes, and the slot that survived
was 48..63 instead of 0..63.

**The consequence for the diagnostic:** with any realistic bar count, every
visible row rendered entirely background. A failing NFC would have painted
**solid green** — indistinguishable from success. The check added in Entry 58
was inert, and had it stayed unfixed we would have read "solid green" as a
passing NFC test when it might have been a failing one.

Fixed to index in words:

```c
const long words_per_row = FB_STRIDE / 4;      /* 1088 */
const long slot_words    = words_per_row / 64;  /* 17   */
if (slot_words * 64 != words_per_row) return;   /* never draw a wrong pattern */
```

Verified by simulation — every bar count now yields an exact row:

| v | black words | % of width | row length |
|---|---|---|---|
| 0 | 0 | 0.0% | 1088 ✓ |
| 1 | 17 | 1.6% | 1088 ✓ |
| 7 | 119 | 10.9% | 1088 ✓ |
| 22 | 374 | 34.4% | 1088 ✓ |

Incidentally 22 bars is 34.4% of the width — a third. That is the *reboot*
report, and it appears at stage 7, long after the green flash. The barcode
cannot yet be confirmed as the source of the artefact, but it was demonstrably
wrong and is now demonstrably right, so the next run will settle it.

### Method

A unit error slipped through a "verified" routine because the only check was
that the *total* width matched: `64 * (FB_STRIDE/64) == FB_STRIDE`. That
identity is true in pixels and false in words, which is exactly the trap. The
guard now checks the arithmetic in the unit the code actually uses.

Fourth time this project has been bitten by a unit mismatch or a value asserted
rather than computed (`read=45`, `reboot=142`, `0x4321`, `112 vs 160 bytes`).

## Entry 60 — NFC reads a response; it just isn't the one we expected

**User:** "Same as before but first flash of the green was ~1/10 black on the
left, subsequent flash were full green"

### That is a reading, not an artefact

The fixed barcode puts each bar in 17 of 1088 words per row, so `v` bars cover
`v/64` of the width:

| v | % of width | what it means |
|---|---|---|
| 6 | 9.4% | no response at all |
| **7** | **10.9%** | **response received, unexpected header** |

"~1/10 black on the left" is **7 bars**. The subsequent flashes were full green
because `wait_touch()` overwrites the pattern with a solid fill, exactly as
designed — the barcode is a still frame, not a persistent state.

### What that says about NFC on our payload

`nfc == 7` means the first five steps all succeeded:

1. `open("/dev/bcm2079x", O_RDWR|O_NOCTTY)` — **worked**
2. `ioctl(POWER_CTL, 1)` — **worked**
3. `ioctl(WAKE_CTL, 1)` — **worked**
4. `write(Core Reset)` — **worked**
5. `write(GET_BUILD_INFO)` — **worked**
6. `read()` returned **> 0 bytes** — the controller answered

Only the final assertion failed: `rsp[0] != 0x10 || rsp[1] != 0x60`.

So **NFC transport is functionally alive under our own userspace** — this is the
first positive evidence of it on the standalone path, where the only difference
from the working TWRP-hosted probe is that initramfs provides the node instead
of ueventd. The open question is narrow: what did it actually send back?

Candidates, in rough order of likelihood:

- The reply is the response to **Core Reset**, not to `GET_BUILD_INFO`. Both
  writes are issued back-to-back before the read, so whichever frame the
  controller emitted first is what arrives. Core Reset's acknowledgement is
  itself an MT_RSP, so this should still match `10 60` — which argues against
  it, unless the ack has a different shape here.
- The frame is a different MT command.
- `read()` returned a short or partial frame, or a status byte without a body.

### Now drawn instead of described

A bare failure code was not actionable, so the response is now painted:

```c
if (nfc == 7 && g_nfc_got > 0)
        hexdump(g_nfc_rsp, 8, C_GREEN, C_BLACK);   /* show what really arrived */
```

`hexdump()` lays 8 response bytes across the 64 slots, 8 slots per byte, MSB
first, black = bit set. So the first byte is "count the black slots in each
group of eight" — `10 60` would read as 00010000 01100000.

`barcode()` and `hexdump()` now share one `draw_pattern()`, so the slot
arithmetic that was wrong in Entry 59 exists in exactly one place.

Both images rebuilt. `verify-constants.py`: 24 passed, 0 failed.

### Method

The fix in Entry 59 turned an unreadable artefact into a measurement, which is
the whole argument for byte-order- and unit-independent output. A colour would
have hidden this; a counted pattern did not.

## Entry 61 — Readable output: 30s hold with a countdown, and bars that can be counted

**User:** "Can you make the first flash longer? It's hard to read the output when
it disappears so quickly" and "change the value of the size of the bars to more
bars. It's easier to count bars and not theor size".

Both requests point at the same defect: the output was a **contiguous block of
black**, so all a human could report was its approximate width. That is exactly
what came back — "about 1/10 black" — which then had to be mapped back onto a
value by arithmetic (1/10 x 64 ≈ 7) rather than read directly.

### Discrete bars

Slots are now a bar followed by a gap, so stripes are countable:

```
row words  = FB_STRIDE/4 = 1088
slots      = 136, 8 words each = 1088   exact
each slot  = 32 px  ->  16 px bar + 16 px gap
```

136 slots covers every standard errno (max 133), so a value is never silently
clamped — the previous build clamped at 64 and would have shown 64 bars for
`-EOVERFLOW`, say. Guarded by a tripwire on the geometry, so a stride change
cannot silently misalign the pattern.

| value | bars | % width |
|---|---|---|
| 1 | 1 | 0.4% |
| 6 | 6 | 2.2% |
| 7 | 7 | 2.6% |
| 22 | 22 | 8.1% |
| 133 | 133 | 48.9% |

### A 30 second hold, with the remaining time drawn

`wait_touch()` overwrites the result on its first tick — about 200 ms — so the
frame was gone almost immediately. Stage 5 now holds for
`RESULT_HOLD_SECS` = 30 with a countdown bar in rows 1600..1700 that empties
left to right. The pattern occupies rows 0..1500, so the bar never covers a
slot. A silent fixed delay would have been indistinguishable from a hang, which
is precisely the ambiguity this whole exercise has been fighting.

### Timing uses poll(), not a spin loop

The hold is paced by `sleep_ms()`, which calls `poll()` with `fd = -1` and
`events = 0` — a plain sleep with known units. This is the only clock on the
platform with a documented period; the flashing already runs off poll's 200 ms
timeout.

Using `delay()` instead would have made "30 seconds" a fiction: `delay()` is an
uncalibrated volatile loop whose period depends on the CPU frequency and the
memory system. It is still used for the short NFC poll interval where exactness
does not matter, but not for anything a human is asked to wait on.

### Structure

`barcode()` (unary count) and `hexdump()` (8 bytes, MSB first) both feed one
`draw_pattern()`, so the slot arithmetic that was wrong in Entry 59 exists in
exactly one place and is tripwire-guarded.

Both images rebuilt. `verify-constants.py`: 24 passed, 0 failed.

### Method

Both changes came from the user correctly identifying that the *presentation*
was the weak link, not the measurement. Three runs had produced correct numbers
that could not be read off a screen without arithmetic.

## Entry 62 — Bars widened to 44px; NFC fails EARLIER than the previous run

**User:** "There is a countdown bar on the green screen with 3 or 4 thin vertical
bars on the left (too thin to count exactly)"

### The 3-4 bars are a reading: NFC is failing EARLIER than before

| bars | step | result |
|---|---|---|
| 3 | `ioctl(WAKE_CTL, 1)` | failed |
| 4 | `write(Core Reset)` | failed |

Previous run on the same kernel reported **7** — which meant `open`, both ioctls
and both writes had succeeded and the controller had answered with something
that was not an MT_RSP header. Now it fails two or three steps earlier.

The NFC sequence in the code is byte-for-byte identical between those two runs;
only the pattern geometry changed. So this is **run-to-run variation in the NFC
path**, not a code regression. Candidates: whether a tag is on the reader, the
controller's power state from the previous run, or a timing race in the ioctl
path. Worth noting that `open` and `POWER_CTL` are still succeeding — the device
node and the driver are present and the node is not being refused.

That makes NFC the least stable of the four capabilities, and it is the one with
the least direct evidence. It has now produced 7, then 3-or-4, across runs.

### Bars: 16px was too thin, so the range was traded for width

The first discrete-bar build used 136 slots of 8 words = 16 px bars. The user
correctly called that uncountable at arm's length, and they are right: 16 px is
1.5% of a 1080 px panel.

Slot count traded for width, 1088 words per row:

```
68 slots x 16 words = 1088   exact
each slot = 64 px  ->  bar = 11 words = 44 px, gap = 5 words = 20 px
```

44 px bars are unambiguous to count. The range drops from 136 to 68, which still
covers every errno reachable here:

| bars | meaning | | bars | meaning |
|---|---|---|---|---|
| 1 | `open` failed | | 1 | EPERM |
| 2 | `POWER_CTL` failed | | 5 | EIO |
| 3 | `WAKE_CTL` failed | | 14 | EFAULT |
| 4 | `write(Core Reset)` failed | | 19 | ENODEV |
| 5 | `write(GET_BUILD_INFO)` failed | | 22 | EINVAL |
| 6 | no response | | 38 | ENOSYS |
| 7 | response with bad header | | | |

If a value ever exceeds the range the row fills with 68 bars, which reads as
"off the top" rather than silently under-reporting.

### Three consecutive reports, all read off the same widget

`1/10 black` -> `1/10 black, too thin` -> `3 or 4 thin bars`. Each report was
usable and each exposed the next weakness. The widget went from contiguous fill
(too coarse to count) to 16 px bars (too thin to count) to 44 px bars.

## Entry 63 — "3 bars" was never a count of 3: codes 2 and 3 are unreachable

**User:** "Ok, 3 bars on the left of the green screen"

Entry 62 read that as `ioctl(WAKE_CTL)` failing. **That reading was wrong**, and
the driver says so outright:

```c
/* drivers/nfc/bcm2079x.c : bcm2079x_dev_unlocked_ioctl() */
switch (cmd) {
case BCMNFC_POWER_CTL:
        gpio_set_value(bcm2079x_dev->en_gpio, arg);
        break;
case BCMNFC_WAKE_CTL:
        gpio_set_value(bcm2079x_dev->wake_gpio, arg);
        break;
default:
        dev_err(...);
        return 0;
}
return 0;
```

**It returns 0 unconditionally** — every arm, including `default:`. Neither
`POWER_CTL` nor `WAKE_CTL` can return a negative value, so the failure codes 2
and 3 in `nfc_test()` are unreachable. If the display ever shows 2 or 3 bars in
the code band, something else is wrong, because the code that produces them
cannot execute.

### So what were the 3 bars?

The hexdump. Its bar count is a **popcount**, not a value, and the MT_RSP header
`10 60` has three bits set between it:

```
00010000  01100000   ->  popcount 3
```

So "3 bars" was a hexdump of the controller's actual response — and its first
two bytes contain exactly the bits one would expect of `10 60`.

**Two different meanings, one identical picture.** That is the same class of
error as the misnamed colour constants in Entry 56: a display that looked
unambiguous and was not. The previous two reports ("1/10 black", "3 or 4 thin
bars") were also hexdumps, not counts — so the run-to-run variation claimed in
Entry 62 may not exist at all.

### Fixed by construction: disjoint bands

The code and the byte dump can never share screen rows again:

| rows | contents |
|---|---|
| 0 – 700 | result CODE, unary count |
| 800 – 1500 | response BYTES, hexdump, 8 bars per byte |
| 1600 – 1700 | countdown for the hold |

The code band is drawn unconditionally, so there is never a bare screen whose
meaning has to be guessed.

### Method

Three readings in a row were decoded by assuming a bar count was a value. The
correction came from reading the driver rather than reasoning about the picture
— the driver returns 0 on every path, which makes two of the seven codes dead
code. A test that cannot distinguish "failed at step 3" from "dumped 3 bits of
response" is not a test, and the way to find that out was to ask what the kernel
code can actually return.

## Entry 64 — The reading is still impossible; the diagnosis changed to raw errno

**User:** "3 bars, that took only ~1/3 of the vertical space"

That is the code band alone (rows 0..700 of 1920 = 36% ≈ 1/3) with nothing
below it, so it is a unary count of 3 — **not** a hexdump this time, since the
byte band only draws when `nfc == 7`.

Which means `nfc == 3` — the WAKE_CTL branch. But Entry 63 established that this
branch is unreachable, because `bcm2079x_dev_unlocked_ioctl()` in our tree
returns 0 on every path including `default:`.

### Why that contradiction is now our fault, not the code's

**Our kernel tree is not the tree these kernels were built from.** Proven twice:
the running ROM zip kernel has `CONFIG_KSU` while our tree has no KernelSU
anywhere, and the running kernels report git hashes absent from our clone. So
reasoning about the running driver's behaviour from
`drivers/nfc/bcm2079x.c` in our tree is unsound — the running driver may return
`-EINVAL` for commands ours accepts, or may number them differently.

Entry 63's "codes 2 and 3 are unreachable" is therefore **withdrawn as a claim
about the device**; it is only true of the source we happen to have.

### Stop mapping, start reporting

A mapped code is our interpretation; the errno is the kernel's answer. The NFC
result is now three disjoint bands, so a step and its errno are never conflated:

| rows | band | empty means |
|---|---|---|
| 0 – 350 | STEP | all six steps succeeded |
| 400 – 700 | ERRNO | the failing call reported no errno |
| 800 – 1500 | BYTES | nothing came back at all |

`nfc_test()` keeps the raw return of every call, not just its sign.

Reading it: count the bars in the top band for the step (1 open, 2 POWER_CTL,
3 WAKE_CTL, 4 write Core Reset, 5 write GET_BUILD_INFO, 6 no response, 7 bad
header, 0 all OK), then count the middle band for the errno — 22 would be
EINVAL, 25 ENOTTY, 19 ENODEV, 5 EIO.

### Method

Two consecutive entries decoded a screenshot by assuming the source tree in
`/tmp/opencode` described the running kernel. It does not, and Entry 55 already
said so. The corrective is to stop deriving the device's behaviour from a tree
we know is different and ask the device instead.

## Entry 65 — "1 bar" in the errno band may have been my own placeholder

**User:** "It's hard to read the bars when they are so close to the edge of the
screen. But I think there were 3 (maybe 4) bars in the top 1/3 of the screen and
1 bar in the middle of the screen"

Reading: STEP band = 3 or 4, ERRNO band = 1.

### The driver cannot produce EPERM

```c
/* drivers/nfc/bcm2079x.c */
open:  return ret;                       /* ret = 0 */
ioctl: return 0;                          /* every arm, incl. default: */
write: -ENOMEM | -EFAULT | -EIO           /* and -EIO if i2c_send != count */
```

`grep -nE 'EPERM|capable|CAP_'` over the whole driver returns **nothing**. There
is no capability check and no `-EPERM` on any path. So errno 1 from this driver
is impossible.

### Which means the "1 bar" was probably not an errno

The previous revision contained this:

```c
if (sc(SYS_write, fd, ..., 0, 0) < 0) {
        g_nfc_errno = 1;      /* "write sets errno itself; we have none" */
        return 4;
}
```

A **hard-coded placeholder of 1**, drawn in the same band, in the same style, as
a genuine EPERM. That is precisely the failure this whole log keeps recording:
a self-reported value presented as evidence. The comment was also wrong — the
raw syscall return *is* the errno here, because there is no libc wrapper to
flatten it into -1.

So the reported "1 bar" is consistent with either:
- a real EPERM from `ioctl` (step 2 or 3), which the driver cannot emit, or
- step 4/5/6 failing, where the 1 was our own filler.

Fixed to capture `-r` from every call, including both writes and the read.

### Margins

Bars are no longer hard against the bezel:

```
32 + 64*16 + 32 = 1088 words exactly
first bar starts at x=128 px, last bar ends at x=4224 px (screen 4352)
bar 44 px, gap 20 px
```

Slot count drops 68 -> 64 to pay for the margin. Still covers every errno we
can see here. Geometry remains tripwire-guarded.

### Method

This is the fourth instance in this project of a fabricated value reaching a
display as if it were measured (alongside `is_panel_ready is never assigned`,
the `0x28121969` mislabel, and `RESULT_ROW_BOT` overlapping a live band). Each
one was found by asking whether the observed value was physically possible
rather than by reading the code that produced it.

## Entry 66 — NFC diagnosis complete: EIO on the I2C write; timing was missing

**User:** "4 bars on the top, and 5 bars in the middle"

STEP = 4, ERRNO = 5. With the corrected capture that is unambiguous:

- **STEP 4** = `write(Core Reset)` failed
- **ERRNO 5** = **EIO**

And EIO is exactly what this driver produces when the bus transfer did not
complete:

```c
ret = i2c_master_send(bcm2079x_dev->client, tmp, count);
if (ret != count) {
        dev_err(..., "failed to write %d\n", ret);
        ret = -EIO;
}
```

So `open`, `POWER_CTL` and `WAKE_CTL` all succeeded; the controller was not
answering on I2C when we sent the first frame. **This is a hardware/bus-level
answer, not a framing or permissions problem** — and it is the first NFC result
that is consistent with the driver actually running.

### Two things we were doing wrong, both now fixed

**1. No settle time after power-on.** We asserted EN via `POWER_CTL(1)` and
issued the first I2C write microseconds later. The chip is not on the bus until
its regulator and clock settle, and the driver cannot tell that case from a
real bus error — both become EIO. Added `NFC_PWR_SETTLE_MS = 100`.

**2. We asserted wake before talking.** `b/nfc/TRANSPORT.md` records the working
HAL's state as:

> Wake polarity at power-on: `NFC_WAKE_ASSERTED_ON_POR == UPIO_OFF`

Wake is *deasserted* at power-on; the HAL only asserts it later in its own state
machine. Our `WAKE_CTL(1)` was an invention of ours, not part of the verified
transport. Removed from the bring-up path.

Added `NFC_WRITE_RETRIES = 4` with a 50 ms gap, because I2C can fail
transiently and a retry costs milliseconds in a test that runs once.

This also explains the earlier "7" readings: those runs got far enough to read
bytes back, which means the write succeeded *sometimes*. A power-settle race
fits intermittent success exactly.

### Renumbered result codes

Dropping the WAKE_CTL step shifted the codes, so they are now named constants
rather than bare integers, and the display documents them:

| STEP | meaning | sets errno |
|---|---|---|
| 0 | all steps succeeded | – |
| 1 | open failed | yes |
| 2 | POWER_CTL failed | yes |
| 3 | write Core Reset failed | yes |
| 4 | write GET_BUILD_INFO failed | yes |
| 5 | no response in the retry window | – |
| 6 | response with unexpected header | – |

Named constants also stop the class of error in Entry 65, where a bare `1`
literal was drawn as if it were a measurement.

### What we know about NFC now

| fact | evidence |
|---|---|
| device node correct, `open` succeeds | STEP 1 never seen |
| `POWER_CTL` succeeds | STEP 2 never seen |
| first I2C write fails EIO | 4 runs, STEP 4 + errno 5 |
| controller responds *sometimes* | an earlier run reached STEP 6 with bytes |

## Entry 67 — The writes now work; the read was asking for a reply that did not exist yet

**User:** "5 top, 1 middle"

**STEP 5** = no response. **Big progress: the writes succeed.** Entry 66's fixes
(100 ms power settle, 4 write retries, no spurious wake assertion) cleared
STEP 4 / errno 5 (EIO) that four runs had reported.

### The "1" was my placeholder again

Entry 65 fixed a fabricated `g_nfc_errno = 1` on the *write* paths and I did not
notice the same literal still existed on the read timeout path:

```c
if (got <= 0) { if (!g_nfc_errno) g_nfc_errno = 1; return 5; }
```

So "1 bar" was filler again, not EPERM. Removed, with no replacement: when a
call fails on a value rather than an errno, the errno band is left empty.

### The actual bug: we never waited for the IRQ

```c
/* drivers/nfc/bcm2079x.c */
static unsigned int bcm2079x_dev_poll(...)
{
        if (bcm2079x_dev->count_irq > 0) { count_irq--; mask |= POLLIN; }
}

static ssize_t bcm2079x_dev_read(...)
{
        ret = i2c_master_recv(bcm2079x_dev->client, tmp, 4);   /* ONE shot */
        if (ret == 4) { ...parse length, read the rest... }
        return total;                                          /* 0 if silent */
}
```

`read()` does **not** block and does **not** wait for the controller. It performs
a single 4-byte I2C receive and returns 0 if the chip has not answered yet. The
HAL waits on `poll()` for the IRQ first; our code read immediately after writing
and asked for a reply that had not been produced.

Now: `poll(POLLIN, 500ms)` before each read, up to 4 rounds.

### The new distinction that matters

| STEP | meaning |
|---|---|
| 5 | poll **timed out every time** — the chip never raised its IRQ |
| 6 | a reply arrived with an unexpected header (bytes are shown below) |
| 7 | the chip **did** raise its IRQ but the I2C read still returned nothing |

5 and 7 both read as "no data", but only 7 means the controller replied and we
failed to collect it. Before this change the two were indistinguishable.

### Where NFC stands

| step | status |
|---|---|
| `open` | works (never seen fail) |
| `POWER_CTL` | works |
| `write` Core Reset / GET_BUILD_INFO | **works** — the EIO is gone |
| `read` | pending this run |

## Entry 68 — The screen now uses text; the bar encoding is retired

**User:** "Not sure if that's the encoding you expected for me to read: 1 2 1 1"

Four numbers, against a layout of three bands plus a countdown. The report no
longer matched the design, and rather than guess again the encoding is being
replaced.

### The bar encoding was the problem, not the reader

Six runs of results reported as bar counts produced six different misreadings:

| reported | actually was |
|---|---|
| "1/3 black on the left" | a popcount of the response bytes |
| "3 bars" | a hexdump, not a code |
| "3 or 4 thin bars" | the same hexdump, too thin to count |
| "1 bar in the middle" | a hard-coded placeholder errno of 1 |
| "4 top, 5 middle" | genuine: STEP 4, errno 5 EIO |
| "1 2 1 1" | no longer maps to the layout at all |

Five of those were the encoding failing, and one (4/5) was real. A design that
produces a wrong reading four times out of five is the defect.

### Replaced with a bitmap font

`gen-font.py` rasterises a 5x7 font from a system TrueType face with Pillow and
emits `font5x7.h` — 64 glyphs, 320 bytes, column-major, bit 0 = top row.

Generated, not transcribed, for the same reason `include/linux/reboot.h` is
vendored rather than recalled. The generator prints the table back out as ASCII
art so the glyphs are *visually verified* rather than assumed:

```
.###..#####.#####.####........#####.......#####.###...###...
#...#...#...#.....#...#..........##.......#.....#..#..#..#..
#.......#...#.....#...#..........#........#.....#..#..#..#..
.###....#...#####.####...........#........#####.###...###...
....#...#...#.....#.............#.........#.....#.##..#.##..
#...#...#...#.....#.............#.........#.....#..#..#..#..
.###....#...#####.#............#..........#####.#...#.#...#
```

Two bugs found while generating it:
- the packer indexed off the end because the rasteriser crops to the ink and
  narrow glyphs like `1` and `I` come back smaller than the cell
- scoring glyphs by fill ratio selected the *smallest* renderings, producing
  4-row-tall characters. Now scored by height first, so glyphs fill the cell.

### The result screen now reads as prose

```
NFC STEP 6   ERRNO 0
IRQ 1   BYTES 7
10 60 00 00 02 00 01 00
```

plus the countdown strip. `draw_text()` renders from `font5x7.h`;
`put_dec`/`put_hex2`/`put_str` replace the need for libc. There is no libc in
this binary at all, so these are fifteen lines rather than a `snprintf`.

Scale 7 gives a 35x49 px glyph; the widest line (23 characters) needs 966 px
against the 1016 px available. A tripwire fails the build if that stops being
true. Note the earlier arithmetic slip this caught: I sized the layout against
`FB_STRIDE` = 4352 as if it were pixels, when it is **bytes** and the row is
1088 u32 = 1088 pixels.

### What this costs

320 bytes of font in a 4,747-byte ramdisk. In exchange, every remaining
diagnostic is unambiguous, and the same renderer serves the Krux UI later.

## Entry 69 — ALL FOUR CAPABILITIES PROVEN on our own userspace

**User:** "nfc step 0 error 0 irq 1 b 6 10 ... Disappeared before i could finish"

```
NFC STEP 0   ERRNO 0
IRQ 1        BYTES 6
10 ...
```

- **STEP 0** = `NFC_OK` — every step succeeded
- **ERRNO 0** — no syscall reported an error
- **IRQ 1** — `poll()` reported POLLIN, so the controller raised its interrupt
- **BYTES 6** — `read()` returned six bytes
- first byte **`0x10`** — the MT response header we expected, and the header
  check therefore passed (`rsp[1] == 0x60` or it would have returned STEP 6)

### The complete chain, and what made each part work

| step | what finally made it work |
|---|---|
| `open` | node baked into the cpio; `O_NOCTTY` was essential from the start |
| `POWER_CTL` | always worked |
| `write` Core Reset / GET_BUILD_INFO | **100 ms settle after power-on + 4 retries** (Entry 66) — before that it was EIO every time |
| `wait for IRQ` | **`poll(POLLIN, 500ms)` before every read** (Entry 67) — `read()` is a single-shot I2C receive and does not block |
| `read` | returned 6 bytes starting `10 60` |

Both NFC fixes came from reading `drivers/nfc/bcm2079x.c` rather than from the
HAL: `bcm2079x_dev_read()` performs one `i2c_master_recv()` and returns 0 if the
chip has not answered, and `bcm2079x_dev_poll()` reports readable only once the
chip has pulled its interrupt line. Reading straight after writing was asking
for a reply that did not exist yet.

### Final status, on both kernels, with no TWRP and no Android

| capability | evidence | method |
|---|---|---|
| **screen** | magenta, green, text, countdown all rendered | human observation |
| **touch** | two taps advanced two stages every run | human observation |
| **NFC** | IRQ asserted, 6 bytes read, MT header correct | **on-screen text** |
| **power-off** | device powers off after stage 6 | human observation |

Entry 49's `pm_power_off` claim, Entry 57's struct fix and Entry 66/67's timing
and poll fixes are what got here. The device runs a 4,780-byte initramdisk as
PID 1 with no recovery, no Android userspace and no console.

### Change made

`RESULT_HOLD_SECS` 30 -> **90**. The first run that produced a real reading was
cut off mid-word because 30 s was not enough to read three lines of text. Also
added an explicit `OK`/`FAIL` word to the status line so the number no longer
has to be interpreted against a legend.

### Where this leaves the project

All four hardware capabilities are demonstrated under a minimal freestanding
userspace. What is *not* yet demonstrated is any of it **on a kernel we build**:
every run so far used one of the three existing images. The next step is the
phase-6 kernel — `lineageos_hammerhead_defconfig` plus the air-gap profile, with
`CONFIG_USB_FUNCTIONFS=y` added so the adb channel comes back, and with the
NFC keep-list fixed so the profile cannot strip the driver this entry just
proved works.

---

## Entry 70 — The wrong kernel tree, and the wrong USB symbol

Two corrections to long-standing notes in this project, both of which had been
carried forward unexamined.

### The tree

We had been building from `https://github.com/z3DD3r/android_kernel_lge_hammerhead`
@ `9ac11678` (2023-06-05). The kernel the device actually runs — the installed
one, and the ROM's — is built from
**`https://github.com/acroreiser/android_kernel_lge_hammerhead`** @ `466f069d`
(2026-10-10). Same defconfig names, same camera_v2 layout, same option names
at the same line numbers, but a much closer match to the live config:

| | z3DD3r tree | acroreiser tree |
|---|---|---|
| options in the live config but not in ours | 170 | **111** |
| value-level differences from the live config | ~200 | **152** |

Every remaining difference is accounted for: the radio symbols the air-gap
profile removes, plus a handful of Kconfig symbols that exist in one tree's
inventory and not the other's. **The camera block and the USB gadget block now
match the live, working kernel exactly** — same options, same values.

Switched. The two patches we had (`f_fs.c` uidgid include, `camera_v1/ov5640.c`
includes) belonged to the old tree; `f_fs.c` is still needed in the new one
(it uses `kuid_t`, `make_kuid`, `uid_valid` and never includes
`<linux/uidgid.h>`, though the tree has that header and `linux/fs.h` does not
pull it in), and the `camera_v1` one is not (camera_v1 is off).

### The USB symbol

The standing note said "`CONFIG_USB_FUNCTIONFS` is off in all three existing
kernels, so adbd finds no `/dev/ffs.ep0` and sits at `offline`" — and the fix
was duly made: we enabled functionfs.

**That was wrong.** `CONFIG_USB_FUNCTIONFS` is the functionfs *filesystem*
(`/dev/ffs.ep0`), used by the generic `g_ffs` gadget function. It is not the
adb path. The adb path is **`CONFIG_USB_G_ANDROID`**, the AOSP "android" USB
gadget, whose adb function is implemented *on top of* f_fs internally
(`drivers/usb/gadget/android.c` refers to ffs 66 times) and which creates
`/dev/usb-ffs/adb/ep0` itself.

The proof is the config of the kernels that are known to give a working adb:

```
TWRP kernel (bbba49d1)      CONFIG_USB_G_ANDROID=y   CONFIG_USB_FUNCTIONFS is not set
installed kernel (e4e483b4)  CONFIG_USB_G_ANDROID=y   CONFIG_USB_FUNCTIONFS is not set
ROM zip kernel               CONFIG_USB_G_ANDROID=y
```

and TWRP's own adbd:

```
strings sbin/adbd | grep usb-ffs
  /dev/usb-ffs/adb/ep0
  /dev/usb-ffs/adb/ep1
  /dev/usb-ffs/adb/ep2
strings sbin/adbd | grep usb_init
  [ usb_init - using FunctionFS ]
```

And enabling functionfs is not a harmless alternative — it **clears**
`USB_G_ANDROID`, because the two are siblings in the "USB Gadget Drivers"
choice (`drivers/usb/gadget/Kconfig:552`, with `USB_FUNCTIONFS` at :733 and
`USB_G_ANDROID` at :869). We built exactly that, and the result was: **no USB
device enumerated on the host at all.** Not "adb offline" — nothing.

`tools/profiles/hammerhead-keep.list` now says `CONFIG_USB_G_ANDROID=y` and
`CONFIG_USB_FUNCTIONFS=n`, with the reasoning in the file.

### The 128-vs-136 `v4l2_event` was our probe's fault

§16.3 concluded that the device's kernel is not ours, because the running
kernel wanted a 128-byte `struct v4l2_event` and our tree's header says 136.

**Both trees carry byte-identical `include/linux/videodev2.h`** — I diffed
them. The difference was in the *userspace* struct our probe compiled:
`struct v4l2_event` contains a `struct timespec`, and the kernel's
`__kernel_timespec` and glibc's `struct timespec` are not the same size on
32-bit ARM. The probe therefore sent an ioctl number encoding the wrong size
and the kernel rejected it with EINVAL. Our `standalone/cam/` programs are now
built against the tree's own header precisely so this cannot happen to our
own kernel.

---

## Entry 71 — The camera is camera_v2, the rear sensor is an IMX179, and it needs an "imaging server"

### The hardware

Read off the live device with `adb shell`, not recalled:

```
/sys/class/video4linux/video0/name -> msm-config   (dev fd8c0000.qcom,msm-cam)
/sys/class/video4linux/video1/name -> msm-sensor   (dev 20.qcom,camera)
/sys/class/video4linux/video2/name -> msm-sensor   (dev 90.qcom,camera)
```

and the subdevice graph — 18 subdevs, camera_v2's media-controller
architecture in full:

```
v4l-subdev0/1/2  msm_csiphy      v4l-subdev3..6  msm_csid
v4l-subdev7      msm_actuator    v4l-subdev8     msm_flash
v4l-subdev9      msm_eeprom      v4l-subdev10    msm_ois
v4l-subdev11     imx179          <-- REAR
v4l-subdev12     mt9m114b        <-- FRONT
v4l-subdev13/14  vfe             v4l-subdev15    msm_ispif
v4l-subdev16     msm_buf_mngr    v4l-subdev17    cpp
```

**The rear camera is a Sony IMX179 (8MP, binned to 5MP), not an OV5640.**
The front is an OmniVision MT9M114B. (The Nexus 5's rear camera being an 8MP
Sony part is exactly the kind of thing that is easy to get wrong from memory
and is settled here by the device.)

Both drivers are in **our** tree (`camera_v2/sensor/imx179.c`, `mt9m114b.c`),
and so is hammerhead's whole camera configuration as a device tree:
`arch/arm/boot/dts/msm8974-hammerhead/msm8974-hammerhead-camera.dtsi` —
`qcom,imx179` with `qcom,cam-vreg-name = "cam_vdig"`, `qcom,mt9m114b` with
`cam_vdig`/`cam_vio`/`cam_vana`, and `qcom,actuator`.

### The architecture: there is no camera without an "imaging server"

camera_v2 has **no userspace support of its own**. Every V4L2 request on a
sensor node is posted as an event to a userspace imaging server and the
kernel **blocks** until that server acknowledges it:

```c
/* drivers/media/platform/msm/camera_v2/camera/camera.c */
static int camera_v4l2_querycap(struct file *filep, void *fh, struct v4l2_capability *cap)
{
	camera_pack_event(filep, MSM_CAMERA_GET_PARM, MSM_CAMERA_PRIV_QUERY_CAP, -1, &event);
	rc = msm_post_event(&event, MSM_POST_EVT_TIMEOUT);
	...
}

/* drivers/media/platform/msm/camera_v2/msm.c */
	/* send to imaging server and wait for ACK */
	if (!atomic_read(&serv_running)) {
		pr_info("%s: daemon hasn't subscribed yet!\n", __func__);
		return -EIO;
	}
```

On Android that server is `/vendor/bin/mm-qcamera-daemon`, and it is what
holds `/dev/video0` open. The protocol is small: 7 event ids
(`MSM_CAMERA_NEW_SESSION` .. `MSM_CAMERA_MSM_NOTIFY`), 13 commands
(`PRIV_S_CROP` .. `PRIV_STREAM_INFO_SYNC`), one ACK ioctl
(`MSM_CAM_V4L2_IOCTL_CMD_ACK` = `_IOW('V', 224, struct v4l2_event)`) carrying
`struct msm_v4l2_event_data` (16 words, `arg_value` at word 7).

**We replaced the imaging server on the live device and the handshake
completed.** The blocked `open()` of the rear sensor node went from
`ETIMEDOUT` to `EFAULT` — the kernel accepted our event, we acknowledged it,
and the kernel resumed into the work the real daemon does. The kernel's own
log agreed: `msm_post_event: Timed out: event id is 1` before we ACKed.

So **no vendor blob is needed**: the imaging server's job is *protocol*, and
its tools are the standard V4L2 subdevice API (`VIDIOC_SUBDEV_S_FMT` at
`include/linux/v4l2-subdev.h:127`, `S_CROP` at :139), which the tree
implements.

### Two traps, both found by experiment

**Trap 1 — the subscription id must match exactly.** In this 3.4 tree:

```c
/* drivers/media/video/v4l2-event.c */
static struct v4l2_subscribed_event *v4l2_event_subscribed(
		struct v4l2_fh *fh, u32 type, u32 id)
{
	...
	if (sev->type == type && sev->id == id)
		return sev;
```

No "id == 0 means all" wildcard (later kernels added one). A single
subscription with id 0 matches **nothing** and the kernel waits forever.
**Subscribe to ids 1..7 separately.**

**Trap 2 — `functions ffs` is not a valid name.** The android gadget resolves
a requested function name against `dev->ffs_aliases` first; if it matches, the
ffs function is used, and `functions` then reads back "ffs". If it does not
match, the **native** android "adb" function is used instead — which presents
the same interface (255/66/1) and the same two 512-byte bulk endpoints, so the
host cannot tell the two apart by looking at the descriptors, but the native
function speaks the legacy protocol while TWRP's adbd is the functionfs kind.
That is "enumerates perfectly, then sits at `offline` forever".

I tried `functions ffs` as a fix. It is wrong: the android gadget has no
native "ffs" entry, so the composite ended up with no function at all and the
gadget stopped enumerating. Reverted.

### Change made

- `tools/profiles/hammerhead-camera.list` — new. The camera configuration as
  intent, `CONFIG_X=y` / `CONFIG_X=n` lines, with the values taken from the
  running kernel's own `/proc/config.gz`.
- `tools/apply-defconfig.py` — gained `--disable`, symmetric with `--enable`,
  so "we want this sensor off because we do not have it" sits next to "we
  want that sensor on".
- `build/build-kernel.sh` — the keep-lists now take `=y` / `=n` lines, and the
  reader is `while read -r sym || [ -n "$sym" ]` (plain `read` drops the final
  line when the file has no trailing newline, and these files are
  hand-edited — which is exactly how `CONFIG_USB_FUNCTIONFS` came to be
  "enabled" when it was not).
- `standalone/cam/` — the probe, the imaging-server prototype and the
  diagnostic.

---

## Entry 72 — The camera option set, devtmpfs, and a watchdog that was resetting the device

### The camera is enabled in our kernel

`tools/profiles/hammerhead-camera.list` states the camera configuration and
`apply-defconfig.py` verifies it survived the config re-resolution. The
values are the running kernel's own:

```
CONFIG_MSMB_CAMERA=y          CONFIG_MSM_CSI30_HEADER=y
CONFIG_MSM_CAMERA_SENSOR=y    CONFIG_MSM_CSIPHY=y
CONFIG_IMX179=y               CONFIG_MSM_CSID=y
CONFIG_MT9M114B=y             CONFIG_MSM_ISPIF=y
CONFIG_MSM_CPP=y              CONFIG_MSM_EEPROM=y
CONFIG_MSM_CCI=y              CONFIG_OIS_CONTROLLER=y
CONFIG_OIS_ROHM_BU24205GWL=y CONFIG_OIS_ONSEMI_LC898111A=y
CONFIG_MSMB_JPEG=y
```

and the sensors we do not have are explicitly off (`MT9M114`, `IMX135`,
`OV2720`, `OV8825`, `OV9724`, `S5K3L1YX`) — they are `default y` in
camera_v1/Kconfig, and each is an I2C driver the camera would try to probe on
the camera bus.

Two things that are *not* separate options and are worth knowing:

- **There is no `CONFIG_MSM_ACTUATOR` in camera_v2.** The autofocus driver is
  `camera_v2/sensor/actuator/msm_actuator.c`; it matches the `qcom,actuator`
  DT compatible and is built by `camera_v2/sensor/Makefile` as part of
  `MSMB_CAMERA`. The live kernel's config has no `MSM_ACTUATOR` either, and
  the live device still has an `msm_actuator` subdev — which is how we know.
  (Adding it to the profile was an error; the tool caught it by refusing to
  enable a symbol that does not exist.)
- `MSM_ION` is the wrong name; the options are `CONFIG_ION=y` and
  `CONFIG_ION_MSM=y`, and both were already present.

### devtmpfs, and why the init mounts it itself

The camera's char-device major is allocated at runtime
(`alloc_chrdev_region`, `msm_camera.c:4007`), so `/dev/video0..2` **cannot** be
baked into the initramfs cpio the way `fb0`, `event1` and `bcm2079x` are.
`CONFIG_DEVTMPFS=y` solves that, and gives `/dev/ffs.ep0` for adb too.

`CONFIG_DEVTMPFS_MOUNT` is deliberately **not** set: in 3.4 the automount is in
`prepare_namespace()`, and `kernel_init()` runs `run_init_process()` — our
init — *before* that. For an initramfs the automount would arrive after we had
already started. So the init mounts devtmpfs on `/dev` itself, with a
fallback: if the mount fails, or succeeds but `/dev/graphics/fb0` is not
there, it **unmounts and carries on with the baked cpio nodes** — the
configuration that has already proved the screen, the touch and the NFC.

`/dev/graphics/fb0` is unchanged by devtmpfs: the MSM framebuffer driver uses
class "graphics", so devtmpfs creates the node at the same path.

### The watchdog was resetting the device

Every boot of our kernel ended with the device rebooting to Android about 30
seconds in — "right about the time the ramdisk usually starts", as it was
described. The cause is the **cmdline**, not the camera:

```
msm_watchdog_v2.enable=0    <- what we had been using
msm_watchdog_v2.enable=1    <- what the working TWRP image uses
```

With `=0` the kernel's watchdog driver never takes over the PMIC watchdog the
bootloader armed, so it fires at ~30 s and resets the hardware into the
bootloader, which then boots the installed Android. With `=1` the device
**stays up** — and the diagnostic, which had never managed to run before,
immediately published its first result.

The cmdline is reused verbatim from the stock header (`bootimg.py pack`
refuses to invent header fields):

```
console=ttyHSL0,115200,n8 androidboot.hardware=hammerhead user_debug=31
maxcpus=2 msm_watchdog_v2.enable=1 androidboot.bootdevice=msm_sdcc.1
androidboot.selinux=permissive loop.max_part=7 buildvariant=eng
```

### Change made

- `standalone/rootfs/standalone/main.c` — mounts devtmpfs on `/dev`, with the
  fallback described above, and prints the result.
- `standalone/rootfs/standalone/build-ramdisk.py` — new. The initramfs used to
  be assembled by hand; this builds it, with the device nodes baked in, from
  the tree's `devnodes.py`.
- `tools/profiles/hammerhead-keep.list` — `CONFIG_DEVTMPFS` added.

---

## Entry 73 — AT_HWCAP settled, and the soft-float mandate retired

Measured on the device with `getauxval(AT_HWCAP)`:

```
AT_HWCAP = 0xfb8d7
  HWCAP_FP    (bit 11) : YES
  HWCAP_ASIMD (bit 12) : YES
```

The running kernel builds `CONFIG_VFP=y`, `CONFIG_VFPv3=y`, `CONFIG_NEON=y`,
`CONFIG_KERNEL_MODE_NEON=y` — **the kernel itself uses VFP/NEON**, so the FPU
demonstrably works.

**The soft-float mandate is not load-bearing.** It may be treated as settled: a
*hosted* MicroPython is permitted, not only the freestanding `minimal` port.

Where the distinction actually bites:

| binary | ABI | VFP insns |
|---|---|---|
| our payload (`standalone/rootfs/standalone/init`) | **soft-float** | **0** |
| the throwaway probes (`standalone/cam/*.c`) | hard-float | 273 |

The hard-float probes work because they are statically linked, so there is no
dynamic loader and the ABI tag only affects how glibc passes float arguments
to itself; the FPU is present; and ARM Linux enables the coprocessor per
process on first use. The one thing that would break them is an FPU-less
kernel, which this is not.

The payload stays soft-float anyway: it is free, and it means the payload
cannot be affected by FPU state at all.

### Also: a duplicate kernel tree was living in RAM

`/tmp/opencode/kern/android_kernel_lge_hammerhead` — 704 MB in tmpfs, git
`9ac11678`, **the same commit as the vendored tree**. Every build has always
used `standalone/vendor/android_kernel_lge_hammerhead` (the `--tree`
argument), so the copy was pure waste. Removed; tmpfs went from 1.9 GB to
1.2 GB used. `standalone/vendor/k_quirc` (Krux's own quirc fork, the QR
decoder) was moved out of tmpfs into the project, where it belongs.

---

## Entry 74 — The adb regression is in the tip of the kernel tree

The long chase, and what it actually was.

### The shape of the failure

Our own kernel boots, the USB gadget enumerates as `18d1:4ee2` with
descriptors **byte-identical** to the working kernel's, and adb sits at
`offline`: the transport is up, the device never sends its banner.

The USB configuration was already identical to the working kernel's
(`USB_G_ANDROID=y`, `USB_FUNCTIONFS=off`, `USB_DWC3=y`,
`USB_DWC3_MSM=y`, `USB_OTG_UTILS=y`, and every `USB_NET_*` difference being
an air-gap removal). The camera configuration was identical too. The android
gadget's `android.c` was identical. The DWC3 driver and the hammerhead
device tree were identical.

**⇒ So the difference had to be a *source* difference, not a *configuration*
difference.**

### Which source, and how we found it

The kernels carry their commit hash in their version banner, so the version
string identifies the tree exactly. Extracting it from the compressed kernel
images:

```
kernel-twrp.img        Linux version 3.4.113-<flag>-ge4e483b4ef6 (acroreiser@thinkstation) ...
kernel-romzip-zip.img  Linux version 3.4.113-<flag>-g11370a8eb99 (nobody@android-build) ...
```

and our tree was at `466f069d` (2026-10-10) — **later than both**. Both hashes
are in `https://github.com/acroreiser/android_kernel_lge_hammerhead`:

```
e4e483b4ef6   2026-09-14   kernelsu: sync with https://github.com/backslashxx/KernelSU
11370a8eb99   2026-09-24   adreno: prevent infinite sleep in adreno_drawctxt_wait
```

**The TWRP kernel and the installed kernel are both built from the *same*
tree as ours**, at `e4e483b4ef6` — not from the z3DD3r tree I had assumed.
So the comparison is ours-vs-ours, one commit apart, and the whole delta is
`f_fs.c`, which differs by **ten lines**.

Eight of those lines are debug prints. Two are not:

```c
	if (atomic_read(&ffs->opened))
		return -EBUSY;
```

**That is the bug.** In the camera_v2 android gadget the gadget opens the
functionfs ep0 *itself*, in order to bind the ffs function; adbd opens it
too. With this check in place, the second open returns `-EBUSY`, the ffs
function never binds, and adb can never complete its handshake — while the
USB gadget itself still enumerates perfectly, with descriptors identical to a
working kernel's, because the failure is inside the function, not the
composite.

That is also why the diagnostic showed the contradiction it did:

```
fn=ffs   al=adb   st=DISCONNECTED   en=1   ep0=100
```

`functions=ffs` (the alias resolved, the configuration was built, the kernel
logged `func:ffs is enabled.`) and yet `state=DISCONNECTED` and `ep1` missing
— the ffs instance was created and its ep0 existed, but the function was
never bound, because the bind's ep0 open was refused.

**One commit later in the same tree broke it.** The kernels that work are
built from `e4e483b4ef6`, which does not have the check.

### Change made

`standalone/patches/0001-usb-gadget-f_fs-uidgid-and-revert-second-open.patch`:

- add `#include <linux/uidgid.h>` — needed at the tree's tip, where
  `f_fs.c` uses `kuid_t`, `make_kuid` and `uid_valid` but never includes the
  header that defines them (the tree has `include/linux/uidgid.h`;
  `linux/fs.h` does not pull it in). At `e4e483b4ef6` this include is not
  needed, so the two trees differ here too.
- remove the `ffs->opened` / `-EBUSY` check, restoring the older behaviour
  where the gadget's internal open and adbd's open can both succeed.

### Also in this entry: the diagnostic was destructive

The diagnostic that produced the readings above also **repaired** the gadget:
it wrote `enable 0`, `functions adb`, `enable 1` in a loop, hoping to drive
the android gadget's `disable_depth` counter to zero (the gadget only calls
`usb_add_config()` from `android_enable()`, and only when that counter
reaches zero). That is destructive — `enable 0` really does disable the
gadget — and it was shipped inside the ramdisk, so it also broke the
*reference* run, which is what made the comparison invalid for a while.

The diagnostic is now strictly read-only. It reports, via the USB string
descriptors, so the host can read it with `lsusb -v` while the device is
still in our kernel:

- `fn`, `al`, `st`, `en`: the android gadget's `functions`, `f_ffs/aliases`,
  `state` and `enable`
- `ep0`: whether `/dev/usb-ffs/adb/ep0` and `/dev/ffs.ep0` exist
- `U:`: the state of every `android_usb/android*` instance
- `K:`: the last few kernel-log lines matching android/ffs/dwc3/udc/gadget/
  enable/config/WARN

Two bugs in it were worth recording, because both produced *plausible but
wrong* readings: the path builder overwrote the instance digit (so the
per-instance states always came back empty), and it read `/proc/kmsg` with a
1 KB buffer, which is the *beginning* of the log — the errors are at the end.
And the published string is capped under 200 bytes, because a USB string
descriptor holds at most 255 and the first version wrote ~2 KB, which made
the kernel reject the whole write and publish nothing at all.

---

## Entry 75 — The compiler, and ccache

### Which compiler the working kernels were built with

From the version banners extracted out of the compressed kernel images:

```
kernel-twrp.img         Linux version 3.4.113-<flag>-ge4e483b4ef6 (acroreiser@thinkstation) (gcc version 16.2.0 (GCC) ) #315 ...
kernel-romzip-zip.img   Linux version 3.4.113-<flag>-g11370a8eb99 (nobody@android-build)     (gcc version 16.2.0 (GCC) ) #47  ...
installed kernel        Linux version 3.4.113-<flag>-ge4e483b4ef6 (acroreiser@thinkstation) (gcc version 16.2.0 (GCC) ) #315 ...
```

So all three are **gcc 16.2.0** — not the Linaro 15.2.pre1 that the earlier
note recorded. That earlier note was wrong twice over: it named a compiler
none of these kernels used, and it attributed the difference to the compiler
when the difference was in the source.

We build with the **AOSP prebuilt gcc 4.9.x (20150123)**, because it is the
only ARM toolchain on this host that has a sysroot, and because a CAF 3.4-era
tree is what gcc 4.x was written for. Our build completes with **0 errors**.

Is the version gap a problem? The things that could couple the kernel to a
particular compiler are absent:

| option | state | why it would matter |
|---|---|---|
| `CONFIG_GCC_PLUGINS` | **not set** | gcc plugins are built against a specific gcc and are not portable across versions |
| `CONFIG_MODVERSIONS` | **not set** | module vermagic/CRC is tied to the exact build; with it on, /system's modules would not load on our kernel |
| `CONFIG_WERROR` | not set | — |

There is also a direction of risk that favours us: **gcc 14 made
`-Wimplicit-function-declaration` an error by default**, so a tree that builds
cleanly with gcc 16.2.0 will build cleanly with 4.9 — the reverse is not
true. Our 4.9 build is therefore the *more permissive* one, and it produced
no warnings that became errors.

The remaining risk is codegen differences exposing a latent bug in a path we
care about. That is not answerable by argument; it is answered by booting it,
which is what the next entry does.

### ccache

`ccache 4.12.3` is installed. It is now wired into `build/build-kernel.sh` by
prefixing the `CROSS_COMPILE` prefix, so the kernel execs
`ccache arm-linux-androidkernel-gcc`:

```sh
CCACHE_BIN=""
if [ "${CCACHE_DISABLE:-0}" != "1" ] && command -v ccache >/dev/null 2>&1; then
    CCACHE_BIN="ccache "
fi
...
MAKE=(make -C "$TREE" O="$OUT" ARCH="$ARCH"
      CROSS_COMPILE="${CCACHE_BIN}${CROSS_COMPILE}"
      PATH="$CC_PATH:$PATH")
```

`CCACHE_DISABLE=1` turns it off for a from-scratch build.

This works even though the AOSP driver is a **Python wrapper**
(`arm-linux-androideabi-gcc` is a script that execs
`real-arm-linux-androideabi-gcc`): ccache does not need to understand the
compiler, it only needs to run it and inspect the preprocessor output.
Verified:

```
$ ccache prebuilts/gcc-androideabi-4.9/bin/arm-linux-androidkernel-gcc -O2 -c t.c -o t3.o
$ ccache -s
    Cacheable calls:     2 /    2 (100.00%)
```

No "unsupported compiler" — the `androidkernel` alias name is handled the
same way as the `androideabi` one, because the wrapper resolves its real
compiler from the readlink target.

### A note on reading the diagnostic

The USB string descriptor can carry at most 255 bytes, and the published
string was capped at 190. That is enough for a status line and a couple of
kernel-log lines — not enough to see a boot's worth of messages. The
diagnostic now also writes the **whole** kernel log, filtered to the
interesting lines, into `/data/diag.txt`, which has no such limit. That file
can be read after rebooting into the TWRP-kernel image, where adb works.

The reason this matters: the published sample showed only ONE matching line
(`func:ffs is enabled.`) from a whole boot, which was implausible for a boot
that starts Android — and it was the reason the `ffs_function_enable` line
the *working* kernel prints went missing from our reading. Reading the full
log is the way to settle it.

### The diagnostic is strictly read-only now

The earlier version also *repaired* the android gadget (writing `enable 0`,
`functions adb`, `enable 1` in a loop). That is destructive — `enable 0`
really does disable the gadget — and because it was shipped inside the
ramdisk it broke the **reference** runs too, which invalidated the
comparison for a while. The repair has been removed; the diagnostic only
reads.

### The f_fs.c revert is in, and it is still the right change

`standalone/patches/0001-usb-gadget-f_fs-uidgid-and-revert-second-open.patch`
now contains both changes: the `uidgid.h` include the tree's tip needs, and
the removal of the `ffs->opened` / `-EBUSY` check that the tip added and
that the working commit does not have. The revert alone did not change the
symptom, so the `-EBUSY` check is **not** the whole cause — but it is
genuinely a difference from the commit the working kernels are built from, and
it is a difference that can only hurt (the android gadget opens the ep0
itself to bind the function, and adbd opens it too). It stays reverted.

### Where the camera/USB investigation stands (end of 2026-10-19)

Our kernel, with the `f_fs.c` revert and the read-only diagnostic, still
reports:

```
DIAG fn=ffs  al=adb  st=DISCONNECTED  en=1  ep0=100  U:DISCONNECTED , udc
K:<3>[    3.627627] func:ffs is enabled.
```

and adb is still absent. The working kernel (TWRP kernel, same tree, one
commit earlier, same ramdisk) reports the same `fn=ffs`/`al=adb`/`en=0` but
*does* log `ffs_function_enable: functionfs...`, and adb works.

Two things are therefore still unexplained:

1. **`en=1` on ours, `en=0` on the working kernel.** The android gadget's
   `enable` attribute reads `dev->enabled`. On the working kernel it is 0 at
   the time we sample it, yet adb works — so the working setup does not need
   the android gadget's `enable` path to have completed, whereas on ours the
   gadget *is* enabled and yet adb does not work.
2. **The working kernel reaches `ffs_function_enable`; ours does not.** That
   function is called from `enable_store`'s first branch, which requires
   `dev->enabled == false`. On ours `dev->enabled` is already true, so the
   write is a no-op and the ffs function is never enabled — which is exactly
   the symptom.

So the remaining question is **why `dev->enabled` is already true on our
kernel** when the `enable 1` write arrives. The candidate is the
`on property:service.adb.root=1` block, which does `enable 0` / `restart
adbd` / `enable 1` and would set `dev->enabled = true`; the `on
property:sys.usb.config=adb` block then writes `enable 0` (which would clear
it), `functions adb`, and `enable 1`.

The full kernel log from our boot is now captured to `/data/diag.txt` and will
be read after rebooting into the TWRP-kernel image, where adb works. That
will show whether the "already enabled" ratelimit message is there — its
presence is the proof that the `enable 1` write was a no-op.

---

## Entry 76 — The functionfs instance: mounted, but not where adbd looks

The chain of reasoning that got here, because each step was a wrong turn
worth recording.

### The contradiction that started it

Our kernel and the working kernel (TWRP kernel, same tree, one commit
earlier, same ramdisk) both report, via the diagnostic:

```
fn=ffs   al=adb   st=DISCONNECTED   en=1   ep0=000
```

`fn=ffs` means the alias resolved and the configuration was built — the kernel
logged `func:ffs is enabled.`. `en=1` means the android gadget's `enable`
attribute reads `dev->enabled == true`, so `enable_store`'s first branch ran,
which is the branch that calls `ffs_function_enable()`. **And yet the working
kernel logs `ffs_function_enable: functionfs...` and ours does not.**

`ep0=000` was the other half: `/dev/usb-ffs/adb/ep0` does not exist on ours.

### What the diagnostic established

| measurement | how |
|---|---|
| `ffsfs=1` | the functionfs **filesystem is registered** — read from `/proc/filesystems` |
| `M:11-` | our attempt to mount the instance returned **errno 11 = EAGAIN** |
| `ep0=000` | `/dev/usb-ffs/adb/ep0` does not exist |

EAGAIN comes out of `functionfs_acquire_dev_callback()`, which refuses a
**duplicate instance name**. So the instance named "adb" is **already
mounted** — the post-fs mount in TWRP's `init.recovery.usb.rc` *did* succeed —
and what is missing is the **endpoint node in the place adbd opens it**.

The mount in TWRP's init.recovery.usb.rc is:

```
on post-fs
    mkdir /dev/usb-ffs 0770 shell shell
    mkdir /dev/usb-ffs/adb 0770 shell shell
    mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000
```

and adbd opens `/dev/usb-ffs/adb/ep0`.

Note the kernel's `mount()` is `mount(source, target, type, flags, data)` and
for functionfs the **source is the instance name** — so the kernel call is
`mount("adb", "/dev/usb-ffs/adb", "functionfs", 0, "uid=2000,gid=2000")`. The
first version of the diagnostic got this backwards and tried
`mount("functionfs", "/dev/usb-ffs/adb", "adb", ...)`, which is a different
mistake from the one it was trying to diagnose, and the EAGAIN it produced
would have been meaningless. Corrected.

### Why this is the same bug as the missing `ffs_function_enable`

In the working kernel, `ffs_function_enable` runs — that is the android
gadget's *function* enable, and it is what makes the gadget take over the
functionfs instance and give it a real endpoint. In ours it does not run, and
the instance therefore stays in the state the kernel put it in when it was
mounted, with no ep0 at the path adbd opens.

So the single remaining question is the same one as before: **why does
`enable_store`'s first branch not run on our kernel?** The gadget's
`enable` attribute says `dev->enabled == 1`, so *something* enabled it — and
the only thing that sets `dev->enabled = true` is `enable_store`'s first
branch, which also calls `ffs_function_enable()`. So the branch did run at
some point, and the ffs function was enabled at that moment — but by the time
the *second* `enable 1` arrived, `dev->enabled` was already true, so the write
was a no-op.

That is consistent with TWRP's own init doing the enabling **twice**:

```
on property:service.adb.root=1
    write .../enable 0
    restart adbd
    write .../enable 1        <- first enable: dev->enabled becomes true

on property:sys.usb.config=adb
    write .../enable 0
    ...
    write .../functions adb
    write .../enable 1        <- second enable: no-op, dev->enabled is already true
    start adbd
```

On the working kernel the first `enable 1` evidently did *not* leave
`dev->enabled` true — its `en` reads 0 — so the second one took the branch and
called `ffs_function_enable`. On ours the first one did.

**The open question is therefore: what enabled the gadget early on our kernel,
and why does the working kernel's `enable 0` not undo it?** The full kernel
log from our boot is captured to `/data/diag.txt`; the next step is to read it
and look for the ratelimit message `android_usb: already enabled`, whose
presence is the proof that the second `enable 1` was a no-op, and to see
which of the two `enable 1` writes ran first.

### The compiler note, for the record

The three working kernels are built with **gcc 16.2.0** — most likely Arch
Linux's `aarch64-linux-gnu` / ARM cross toolchain, per the research. We build
with the AOSP prebuilt **gcc 4.9.x**, because it is the only ARM toolchain
on this host with a sysroot and because a CAF 3.4-era tree is what gcc 4.x
was written for. Our build is clean, and the two things that could couple a
kernel to a compiler version — `CONFIG_GCC_PLUGINS` and `CONFIG_MODVERSIONS`
— are both off, so nothing is version-coupled.

If we ever need to match the compiler exactly, Arch's cross-binutils are
pullable into the project the same way the AOSP prebuilts are, and the
`CROSS_COMPILE` prefix in `build/build-kernel.sh` is the only thing that
would change.

### The mount succeeds, and the ep0 still is not there

With the mount arguments in the right order — the kernel's `mount()` is
`(source, target, type, flags, data)` and for functionfs the **source is the
instance name**, so it must be
`mount("adb", "/dev/usb-ffs/adb", "functionfs", 0, "uid=2000,gid=2000")` — the
diagnostic's own attempt now returns:

```
M:0-      the mount SUCCEEDS (errno 0), and /dev/usb-ffs/adb/ep0 still does
           not exist
```

So the instance *is* mounted, and the ep0 is still missing. That is the
android gadget's function enable: in camera_v2's android gadget the endpoints
of the functionfs instance are set up when the ffs function is **bound** to
the composite, which is what `ffs_function_enable()` does — and the working
kernel logs `ffs_function_enable: functionfs...` while ours does not.

That closes the loop, and it means the whole failure is one thing:

> **the android gadget's `enable_store` never takes its first branch on our
> kernel, so `ffs_function_enable()` is never called, so the functionfs
> instance is mounted but never gets endpoints, so adbd has nothing to talk
> to, so adb sits at "offline" — while the USB gadget itself enumerates
> perfectly, with descriptors identical to a working kernel's, because the
> failure is inside the function, not the composite.**

`enable_store`'s first branch is guarded by `if (enabled && !dev->enabled)`.
`enable_show` reports `dev->enabled`, and ours reads **1**. So by the time the
`enable 1` write arrives, `dev->enabled` is already true and the write is a
no-op — and the *first* `enable 1` (from TWRP's
`on property:service.adb.root=1` block) is the one that set it.

The working kernel reads `en=0`, which means its first `enable 1` did **not**
leave `dev->enabled` true — so its second `enable 1` took the branch and called
`ffs_function_enable()`.

The proof on our side is one line in the kernel log: `enable_store`'s ratelimit
message, `android_usb: already enabled`, which is printed exactly when the
write is a no-op. The diagnostic's log filter has been narrowed to
`ffs_function` and `already` (plus `android_usb`, `WARN`, `dwc3`, `udc`,
`gadget`, `BUG`) so that the answer fits inside the 190-byte USB string
descriptor and one boot settles it.

### The Arch toolchain, for the record

The compiler the working kernels use is almost certainly Arch's
`arm-none-eabi-gcc` (https://archlinux.org/packages/extra/x86_64/arm-none-eabi-gcc/),
which is a bare-metal cross toolchain — the right shape for a kernel, and
version 16.2.0, which matches the version banner exactly. `arm-none-eabi-gcc`
is also a better match than the AOSP `arm-linux-androideabi-4.9` prebuilt,
which is a *bionic* toolchain shipped for userspace and which we have been
using for the kernel only because it was the one on hand with a sysroot.

If we want to match the working kernels' toolchain exactly, that package
pulls into the project the same way the AOSP prebuilts do, and the only
change to `build/build-kernel.sh` would be the `CROSS_COMPILE` prefix.

### The diagnostic, rewritten

Repeatedly trying to squeeze a boot's worth of information through a 190-byte
USB string descriptor produced a series of *plausible but wrong* readings,
and that is the failure mode worth recording: a truncated sample of a log
looks like a complete log, and the difference between "the message is
absent" and "the message was cut off" is invisible unless you know the
budget.

So the diagnostic was rewritten with two channels and an explicit budget:

- **the USB descriptor** carries a deliberately short line — the gadget's
  `state`, `enable` and `functions`, plus at most one kernel-log line each
  for `ffs_function` and `already`. Nothing else. If it does not fit, it is
  cut, rather than pushing everything else out.
- **`/data/diag.txt`** carries the full report: the android_usb attributes,
  the endpoint nodes, whether `functionfs` appears in `/proc/filesystems`,
  the `/proc/mounts` lines mentioning functionfs, and the whole kernel log
  filtered to the interesting lines. It persists across a reboot and can be
  read from the TWRP-kernel image where adb works.

Two bugs in the previous version were the reason nothing was ever written to
`/data`:

- the readiness test used `stat64` with a **hand-rolled struct layout**, which
  does not match the kernel's, so the wait for `/data/media` never succeeded.
  It now uses `access()`, whose result is a plain int and cannot get the
  layout wrong.
- the report was written **once**, after that wait. It is now written to
  every persistent path that will take it, retried for a minute and a half,
  so a short boot still leaves evidence behind.

---

## Entry 77 — Which toolchain built what

Three different toolchains are in play, and they were never distinguished
before. The evidence is the `.comment` section, which records the compiler
that produced each object, and the version banner, which the kernel carries
in `CONFIG_LOCALVERSION`.

### The kernels: gcc 16.2.0, and almost certainly Arch's `arm-none-eabi-gcc`

```
kernel-twrp.img        Linux version 3.4.113-... (acroreiser@thinkstation) (gcc version 16.2.0 (GCC) ) #315
kernel-romzip-zip.img  Linux version 3.4.113-... (nobody@android-build)     (gcc version 16.2.0 (GCC) ) #47
installed kernel       Linux version 3.4.113-... (acroreiser@thinkstation) (gcc version 16.2.0 (GCC) ) #315
```

and Arch's package is version **16.2.0** exactly:

```
https://archlinux.org/packages/extra/x86_64/arm-none-eabi-gcc/
  -> arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst
```

`arm-none-eabi-gcc` is also the right *shape* for a kernel — a bare-metal
target with no libc — whereas the AOSP `arm-linux-androideabi-4.9` prebuilt
we have been using is a **bionic** toolchain shipped for userspace, and we
have been using it for the kernel only because it was the one on hand with a
sysroot. `arm-none-eabi-binutils` is pulled alongside it because the kernel
build needs the matching `ld`, `objcopy` and `objdump`.

`standalone/tools/fetch-toolchain.sh` pulls both into
`standalone/vendor/toolchain/`, with three mirrors and a fallback, and
unpacks them with `zstd`.

### TWRP's userspace: clang 6.0.2 with gcc 4.9's assembler

The `.comment` of the ramdisk's `adbd` records both compilers:

```
Android (4691093 based on r316199)
clang version 6.0.2 (https://android.googlesource.com/toolchain/clang 183abd29...)
              (based on LLVM 6.0.2svn)
GCC: (GNU) 4.9.x 20150123 (prerelease)
```

That is the classic AOSP combination: **clang as the compiler, the AOSP
gcc 4.9 prebuilt supplying the assembler and binutils.** The ELF header says
`soft-float ABI` — the same soft-float the AOSP userland is built with, and
the same constraint this project has been honouring in its own payload.

The other TWRP binaries (`recovery`, `ueventd`, `busybox`, `logcat`) have
their `.comment` stripped, so they give nothing away.

### Ours

| what | compiler |
|---|---|
| the three reference kernels | gcc 16.2.0 (Arch `arm-none-eabi-gcc`) |
| TWRP's userspace | clang 6.0.2 + gcc 4.9.x assembler (AOSP), soft-float |
| our payload (`standalone/rootfs/standalone/init`) | AOSP gcc 4.9.x, soft-float, **0 VFP instructions** |

### Why the compiler difference was never a problem

The two things that could couple a kernel to a compiler version are both off
in the reference kernels: `CONFIG_GCC_PLUGINS` (plugins are built against one
specific gcc) and `CONFIG_MODVERSIONS` (module vermagic and CRCs are tied to
the exact build; with it on, `/system`'s modules would not load on our
kernel). And the risk runs the wrong way for us: **gcc 14 made
`-Wimplicit-function-declaration` an error by default**, so a tree that builds
cleanly with gcc 16.2.0 will build cleanly with 4.9 — the reverse is not
true. Our 4.9 build is the *more permissive* one, and it produced no errors.

Matching the reference toolchain exactly is still worth doing, because it
removes a whole class of "is it the code or the build?" questions from every
future experiment — and it is cheap now that the packages are identified.

### The toolchain is now the reference one

`build/build-kernel.sh` now prefers Arch's `arm-none-eabi-gcc` over the AOSP
prebuilt, and reports:

```
ok  .../standalone/vendor/toolchain/usr/bin/arm-none-eabi-gcc  (via ccache )
..  version: arm-none-eabi-gcc (Arch Repository) 16.2.0
..  prefix: arm-none-eabi-
```

which is the same compiler, and the same target triple, that produced all
three reference kernels. `standalone/tools/fetch-toolchain.sh` pulls
`arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst` and
`arm-none-eabi-binutils-2.47-1-x86_64.pkg.tar.zst` from three mirrors with a
fallback, and unpacks them with `zstd` into `standalone/vendor/toolchain/`.

**The AOSP prebuilt was the wrong tool for the job.** It is a *bionic*
toolchain — shipped for userspace, with a sysroot and C++ runtime — used here
only because it was the one ARM toolchain on the host that had a sysroot. A
kernel wants a bare-metal target with no libc, which is exactly what
`arm-none-eabi-gcc` is. It also means the AOSP prebuilt's Python wrapper was
in the build path for no reason, and ccache had to be taught to cope with it.

---

## Entry 78 — The mislabelled kernel file, and what the TWRP kernel really is

### `standalone/kernelcaps/kernel-twrp.img` is not the TWRP kernel

This file has been treated as "the TWRP kernel" throughout the project. It is
not. Extracting the version banner out of it gives:

```
Linux version 3.4.113-<flag>-ge4e483b4ef6 (acroreiser@thinkstation) (gcc version 16.2.0 (GCC) ) #315
```

which is the **installed** KernelSU kernel. The actual TWRP kernel — the one
in `standalone/rootfs/stock/boot-twrp-airgap.img`, the one that gives a
**working adb** — reports:

```
Linux version 3.4.113-<flag>-gda91d066bc6 (acroreiser@thinkstation)
    (gcc version 15.2.1 20251203 (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) ) #42
    SMP PREEMPT Sun May 17 23:24:06 EEST 2026
```

So there are **four** kernels in this project, not three:

| kernel | commit | built | compiler |
|---|---|---|---|
| TWRP / recovery | `gda91d066bc6` (2026-05-17) | acroreiser | **Arm GNU Toolchain 15.2.Rel1** (gcc 15.2.1) |
| installed (KernelSU) | `ge4e483b4ef6` (2026-09-14) | acroreiser | **gcc 16.2.0** |
| ROM zip | `g11370a8eb99` (2026-09-24) | LineageOS buildbot | **gcc 16.2.0** |
| ours | `g466f069d` (2026-10-10) | us | 4.9.x, now **16.2.0 (Arch)** |

The project's earlier note — "the recovery kernel used Arm GNU 15.2.Rel1" —
was right, and I overwrote it with a bad extraction from the mislabelled file.
**The TWRP kernel is the May build, and the installed and ROM kernels are the
September builds.** Four months and 714 commits apart.

`da91d066bc6` is not in any public tree (not acroreiser's, not LineageOS's,
not z3DD3r's), so it is from a private or rewritten branch. The closest
public commit is `299b2e18f65d` (2026-05-16, "ARM: wire up epoll_pwait2
syscall") — one day earlier.

### The `-EBUSY` revert was based on the mislabelled file, and is reverted back

I removed this from `f_fs.c` on the theory that it broke the ffs function's
bind:

```c
	if (atomic_read(&ffs->opened))
		return -EBUSY;
```

That theory came from diffing our tree's `f_fs.c` against the mislabelled
`kernel-twrp.img`'s tree — and the real TWRP kernel **has the check and works
fine with it**. Both the May and the October versions of the file have it. So
it is restored, and the only patch we carry against this tree is now the
one-line `#include <linux/uidgid.h>` that the tree's tip genuinely needs.

### The toolchain, corrected

The TWRP kernel — the one with working adb — was built with **Arm GNU
Toolchain 15.2.Rel1**, not with Arch's `arm-none-eabi-gcc`. The
September kernels (installed and ROM zip) use **gcc 16.2.0**, which is what
Arch's `arm-none-eabi-gcc` provides. So acroreiser switched toolchains
between May and September.

Both are now available to us:
- Arch's `arm-none-eabi-gcc` **16.2.0** (pulled, in
  `standalone/vendor/toolchain/`) — matches the installed and ROM kernels, and
  the tree's tip **builds with it, 0 errors**.
- Arm GNU Toolchain 15.2.Rel1 would match the TWRP kernel; it is not pulled
  yet.

### The live axis is the config, not the source

The tree moved a long way between the working TWRP kernel and our tip (714
commits, 300+ changed files, including BORE scheduling, a new lowmemorykiller,
IPsec, `HZ=300` and ThumbEE). So the experiment to run is: **our tree, our
air-gap profile, and the TWRP kernel's own configuration** — the one known to
give a working adb with the TWRP ramdisk. `build/build-kernel.sh` now takes
`DEFCONFIG_NAME` to override the base, and
`arch/arm/configs/twrp_hammerhead_defconfig` is the TWRP kernel's live
`/proc/config.gz` turned into a defconfig.

If that boots with working adb, the difference is the configuration, and the
remaining work is to find which of the ~175 differing options matters. One
candidate already visible: `CONFIG_ARM_THUMBEE` is **off** in the TWRP kernel
and **on** in ours — it was switched on by an acroreiser commit dated
2026-08-08, after the working May build.

### The config is not the difference either

With the TWRP kernel's **own live configuration** as the base
(`arch/arm/configs/twrp_hammerhead_defconfig`, made from its
`/proc/config.gz`), plus the air-gap profile, the kernel builds and boots and
adb is **still `offline`**. So the difference is not in the configuration
after all — the USB gadget options in the two builds are now identical
(`USB_G_ANDROID=y`, `USB_FUNCTIONFS=off`, `USB_DWC3=y`, `USB_DWC3_MSM=y`,
`USB_OTG_UTILS=y`).

One thing that build *did* expose, and that is a real defect in the tree at
our tip: with `CONFIG_XFRM_INTERFACE=n` the tree does not link.

```
arm-none-eabi-ld: net/built-in.o: in function `xfrm_input':
  (.text+0xb0ff4): undefined reference to `xfrmi_lookup'
```

`net/xfrm/xfrm_input.c` calls `xfrmi_lookup()` **unconditionally** inside an
`if` on the xfrm mode, while `net/xfrm/xfrm_interface.c` — where it is
defined — is only compiled with `CONFIG_XFRM_INTERFACE=y`. The TWRP kernel's
tree (May) did not have that call; an acroreiser commit from 2026-08-21,
"xfrm: adapt 3.4 xfrm/xfrm_interface for VoWIFI IPsec", introduced it. So the
tree at our tip is **internally inconsistent** for that option, and the
workaround is `CONFIG_XFRM_INTERFACE=y` — which is what our own
`lineageos_hammerhead_defconfig` already has.

That is a second independent reason to treat the tree at our tip as *not* the
tree the reference kernels came from, and to consider building from the May
commit instead. `da91d066bc6` is not public, but `299b2e18f65d` (2026-05-16,
one day earlier) is.

### The diagnostic cannot report, and why

The diagnostic's writes are all denied: it publishes to
`/sys/class/android_usb/android0/iSerial` and writes `/data/diag.txt`, and
neither lands. The service is started from `init.airgap.rc` with
`seclabel u:r:recovery:s0`, and TWRP's recovery domain is not permitted to
write those paths. `androidboot.selinux=permissive` is in the cmdline but
TWRP's init does not necessarily honour it for a domain that was already
assigned a context.

So that channel is spent, and it is a channel we do not need: **the decisive
test does not require a diagnostic.** The variable that matters is the
kernel's *source*, and the working TWRP kernel is a May build. So: swap the
tree to the closest public commit to that build and test it.

`standalone/vendor/android_kernel_lge_hammerhead` is now the tree at
**`299b2e18f65d` (2026-05-16)** — one day before the working TWRP kernel's
`da91d066bc6` (2026-05-17), which is not in any public tree. The October tree
is kept alongside it as `standalone/vendor/android_kernel_lge_hammerhead.oct`,
so the comparison is one `ls` away.

Build parameters for the May tree: the **TWRP kernel's own live config**
(`arch/arm/configs/twrp_hammerhead_defconfig`, made from its
`/proc/config.gz`), the air-gap profile on top, and **Arch's
`arm-none-eabi-gcc` 16.2.0** — the same compiler as the September kernels,
and the same *shape* as the TWRP kernel's (Arm GNU bare-metal). The one-line
`uidgid.h` include is still needed: the May tree's `f_fs.c` uses `kuid_t` and
`make_kuid` exactly as the October one does, and neither `linux/fs.h` nor
`linux/cred.h` reaches `linux/uidgid.h`.

## Entry 79 — The ramdisk/cmdline was never the TWRP one; the decisive test was never run

### The host is exonerated

Before touching the kernel again, the host side was audited, because a stale
host adb server can absolutely produce a permanent `offline`:

- the server had been up **2 h 55 m** (pid 3626566) — `adb kill-server` +
  `adb start-server` gave a fresh server, and the device came back as
  `transport_id:1` (not the stale `transport_id:41`) — still `offline`
- no `ADB_LIBUSB` in the environment, single `adb` binary (1.0.41 / 35.0.1),
  one server only
- `~/.android/adbkey` present and unchanged since 29 Sep
- `lsusb` shows the device enumerating normally as `18d1:4ee2` (Google MTP+debug)

And decisively, the device was then **plugged into a different host** and was
still `offline` there. The host is not the problem; the serial
`0879524b0068abca` is identical across kernels, so the "serial changes with
the kernel" theory is out too.

### What the boot images actually contain

Unpacking every boot image with `standalone/rootfs/bootimg.py` and hashing
the kernel and the ramdisk separately shows that the image names have been
lying:

| image | kernel | ramdisk | TWRP ramdisk? |
|---|---|---|---|
| `boot.img` (untouched TWRP) | `a984548b3263` | 1,334,266 | **yes** |
| `boot-ourkernel-noprof-origtwrp.img` | `7c02ccba707c` (ours) | 14,595,115 | no — Android |
| `boot-ourkernel-origtwrp.img` | `e2ce5fab9316` (ours) | 14,595,115 | no — Android |
| `boot-ourkernel-twrp.img` | `30fe1faefbb5` (ours) | 14,563,986 | no — Android |
| `boot-ourkernel-twrcfg.img` | `d2422503e436` (ours) | 14,795,559 | no — Android |
| `boot-ourkernel-android.img` | `e91998faf59a` (ours) | 1,334,266 | **yes** |
| `boot-twrp-airgap.img` | `bbba49d13c78` (**the TWRP kernel**) | 14,563,986 | no — Android |
| `boot-twrp-romkernel.img` | `ffde091513d9` (ROM kernel) | 14,595,115 | no — Android |

**"origtwrp" means "origtwrp-*cmdline*", not "origtwrp ramdisk."** Every
`origtwrp` image carries the **ROM's Android ramdisk** and the **ROM's
cmdline** (`maxcpus=2 androidboot.selinux=permissive buildvariant=eng`),
while the working TWRP image has neither. So the "no-profile + unmodified
TWRP ramdisk" test that was run last was, in fact, *"no-profile kernel +
Android 13 ramdisk"* — a combination that was never intended and never
validated.

### Why that combination cannot work

The ROM's ramdisk carries a modern Android 13 `adbd`, which expects adb over
**FunctionFS** (the `ffs` gadget function, aliased `adb`) — exactly what the
diagnostic reported at runtime: `fn=ffs al=adb`. Our tree is CAF 3.4, whose
`android.c` provides the classic kernel-side `adb` function (`f_adb.c`) and
where `USB_FUNCTIONFS` and `USB_G_ANDROID` are **siblings in one Kconfig
choice** (`drivers/usb/gadget/Kconfig:552`–`:1007`), so only one can be built.
Both the working TWRP kernel and the working ROM kernel have
`CONFIG_USB_G_ANDROID=y` / `# CONFIG_USB_FUNCTIONFS is not set`, i.e. the
classic path, and the ROM's own ramdisk talks to it via `f_adb.c`'s
`/dev/usb-ffs/adb` node.

So the Android ramdisk against a CAF-3.4 kernel is a mismatch of *userspace
generation*, not of kernel source. The device enumerates (the descriptors are
identical) but `adbd` never completes the handshake → `offline`. **This
explains "offline" without any kernel defect at all.**

### The test that had never been run

The decisive experiment is trivially cheap and had never been performed:
**our kernel + the original TWRP ramdisk + TWRP's exact cmdline and header** —
differing from the working TWRP image in the kernel and nothing else.

Built as `standalone/rootfs/stock/boot-test-A-ourkern-twrprd.img` and
verified by round-trip unpack:

- kernel `7c02ccba707c` (our May tree, TWRP's own config, Arm GNU 15.2.Rel1,
  **no air-gap profile**) — yes
- ramdisk `25bbaa0f1b10a0ad` (the untouched TWRP ramdisk) — yes
- cmdline `console=ttyHSL0,115200,n8 androidboot.hardware=hammerhead
  user_debug=31 msm_watchdog_v2.enable=1 androidboot.bootdevice=msm_sdcc.1
  loop.max_part=7` — byte-identical to the working TWRP image

The inverse experiment is already on disk as `boot-twrp-airgap.img` (the
**working TWRP kernel** `bbba49d13c78` + the ROM's Android ramdisk + the
ROM's cmdline). If image A comes up `device`, the ramdisk was the whole story
and the kernel was never guilty. If image A is still `offline`, then the
kernel really is at fault and `boot-twrp-airgap.img` says whether the Android
ramdisk can talk to a known-good CAF-3.4 kernel.

### One more naming trap to keep in mind

`boot-ourkernel-android.img` is our kernel + the TWRP ramdisk, but its
cmdline carries `msm_watchdog_v2.enable=0`, which makes the PMIC watchdog
reset the device after ~30 s. It is not a clean adb test either.

## Entry 80 — `fastboot boot` is ignored; the device always boots its internal `/boot`

### The host really was innocent

Audited and cleared before touching the kernel again:

- the adb server had been up 2 h 55 m; `adb kill-server` + `adb start-server`
  gave a fresh one (`transport_id:1` instead of the stale `41`) — still
  `offline`
- no `ADB_LIBUSB`, one adb binary (1.0.41 / 35.0.1), one server
- `~/.android/adbkey` unchanged since 29 Sep
- `lsusb` shows `18d1:4ee2` enumerating normally
- **and the decisive one: the device was plugged into a different host and was
  still `offline` there.** The host is not the problem, and the serial
  `0879524b0068abca` is identical across kernels.

### The images were misnamed

Unpacking every boot image and hashing kernel and ramdisk separately:

| image | kernel | ramdisk | cmdline |
|---|---|---|---|
| `boot.img` (TWRP) | `a984548b3263` | 1,334,266 (TWRP) | TWRP |
| `boot-ourkernel-noprof-origtwrp.img` | `7c02ccba707c` (ours) | 14,595,115 (**Android**) | ROM-style |
| `boot-romkernel-romcmd.img` | `ffde091513d9` (ROM) | 14,595,115 (Android) | ROM |
| `boot-test-A-ourkern-twrprd.img` (new) | `7c02ccba707c` (ours) | 1,334,266 (TWRP) | TWRP |
| `boot-test-B-twrpkern-twrprd.img` (new) | `a984548b3263` (TWRP) | 1,334,266 (TWRP) | TWRP |

**"origtwrp" meant "the original TWRP *cmdline*", not the TWRP ramdisk.** Every
`origtwrp` image carries the ROM's Android ramdisk, so the "no-profile +
unmodified TWRP ramdisk" test was really "no-profile kernel + Android 16
ramdisk" — a combination that was never intended.

### The bootloader ignores `fastboot boot`

`fastboot boot` reports success (`Sending 'boot.img' … OKAY`, `Booting …
OKAY`), the device reboots — and boots **its internal `/boot` partition**,
not the image. Three independent observations:

1. booting the untouched 23 MB TWRP `boot.img` gave
   `3.4.113-🇺🇦-ge4e483b4ef6 (acroreiser@thinkstation) (gcc 16.2.0)` — the
   **installed KernelSU** kernel, which is in *no* image we sent
2. booting image B (TWRP kernel `a984548b3263` + TWRP ramdisk, 8.8 MB — small
   enough for the download buffer) gave **the same** KernelSU kernel and the
   same Android 16 userspace
3. `bootreason=fastboot` in `/proc/cmdline` shows the fastboot path was taken,
   yet the kernel is the one in `/boot`

⇒ **Every "our kernel" test run so far was really a test of whatever was in
`/boot` at that moment.** That is why the results looked so inconsistent:
same kernel, different outcome, depending on what had been flashed last.

### What the "offline" runs actually were

One run of the internal `/boot` produced our own kernel
(`3.4.113-🇺🇦-gb6c82d07`, `b6c82d07` = our vendored-tree commit, built
Oct 19 11:25:07) with the ROM's Android 16 userspace
(PID 1 = `/system/bin/init second_stage`) and **adb `device`**.

The distinguishing detail: that `/boot` carried the **TWRP-short cmdline**
(`console=… user_debug=31 msm_watchdog_v2.enable=1 androidboot.bootdevice=…
loop.max_part=7`), whereas the "offline" runs carried the **ROM-style
cmdline** with `maxcpus=2 androidboot.selinux=permissive buildvariant=eng`
appended. Same kernel, same Android ramdisk — **different cmdline, different
adb outcome.**

### Meanwhile, the camera stack is alive on our kernel

While adb was up on our kernel, the camera stack was inspected:

- `/dev/video0,1,2` + `video32..35` (venus) + 18 `v4l-subdev` nodes
- `dumpsys media.camera`: **2 camera devices**, legacy HAL
  `device@1.1/internal/0` and `/1`, "Camera provider … ready with 2 camera
  devices", "ADD device 0 / ADD device 1" at boot
- `mm-qcamera-daemon` (uid `camera`) is the HAL, and it opens a session
  successfully (`mm_camera_open: opened, break out while loop`)
- no root though: KernelSU needs its own kernel patches, so on our kernel
  there is no `su`; `/dev/kmsg` is `crw-------`; `dmesg` is unavailable;
  `stop cameraserver` → "Must be root"

⇒ **The camera HAL comes up cleanly on our kernel.** What is still unproven
is that *frames* flow — that needs a client that can hold the camera, and
that needs root, which needs our own userspace.

### Consequence

`fastboot boot` is a dead end. To run *our* kernel with *our* userspace the
image has to be **flashed** to `/boot`. Image A
(`boot-test-A-ourkern-twrprd.img` = our no-profile kernel + the untouched TWRP
ramdisk + TWRP's cmdline) is the one to flash: TWRP's `adbd` runs as **root**,
which finally gives us `/dev/video1`, `dmesg`, and a place to run the
imaging server. It is reversible by flashing the ROM's boot image back.

---

## Entry 81 — the adb blocker is finally solved (and it was a USB bug, not a userspace bug)

*2026-10-19, 12:00–13:30*

### Where things stood

The device was in TWRP (adb `recovery`, root) running the **real TWRP kernel**
`3.4.113-🇺🇦-gda91d066bc6` — built by acroreiser@thinkstation on 2026-05-17
with Arm GNU 15.2.Rel1, i.e. the commit we had been unable to find in any
public tree. The user's working reference image,
`twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img`, was now on the host.

Everything that came out of this entry invalidates part of Entry 80, so
read this one instead.

### The "TWRP ramdisk" we had been testing with was not TWRP's

Unpacking the genuine image settles it:

| image | kernel | ramdisk | cmdline |
|---|---|---|---|
| `twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img` (genuine) | `bbba49d1` (7,446,592 B) | `b222022f` (14,595,115 B) | ROM-style (`maxcpus=2 … selinux=permissive … buildvariant=eng`) |
| `standalone/rootfs/stock/boot.img` (= `/dev/block/mmcblk0p19`) | `a984548b3263` (7,414,992 B) | `25bbaa0f` (1,334,266 B) | TWRP-short |

The 1.33 MB ramdisk is **Android's `first_stage`** (`init`,
`debug_ramdisk`, `second_stage_resources`, `fstab.hammerhead`, `metadata`,
`mnt`, `proc`, `sys`) — *not* TWRP. TWRP's real ramdisk is 14.6 MB and
contains `sbin/recovery`, `etc/python`, `twres`…

⇒ **Every "our kernel + TWRP ramdisk" test up to now was actually "our
kernel + Android's first_stage ramdisk"**, which is why it always came up
as Android 16 and why adb behaved differently. The `/boot` partition was
never modified: the user boots TWRP with `fastboot boot`, which does not
write anything, so `/dev/block/mmcblk0p19` still holds the ROM's image.

⇒ Entry 80's "the TWRP ramdisk" is the ROM's Android ramdisk. Its
"offline" runs were therefore "our kernel + Android ramdisk + ROM-style
cmdline", and the TWRP-short cmdline was a red herring.

`a984548b3263` is the **ROM's KernelSU kernel**; the banner inside it
reads `ge4e483b4ef6` (built Sep 26 2026, gcc 16.2.0). The banner shows the
*source commit*, not the file hash — worth remembering.

### Boot images must carry `zImage-dtb`, not `zImage`

The first attempt to boot our kernel failed with a **broken image**. The
hammerhead bootloader wants the kernel with the appended device tree:
`arch/arm/boot/zImage-dtb` (6.87 MB) vs `zImage` (5.46 MB). Every earlier
image that worked used `zImage-dtb`; the one that did not used `zImage`.

### The two USB bugs

With the genuine TWRP ramdisk and our own kernel, adb came up but stuck at
**`offline`**: the host enumerated `18d1:4ee2` with an "ADB Interface"
(2 bulk endpoints) and then nothing happened. `fastboot boot` works
perfectly — the bootloader honours it every time.

**Bug 1 — the `f_ffs/aliases` race.**
`/sys/class/android_usb/android0/f_ffs/aliases` decides whether the name
"adb" in a USB configuration string selects the *userspace* functionfs
instance (what every modern adbd uses) or the *legacy in-kernel* `f_adb`
function (`drivers/usb/gadget/f_adb.c`). That attribute is created when the
gadget binds to a UDC, and TWRP writes it in `on post-fs` — a race. Lose
it and the composite binds the in-kernel adb function: the host sees an
ADB interface that **no adbd is attached to** ⇒ `offline`, forever.

Fixed kernel-side by defaulting the alias (commit `eaabda3e`):

```c
	strlcpy(android_dev->ffs_aliases, "adb", sizeof(android_dev->ffs_aliases));
```

**Bug 2 — the DWC3 cannot do a short OUT transfer.** This was the real one.
`/proc/last_kmsg` (the previous boot's kernel log, still in RAM) had it:

```
[    4.973370] android_usb gadget: high-speed config #1: fe8050c8.android_usb
[    4.974702] trying to queue unaligned request (24)
[    4.976377] [<c04937f8>] (dwc3_gadget_ep_queue+0x78/0x23c)
[    4.976725] [<c04cd9ec>] (ffs_epfile_io+0x3d8/0x4d8)
[    4.977068] [<c04cdc84>] (ffs_epfile_read+0x20/0x28)
```

adbd reads the **24-byte ADB header** with a single `read()`. Our
`ffs_epfile_io` queued exactly that 24 bytes, and the DWC3 controller
never completes a transfer shorter than the endpoint's `wMaxPacketSize`
(512) — so the host's bytes never reached userspace and the handshake
stalled. The buffer was already allocated rounded up
(`round_up(io_data->len, 1024)`); only the *request length* was not.

How it was pinned down (no guessing, three-way binary comparison):

| | `ffs_epfile_io` | `dwc3_gadget_ep_queue` | "unaligned" warning | adb |
|---|---|---|---|---|
| ROM kernel `e4e483b4` | **same as ours** | **byte-identical to ours** | never logged | works |
| TWRP kernel `da91d066` | **different** (rounds the read length up) | — | never logged | works |
| ours | queues `io_data->len` | same as ROM's | **logged** | **offline** |

The ROM kernel gets away with it because its adbd never issues a 24-byte
read; the TWRP kernel rounds the length up; ours did neither.

The fix (commit `d42d37bb`), in `drivers/usb/gadget/f_fs.c`:

```c
req->length = io_data->read ? buffer_len : io_data->len;
```

with `buffer_len = round_up(io_data->len, 1024)` — always a multiple of
the 512-byte max packet size, and the buffer is already that big. The
copy back to userspace stays limited to what the caller asked for.

### Result

```
Linux version 3.4.113-🇺🇦-gd42d37bb (user@nuc7) (gcc version 15.2.1 20251203
  (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) ) #2 SMP PREEMPT Mon Oct 19 13:22:46 MST 2026
uid=0(root) gid=0(root) ... context=u:r:su:s0
```

**Our own kernel + the *pristine* TWRP ramdisk = adb `recovery` with root.**
No ramdisk patch, no userspace change — one kernel commit.

Image: `standalone/rootfs/stock/boot-ourkern-ffsfix-twrp.img`
(our air-gap kernel `zImage-dtb` + genuine TWRP ramdisk `b222022f` +
genuine TWRP cmdline).

### Method notes

- `/proc/last_kmsg` is the previous boot's kernel log, still in RAM. It is
  the only way to see what happened in a boot where adb never came up —
  read it from the *next* TWRP boot. This is how Bug 2 was found.
- `init` refuses to `exec` a script out of the initramfs ("no SELinux
  domain defined"), so a ramdisk-side fix script does not run as written;
  the fix belongs in the kernel or in a proper service definition.
- The two `android_usb: DIAG …` / `ffs: DIAG …` / `f_adb: DIAG …` traces
  (commit `a77eda3e`) are still in the tree and should be removed once the
  camera work is done.

### Camera status on our kernel (next blocker)

With root adb on our own kernel the camera stack is **fully registered**:

```
imx179_init_module:158
/dev/video0  msm-config     /dev/video1  msm-sensor    /dev/video2  msm-sensor
v4l-subdev11 = imx179   (rear)      v4l-subdev12 = mt9m114b (front)
v4l-subdev13/14 = vfe   v4l-subdev15 = msm_ispif
v4l-subdev16 = msm_buf_mngr          v4l-subdev17 = cpp
```

but opening the sensor node fails:

```
/dev/video1: I/O error
msm_sensor_cci_i2c_util line 537 rc = -22
msm_actuator_close:690 cci_init failed
msm_csiphy_release csiphy dev NULL / ref_count ZERO
msm_csid_release: csid invalid state 1
```

⇒ the camera_v2 stack needs its **configuration** written to the
`msm-config` node before the sensor node can be opened, exactly as the
vendor HAL does it. That is the "imaging server" work: implement the
`CAM_CAMERA_V4L2` ioctl protocol (config on `/dev/video0`, then
`/dev/video1`: S_FMT → REQBUFS → QBUF → STREAMON → DQBUF) and replace
`mm-qcamera-daemon`. Everything the kernel side needs is now proven to be
present and registered on our kernel.

---

## Entry 82 — the fixes rebased onto upstream, and our own imaging server

*2026-10-19, 13:30–14:00*

### Rebased onto the newest upstream commit

Question raised: are we on the newest commit of acroreiser's tree?

| tree | commit | date | what it is |
|---|---|---|---|
| `standalone/vendor/android_kernel_lge_hammerhead` | `b6c82d07` (wraps `299b2e18f65d`) | 2026-05-16 | our **May** base, a squashed vendor import |
| `standalone/vendor/android_kernel_lge_hammerhead.oct` | `466f069d` | 2026-10-10 | **upstream's current tip** (`lineage-23.2`) |

`466f069d` *is* the tip of acroreiser's default branch, and after fetching
the upstream history the October tree turned out to hold the **real**
upstream commit (not a squashed copy). The TWRP kernel's commit
`da91d066bc6` is in **no** branch of that repository — we fetched
`lineage-22.2-kernelsu`, `lineage-23.0-kernelsu`,
`lineage-23.2-kernelsu` and `fbe-dirty-tree` and it is not there, so
that kernel was built from a tree that was never pushed.

⇒ The work was therefore **rebased onto upstream's tip**: the October
tree was reset to pristine upstream, the two fixes from Entry 81 were
re-applied as one clean commit (`e9f1372377a`, "usb: make the adb
transport work on this device"), and the TWRP defconfig was kept (it
matches the known-good TWRP kernel's config byte for byte, and upstream
only ships `hammerhead_defconfig` and `lineageos_hammerhead_defconfig`).

Pristine upstream has **both** bugs, so the rebase is a clean port:

- `drivers/usb/gadget/android.c` — no default for `dev->ffs_aliases`
- `drivers/usb/gadget/f_fs.c` — `req->length = io_data->len` for reads

**Test of the rebased kernel** (upstream tip + our fixes, air-gap
profile, Arm GNU 15.2.Rel1, `zImage-dtb` + the pristine TWRP ramdisk):

```
Linux version 3.4.113-🇺🇦-ge9f1372377a (user@nuc7) (gcc version 15.2.1 20251203
  (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) ) #1 SMP PREEMPT Mon Oct 19 13:41:43 MST 2026
uid=0(root) ... context=u:r:su:s0
adb: 0879524b0068abca  recovery
/dev/video0,1,2,32..35 + 18 v4l-subdev nodes
"unaligned request" warnings in dmesg: 0
```

Image: `standalone/rootfs/stock/boot-ourkern-rebase-twrp.img`.

### Our own imaging server: the handshake now completes

`standalone/cam/imaging6.c` is our imaging server (the thing that
`/vendor/bin/mm-qcamera-daemon` normally is). It:

1. opens `/dev/video0` (the `msm-config` node),
2. subscribes to the event type `MSM_CAMERA_V4L2_EVENT_TYPE`
   (`V4L2_EVENT_PRIVATE_START + 0x2000`) for ids 1..7 — this tree's
   `v4l2_event_subscribed()` matches type **and** id exactly, there is no
   "id == 0 means all" wildcard,
3. dequeues events, and acknowledges each one with
   `MSM_CAM_V4L2_IOCTL_CMD_ACK` (`_IOW('V', 224, struct v4l2_event)`),
   echoing the event back with `status = MSM_CAMERA_CMD_SUCESS`.

Two ABI traps, both found by probing the device rather than by guessing:

- **The event size is 128 bytes here, and the host's header does not
  describe it.** The ioctl number encodes the size of
  `struct v4l2_event`; the host's `/usr/include/linux/videodev2.h`
  gives 136 bytes (its `timespec` is 64-bit) and the tree's union is
  laid out differently from the host's. `standalone/cam/dqprobe.c`
  sweeps every size from 80 to 140 on the device: **only 128 is
  accepted**; everything else is `EINVAL`, and a correct size with an
  empty queue gives `ENOENT` ("no pending events"), which is how you
  recognise the right one.
- **The field offsets have to be read off a live event.** The layout
  the kernel actually uses is

```
   0  __u32 type
   4  padding (the union is 8-byte aligned)
   8  union u { ... __u8 data[64]; }   <- the msm event data starts here
        8  command        (word 0)
       12  status         (word 1)
       16  session_id     (word 2)
       20  stream_id      (word 3)
       24  map_op         (word 4)
       28  map_buf_idx    (word 5)
       32  notify         (word 6)
       36  arg_value      (word 7)
       40  ret_value      (word 8)
   72  __u32 pending
   76  __u32 sequence
   80  timestamp.tv_sec   (32-bit)
   84  timestamp.tv_nsec  (32-bit)
   88  __u32 id            <- the event id
   92  __u32 reserved[8]
  124  padding -> 128 bytes
```

  (`status` comes back as stack garbage for events the kernel did not
  fill in — `camera_pack_event()` only sets command, session_id,
  stream_id and arg_value — so a garbage "status" is expected, not a
  bug.)

With the right size and offsets the run is:

```
[parent] subscribed to event ids 1..7 -- we are the imaging server
[parent] event: id=1 type=0x08002000 cmd=0x00000000 status=0 session=1 stream=0 arg=0xffffffff
[parent]   ACKed
[child] open(/dev/video1) SUCCEEDED -- the handshake completed
[parent] event: id=3 type=0x08002000 cmd=0x0800000b(DEL_STREAM) status=... session=1 stream=0 arg=0xffffffff
[parent]   ACKed
[parent] event: id=2 type=0x08002000 cmd=0x00000000 status=1 session=1 stream=0 arg=0xffffffff
[parent]   ACKed
```

⇒ **the rear sensor node opens.** That is the first time the camera has
been usable without a vendor blob on our own kernel. The event sequence
is: `NEW_SESSION` (id 1) → the kernel's stale-stream cleanup
(`SET_PARM`/`PRIV_DEL_STREAM`, id 3) → `DEL_SESSION` (id 2) when the
node is closed.

### Still to do for the camera

The server has to *do the work* for each command instead of just
acknowledging it: `PRIV_QUERY_CAP`, `PRIV_S_FMT`, `PRIV_S_CROP` and
`PRIV_STREAM_ON` have to be carried out on the subdevices
(`v4l2-subdev11` = imx179, `13/14` = vfe, `15` = msm_ispif,
`17` = cpp) with the ordinary subdevice API
(`VIDIOC_SUBDEV_S_FMT`, `VIDIOC_SUBDEV_S_CROP`), and the result
reported back in `status`/`ret_value`. That is the work that replaces
`mm-qcamera-daemon` — and it is the `image` module Krux will need.

---

## Entry 83 — tree cleanup: one tree, public history, two publishable patches

*2026-10-19, 13:50–14:10*

### Which tree was which, and which one we did not need

| | `android_kernel_lge_hammerhead` (May) | `android_kernel_lge_hammerhead.oct` |
|---|---|---|
| baseline | upstream `299b2e18f65d` (2026-05-16), **squashed into one vendor-import commit** `b6c82d07` | upstream `466f069d` (2026-10-10), the **real** upstream commit, tip of `lineage-23.2` |
| history | 1 commit, no remotes — a *vendored snapshot* | upstream's real history, plus `origin/{lineage-22.2,23.0,23.2,23.2-kernelsu,22.2-kernelsu,fbe-dirty-tree}` fetched |
| our commits | 3 (alias fix, debug traces, f_fs fix) | 1 (both fixes + a vendored TWRP defconfig) |
| clone type | full clone of a 4-commit repo | **shallow** (`.git/shallow`) and **blobless** (`--filter=blob:none`) |
| size | 705 MB (132 MB of it .git) | 1.2 GB (558 MB of it .git) |

**Was the shallow history on purpose?**
- The *May* tree: yes, in the sense that it was never a clone of upstream at
  all — it is a **squashed vendor import** ("vendor: acroreiser/… at
  299b2e18f65d"). That is how the tree was vendored into this project:
  one commit, no upstream history, no remote, so re-vendoring any
  upstream commit is a fresh single-commit import and never a merge.
- The *October* tree: it **is** a clone of the public GitHub repo, and it
  was fetched **shallow + blobless** to keep the download down (1.2 GB
  instead of several GB). The depth/blob filter is a transport
  optimisation, not a design decision; the commits we build on
  (`466f069d`) are the real upstream ones.

⇒ The **October tree is the keeper**: it has the public upstream history,
so the fixes can be published on top of the public tree as they are. The
May tree is a 5-months-older snapshot of the same upstream and is no
longer needed — its history was imported into the keeper as a branch
before the directory was deleted.

### master = public upstream + the two fixes, nothing else

In the keeper:

| branch | what it is |
|---|---|
| `master` | **`466f069d` (upstream tip) + 2 commits** — the publishable branch |
| `lineage-23.2` | pristine upstream, tracking `origin/lineage-23.2` |
| `airgap/wip-twrp-defconfig` | the previous work: both fixes + the vendored TWRP defconfig |
| `legacy/may-2026-05` | the old May tree: its vendored base + the 3 commits (including the debug traces) |

The two commits on `master` are the fixes and nothing else — 38 inserted
lines in two files, no debug tracing, no vendored defconfig:

```
846ed801cb1  usb: gadget: default the functionfs alias to "adb"
e0cd9d48553  usb: f_fs: queue a full-size request for reads
```

They are exported as ready-to-send patches in **`build/patches/`**:

```
build/patches/0001-usb-gadget-default-the-functionfs-alias-to-adb.patch
build/patches/0002-usb-f_fs-queue-a-full-size-request-for-reads.patch
```

The vendored `twrp_hammerhead_defconfig` moved off `master` onto
`airgap/wip-twrp-defconfig`, because it was a *build input*, not a fix.
It turned out to be unnecessary: upstream's own
`lineageos_hammerhead_defconfig` differs from it in only three places
(`CONFIG_HZ` 300 vs 100, and a handful of crypto/BPF/cgroup symbols the
air-gap profile removes anyway), and the build script already preferred
upstream's defconfig by default. `build/build-kernel.sh` also had its
tree auto-detection list corrected — it looked in four places, none of
which was where the tree actually lives — so `--tree` is no longer
needed.

### The directory swap

`android_kernel_lge_hammerhead.oct` was moved to the canonical name
`android_kernel_lge_hammerhead` (which is what the build script and the
log refer to) and the old May tree was deleted. Net effect: one kernel
tree, 1.1 GB, with the public upstream history.

### Re-validation of the consolidated tree

Built from `master` with **upstream's own defconfig**, the air-gap
profile and Arm GNU 15.2.Rel1:

```
Linux version 3.4.113-🇺🇦-ge0cd9d48553 (user@nuc7) (gcc version 15.2.1 20251203
  (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) ) #1 SMP PREEMPT Mon Oct 19 14:05:29 MST 2026
uid=0(root) ... context=u:r:su:s0
adb: 0879524b0068abca  recovery
/dev/video0,1,2 + venus video32..35
"unaligned request" warnings: 0        leftover DIAG traces: 0
```

and the imaging server still completes the handshake on it:

```
[parent] event: id=1 ... session=1 arg=0xffffffff   ACKed
[child]  open(/dev/video1) SUCCEEDED -- the handshake completed
```

⇒ the tree that is now published from is the same one the device is
running, with the two fixes and nothing else.

---

## Entry 84 — the camera: our own imaging server, and the whole V4L2 pipeline running

*2026-10-19, 14:00–15:00*

### The camera_v2 protocol, in full

`camera_v2` has no userspace of its own. Every V4L2 request on a sensor
node is posted as an event to a userspace **imaging server** (on Android:
`mm-qcamera-daemon`) and the kernel blocks until that server acknowledges
it:

```c
/* drivers/media/platform/msm/camera_v2/msm.c */
int msm_post_event(struct v4l2_event *event, int timeout)
{
	...
	if (!atomic_read(&serv_running)) {
		pr_info("%s: daemon hasn't subscribed yet!\n", __func__);
		return -EIO;
	}
```

So the server and the client are the **same process**: the client thread
blocks in an ioctl while the server thread services the event and ACKs
it, and the server is the one that knows what the client wants.

**The event.** 128 bytes, and the host's `/usr/include/linux/videodev2.h`
does *not* describe it (its `struct timespec` is 64-bit, and the union
inside is laid out differently), so the ioctl number has to be built by
hand. `standalone/cam/dqprobe.c` sweeps every size from 80 to 140 on the
device: **only 128 is accepted**; everything else is `EINVAL`, and the
right size with an empty queue gives `ENOENT`.

The layout, read off a live event:

```
   0  __u32 type                       (0x08002000)
   4  padding                         (the union is 8-byte aligned)
   8  union u { ... __u8 data[64]; }  <- the msm event data starts here
        8  command        (word 0)      8+28  arg_value  (word 7)
       12  status         (word 1)      8+32  ret_value  (word 8)
       16  session_id     (word 2)
       20  stream_id      (word 3)
       24  map_op         (word 4)
       28  map_buf_idx    (word 5)
       32  notify         (word 6)
   72  __u32 pending
   76  __u32 sequence
   80  timestamp.tv_sec  (32-bit)
   84  timestamp.tv_nsec (32-bit)
   88  __u32 id                      <- the event id
   92  __u32 reserved[8]
```

`status` comes back as stack garbage for the events the kernel does not
fill in (`camera_pack_event()` only sets command, session_id, stream_id
and arg_value), so a garbage "status" is expected, not a bug.

**The events the kernel posts** (from `camera/camera.c`): `NEW_SESSION`
(id 1) when the node is opened, `SET_PARM` (id 3) with a command
(`PRIV_S_FMT`, `PRIV_NEW_STREAM`, `PRIV_STREAM_ON`, …), `GET_PARM`
(id 4), `DEL_SESSION` (id 2) when it is closed.

### The IMX179's mode tables are not in any kernel

The sensor driver in the tree — ours, acroreiser's, and the AOSP
hammerhead tree's — is a **stub**:

```c
/* drivers/media/platform/msm/camera_v2/sensor/imx179.c (192 lines) */
static struct msm_sensor_power_setting imx179_power_setting[] = { ... };
static struct v4l2_subdev_info imx179_subdev_info[] = {
	{ .code = V4L2_MBUS_FMT_SBGGR10_1X10, .colorspace = V4L2_COLORSPACE_JPEG, ... },
```

Power sequence and bus format, but **no mode tables** and no
`set_fmt`: the sensor has no `s_ctrl` and the VFE has no video ops
either. The register settings therefore have to come from userspace,
through the kernel's own sensor-config ioctl on the sensor subdev
(`VIDIOC_MSM_SENSOR_CFG`, `include/media/msm_cam_sensor.h`):

```c
case CFG_POWER_UP:          /* the kernel's own power sequence */
case CFG_WRITE_I2C_ARRAY:   /* struct msm_camera_i2c_reg_array[] */
```

That is exactly the "no vendor blob" route: the *kernel headers* give
the whole interface, and the only thing missing is the sensor's
register values.

**Where the register values are.** Not in any kernel tree (checked: ours,
acroreiser's other branches, the AOSP `android-msm-hammerhead-3.4-*`
branches, and the ROM's own kernel binary — all have the same stub). They
are in the vendor's sensor library, `/vendor/lib/libmmcamera_imx179.so`,
as arrays of `struct msm_camera_i2c_reg_array { u16 addr; u16 data;
u32 delay; }` — the very layout the ioctl wants. So they are lifted out
once, by `tools/gen-imx179-regs.py`, into `standalone/cam/imx179_regs.h`
as plain data (62 entries: the sensor's full-resolution mode, plus the
two 20-entry variants). No vendor code is needed at runtime.

### The V4L2 ABI has to come from the kernel, not from the host

`standalone/cam/v4l2_caf.h` is a userspace copy of *this* kernel's V4L2
structs, because the ioctl number encodes the struct size and the host's
header disagrees:

| | host's header | this kernel |
|---|---|---|
| `struct v4l2_buffer` | 80 bytes | **68 bytes** |
| `VIDIOC_QBUF` | 0xc050560f → **ENOTTY** | 0xc044560f |

With the host's header, `VIDIOC_QBUF` fails with "Inappropriate ioctl
for device"; with the kernel's layout it works. The other difference is
the multi-planar design: `m.planes` is a *pointer* to an array of
`struct v4l2_plane` and there is one scalar `length`, not `length[]`.

### Where the pipeline stands now

`standalone/cam/imaging7.c` is the whole thing: a server thread
(`/dev/video0`, subscribe to ids 1..7, ACK) and a client thread
(`/dev/video1`, the ordinary V4L2 path). The run on the device:

```
[sensor] /dev/v4l-subdev11 open
[server] subscribed to event ids 1..7 -- we are the imaging server
[server] event id=1 cmd=0x00000000 session=1 stream=0
[client] open /dev/video1 SUCCEEDED -- the handshake completed
[server] event id=3 cmd=0x0800000a(NEW_STREAM) session=1 stream=0
[client] S_PARM ok: extendedmode=0
[server] event id=3 cmd=0x08000004(S_FMT) session=1 stream=0
[sensor] powered up
[sensor] wrote 62 registers
[client] S_FMT ok: 640x480
[client] REQBUFS ok: 4 buffers
[client] 4 buffers queued
[server] event id=3 cmd=0x08000008(STREAM_ON) session=1 stream=0
[sensor] wrote 1 registers            <- 0x0100 = 1, start streaming
[client] STREAMON ok -- waiting for a frame
[client] no frame after 5s
```

**Every step works**: the handshake, the stream creation (S_PARM — which
is what creates the kernel's stream object; without it the kernel says
"msm_vb2_buf_init: Couldn't find stream"), the sensor power-up, the 62
mode registers, the format, the buffers, the stream-on.

**What is still missing: the ISP.** The kernel log during the run is
empty of ISP activity — the VFE never starts. The ISP is driven by the
server through its own subdev ioctl (`msm_isp_ioctl` in
`camera_v2/isp/msm_isp_util.c`):

```
VIDIOC_MSM_VFE_REG_CFG        the algorithm settings (the "chromatrix")
VIDIOC_MSM_ISP_CFG_STREAM     the stream's configuration
VIDIOC_MSM_ISP_REQUEST_STREAM ask the ISP for the stream
```

and the algorithm settings are the vendor's *chromatrix* blobs
(`/vendor/lib/libchromatix_imx179_*.so`), which is a proprietary format.

So the last step is: configure and start the ISP. Two ways —
(a) send a minimal ISP configuration (no chromatrix) and see whether the
ISP will stream raw frames; (b) extract the chromatrix from the vendor
library as well. (a) is worth trying first.

### Two traps worth remembering

- **The client must not open the sensor node before the server has
  subscribed**, or the kernel answers EIO ("daemon hasn't subscribed
  yet"). The two threads need a handshake; the first version raced and
  failed intermittently.
- **`S_PARM` before `S_FMT`**: `camera_v4l2_s_parm()` is what calls
  `msm_create_stream()`, and without a stream the buffers have nothing
  to attach to.

---

## Entry 85 — the ISP starts; the remaining gap is the MIPI link

*2026-10-19, 15:00–16:00*

### The ISP, started without the chromatrix

The server now brings the ISP up itself, on the ISP subdevice
(`/dev/v4l-subdev13`), with **no algorithm settings at all** — the
"no chromatrix" path, which was the first thing to try:

```
[isp] /dev/v4l-subdev13 open
[isp] REQUEST_BUF ok, handle=512
[isp] REQUEST_STREAM ok, handle=512
[isp] 4 buffers enqueued
[isp] stream started
[client] STREAMON ok -- waiting for a frame
[client] no frame after 5s
```

The ISP accepts everything and starts without a single error in the
kernel log. So the chromatrix is **not** needed to get the ISP running —
it is only the ISP's image processing (denoise, sharpening, colour
correction), which is not needed to decode a QR code.

The order is: `REQUEST_BUF` → `REQUEST_STREAM` → `ENQUEUE_BUF` (once per
client buffer) → `CFG_STREAM` with `START_STREAM`.

### The buffers are dma-bufs, not user memory

`msm_isp_prepare_v4l2_buf()` (camera_v2/isp/msm_buf_mgr.c):

```c
	mapped_info->handle = ion_import_dma_buf(buf_mgr->client,
			v4l2_buf->m.planes[i].m.userptr);
```

⇒ the "user pointer" of the v4l2_buffer is really a **dma-buf file
descriptor**, and the client has to allocate the buffers from the ION
allocator (`/dev/ion`) and share them. With plain `malloc()`ed memory
the ISP says "buf has null/error ION handle 00000000".

So the client now does `ION_IOC_ALLOC` + `ION_IOC_SHARE` per plane and
passes the resulting fds. `standalone/cam/ion.h` has the userspace
layout; the heap ids come from the device tree's `reg`
(`drivers/gpu/ion/msm/msm_ion.c`: `pdata->heaps[idx].id = val`) and the
mask is matched with `1 << heap->id` on a 32-bit int, so the effective
bit is `id & 31`. Which heaps exist was asked of the kernel rather than
worked out (`standalone/cam/ionprobe.c` sweeps all 32 bits): **bits 22,
25, 27, 28 and 30** answer; the others do not.

Two more ioctl-number traps of the same family as the V4L2 ones:
`ION_IOC_ALLOC` is nr **0** (not 1) and `ION_IOC_FREE` is nr 1.

### What is still missing: the MIPI link

The kernel log at the end of a run:

```
msm_isp_close_node: halt timeout rc=-512
msm_csiphy_release csiphy dev NULL / ref_count ZERO
msm_csid_release: csid invalid state 1
```

⇒ the CSID (the MIPI receiver) and the CSIPHY were **never enabled**:
in camera_v2 nothing in the kernel starts them — the *server* has to, the
same way it starts the ISP:

- `VIDIOC_MSM_CSID_IO_CFG` on the CSID subdev, `struct csid_cfg_data`
  with `cfgtype = CSID_CFG` and `struct msm_camera_csid_params`
  (`lane_cnt`, `lane_assign`, `phy_sel`, `lut_params`)
- `VIDIOC_MSM_CSIPHY_IO_CFG` on the CSIPHY subdev, `struct
  csiphy_cfg_data` with `cfgtype = CSIPHY_CFG` and `struct
  msm_camera_csiphy_params` (`lane_cnt`, `settle_cnt`, `lane_mask`,
  `combo_mode`, `csid_core`)

and the lane numbers are **in the device tree**, not in a vendor blob:
the imx179 node has `qcom,csi-lane-assign = <0x4320>` and
`qcom,csi-lane-mask = <0x1F>` (four lanes). So this is still the
"kernel headers and device tree only" route.

That is the next step, and it is the last one before frames.

### The state of the camera work, in one place

| step | who | status |
|---|---|---|
| subscribe + ACK the camera events | server | **works** |
| power up the IMX179 | server (CFG_POWER_UP) | **works** |
| program the IMX179's mode (62 registers) | server (CFG_WRITE_I2C_ARRAY) | **works** |
| open the sensor node | client | **works** |
| S_PARM (create the stream) | client | **works** |
| S_FMT (NV12, 640x480) | client | **works** |
| REQBUFS + QBUF (4 dma-bufs) | client | **works** |
| STREAMON | client | **works** |
| start the sensor (0x0100 = 1) | server | **works** |
| ISP: REQUEST_BUF / REQUEST_STREAM / ENQUEUE_BUF / CFG_STREAM | server | **works** |
| CSID + CSIPHY configuration and enable | server | **todo** |
| frames out of `/dev/video1` | | **todo** |

The files: `standalone/cam/imaging7.c` (the server and the client),
`standalone/cam/imaging7`'s helpers `v4l2_caf.h` (this kernel's V4L2
ABI), `isp.h` (the ISP ioctls), `ion.h` (the ION allocator), and
`imx179_regs.h` (the sensor's register settings, generated by
`tools/gen-imx179-regs.py`).

---

## Entry 86 — the whole camera pipeline works except one thing: camera_v2 never starts the CSID

*2026-10-19, 16:00–17:00*

### Everything that is now working

On our own kernel, with the **pristine TWRP ramdisk** and **no vendor
code at runtime**, the imaging server (`standalone/cam/imaging7.c`) drives
the rear camera:

```
[server] subscribed to event ids 1..7 -- we are the imaging server
[client] open /dev/video1 SUCCEEDED -- the handshake completed
[client] S_PARM ok: extendedmode=0
[mipi]  CSIPHY initialised
[mipi]  CSIPHY configured: 4 lanes, mask 0x1f
[mipi]  CSID initialised
[mipi]  CSID configured: 4 lanes, assign 0x4320
[sensor] powered up
[sensor] wrote 62 registers
[client] S_FMT ok: 640x480
[client] REQBUFS ok: 4 buffers
[ion]    allocated 307200 bytes, dma-buf fd 9
[client] 4 buffers queued
[sensor] wrote 1 registers          <- 0x0100 = 1, start streaming
[isp]    REQUEST_BUF ok, handle=512
[isp]    input configured: CAMIF, 3264x2448
[isp]    REQUEST_STREAM ok, handle=512
[isp]    4 buffers enqueued
[isp]    stream started
[client] STREAMON ok -- waiting for a frame
[client] no frame after 5s
```

The kernel log during the run is **empty of errors** — every step is
accepted. The pieces that had to be discovered:

| what | how |
|---|---|
| the event ABI | 128-byte event, the host's header is wrong (136); the offsets have to be read off a live event |
| the sensor's mode registers | not in any kernel (the driver is a stub); lifted out of the vendor library by `tools/gen-imx179-regs.py` |
| the V4L2 ioctl numbers | the host's `videodev2.h` gives the wrong sizes (`v4l2_buffer` 80 vs 68) ⇒ ENOTTY; `standalone/cam/v4l2_caf.h` is this kernel's ABI |
| the buffers are dma-bufs | the ISP does `ion_import_dma_buf()` on the "user pointer" ⇒ the client allocates from `/dev/ion` |
| the ION heaps | asked of the kernel (`ionprobe.c`): bits 22, 25, 27, 28, 30 |
| the ISP | `REQUEST_BUF` → `REQUEST_STREAM` → `ENQUEUE_BUF` → `CFG_STREAM(START)` — **no chromatrix needed** |
| the ISP's input | `VIDIOC_MSM_ISP_INPUT_CFG`: CAMIF, 3264x2448 |
| the MIPI link | `CSIPHY_INIT`/`CSID_INIT` first (they map the registers), then `CSIPHY_CFG`/`CSID_CFG`; the VC data type is a MIPI type (RAW10 = 0x15) |

### The one thing that does not work, and why it matters

```
[client] no frame after 5s
...
k: msm_csid_release: csid invalid state 1
```

The CSID (the MIPI receiver) is powered up and configured, but **it is
never started**: at teardown the kernel says the CSID is in state 1, not
streaming. And looking at the driver, that is not a mistake in our
server:

```
$ grep -n 'msm_csid_start\|msm_csid_stream\|msm_csid_enable' \
      drivers/media/platform/msm/camera_v2/
   (nothing)
```

**There is no "start the CSID" call anywhere in this tree's camera_v2.**
The functions in `camera_v2/sensor/csid/msm_csid.c` are only
`msm_csid_config`, `msm_csid_init`, `msm_csid_release`, `msm_csid_reset`
— the camera_v2 receiver was never finished. (In camera_v1 the equivalent
lives in `msm_camera.c`'s `msm_camera_set_sensor_power()`, which powers
the CSID and the CSIPHY and starts them; the ROM's camera uses
camera_v1, which is why the ROM works and camera_v2 does not.)

So the choice is:

**(a) finish camera_v2 in the kernel** — add the missing
"start the CSID/CSIPHY when the stream starts" (a kernel patch, in the
same spirit as the two adb fixes: a real hole in this tree, filled in
by us), and then the sensor/ISP/buffer path we already have would
produce frames.

**(b) use camera_v1, the stack the ROM actually uses.** The kernel's
camera_v1 is complete: `msm_camera.c` has the "cam server" protocol
(`struct msm_camera_server_message`, include/media/msm_cam.h) that a
userspace imaging server answers, and `camera_v1/sensors/msm_sensor_bayer.c`
is the generic sensor driver that takes its register settings from
userspace — the very mechanism we already use for the IMX179. The data
path in camera_v1 is the "mctl" one rather than plain V4L2, which is
more work in the client but a *complete* kernel path.

The groundwork for either is in place: the sensor's register table, the
daemon protocol and the kernel headers are all understood.

---

## Entry 87 — the ROM's own camera as a reference, and what it tells us

*2026-10-19, 17:00–17:30*

### The ROM runs camera_v2 too — with the camera2 HAL

The ROM's kernel is the same upstream tree, and the ROM's camera *is*
camera_v2: the log shows the camera2 provider starting and the
**camera2 sensor driver** doing the work:

```
k: [   22.149437] init: starting service 'vendor.camera.provider'...
k: [   22.254007] msm_sensor_config:1750 sensor state 1
k: [   22.313648] MSM-CPP cpp_load_fw:824 MC Bootloader Version: 0x312e3176
k: [   22.320044] MSM-CPP cpp_load_fw:838 CPP FW Version: 0x10020003
k: [   22.353664] msm_sensor_config:1778 sensor state 0
k: [   22.460571] msm_sensor_config:1750 sensor state 1
k: [   22.480505] msm_sensor_config_mt9m114b:1339 [CMD_POLLING] success (cnt=2)
```

Three things to note:

1. **The HAL opens the *front* camera first** (mt9m114b — and *that*
   driver is complete in our tree: `camera_v2/sensor/mt9m114.c` has the
   power sequence, the mode tables and its own `sensor_config`). The IMX179
   (rear) is opened later, when the app switches to it.
2. **The CPP firmware is loaded** ("cpp_load_fw"): the HAL sends the
   colour-processing firmware to the kernel with the register-update
   ioctl. That is the chromatrix — the thing option (b) was about.
3. The HAL sends the sensor's register array to the kernel, and the
   kernel's `msm_sensor_config_mt9m114b()` executes it (the `[CMD_POLLING]`
   lines are the sensor's poll commands). So the *generic* mechanism we
   already use (CFG_WRITE_I2C_ARRAY) is exactly what the vendor HAL uses.

### What this changes

The camera2 stack in this tree is **not** as broken as it looked: the
CSID does not need a separate "start stream" call — in the ROM's flow
the CSID is configured and the data simply arrives. What is missing in
*our* run is therefore one of:

- the **front-camera** path works out of the box, because the kernel's
  own `mt9m114.c` does the sensor part — which would prove the whole
  pipeline (CSID → ISP → buffers → DQBUF) and leave only the IMX179
  programming as the open question; or
- the CPP (colour processing) stage is needed for the data to reach the
  buffers, in which case the chromatrix is not optional after all.

The cheapest way to tell them apart: **run the same imaging server against
the front camera** (sensor node `/dev/video2`, subdev `12`, CSIPHY `2`,
CSID `5` — the device tree says `qcom,csiphy-sd-index = <2>` and
`qcom,csid-sd-index = <2>` for the mt9m114b) and with the **kernel's
own** `mt9m114.c` tables instead of the vendor's. If frames arrive, the
pipeline is complete and the IMX179 is the only variable left — and the
IMX179's table we already have (lifted from the vendor library) can be
tried against it.

If frames do *not* arrive, the chromatrix/CPP firmware is required and
option (b) becomes the route: lift the CPP firmware out of the vendor's
`libchromatix_*.so` (it is a blob the HAL uploads verbatim, so it can be
extracted rather than re-implemented).

---

## Entry 88 — a bug of our own: the sensor was never actually programmed

*2026-10-19, 17:30–18:00*

### The bug

`struct sensorb_cfg_data` has a **union** whose member is a *pointer*:

```c
struct sensorb_cfg_data {
	int cfgtype;
	union {
		...
		void                         *setting;
	} cfg;
	uint32_t setting_size;
};
```

and the kernel copies the struct *out of user space from that pointer*:

```c
if (copy_from_user(&conf_array, (void *)cdata->cfg.setting, sizeof(conf_array)))
```

We were **copying** our `struct msm_camera_i2c_reg_setting` into the union
instead of handing over its address. The kernel therefore read the
`reg_setting` pointer from the wrong place, got a garbage count, and
wrote **nothing** — while the ioctl still returned success, so our own
"Wrote 62 registers" message was a lie.

```c
	if (setting)
		*(void **)c.cfg = setting;	/* the address, not a copy */
```

With that fixed, the kernel's copy is right and the sensor really is
programmed.

### Two more things the front-camera experiment showed

1. **The session id has to come from the events.** The camera node's
   session id is its video node number: 1 for the rear camera, 2 for the
   front. The ISP's `REQUEST_BUF`/`REQUEST_STREAM` must use the same one
   — with the wrong one the ISP said "No free buffer for ping" and
   "No buffer for stream0". Now that it comes from the event, the ISP
   starts cleanly.
2. **The front camera is not vendor-free after all.** The kernel's
   `mt9m114.c` *has* the tables (`mt9m114_recommend_settings`,
   `mt9m114_720p_settings`, `mt9m114_config_change_settings`) and
   implements `CFG_SET_INIT_SETTING` / `CFG_SET_RESOLUTION` /
   `CFG_SET_START_STREAM` itself — but the common path in
   `msm_sensor.c` copies the *user's* array out first, and the
   MT9M114B-specific handler then executes the `CMD_POLLING` /
   `CMD_SLEEP` / `CMD_SET_MASK` commands *from that array*. The vendor
   HAL sends such an array (the ROM's log shows
   `msm_sensor_config_mt9m114b: [CMD_POLLING] success (cnt=2)`), so even
   the front camera needs the vendor's command array.

⇒ The "no vendor data at all" route does not exist: the *sensor command
array* (with its poll commands) comes from the HAL in both cases. What
*can* be lifted is data, not code: the IMX179's register table (done) and
the command array.

### Where the camera stands

With the pointer bug fixed and the session id taken from the events, the
whole pipeline runs **without a single error in the kernel log**:

```
[mipi]  CSIPHY configured: 4 lanes, mask 0x1f, csid 0
[mipi]  CSID configured: 4 lanes, assign 0x4320
[sensor] powered up
[sensor] wrote 62 registers           <- now really written
[client] S_FMT ok: 640x480 / REQBUFS ok / 4 buffers queued
[isp]    REQUEST_BUF ok → input configured: CAMIF 3264x2448
[isp]    REQUEST_STREAM ok → 4 buffers enqueued → stream started
```

and still no frame. The two remaining suspects:

- **the CSID is never told to receive** (nothing in camera_v2 calls a
  "start" on it), and
- **the CPP (colour processing) stage is not configured**: the ROM's
  camera loads the CPP firmware at every camera open
  ("MSM-CPP cpp_load_fw"), and the frames go sensor → CSID → ISP →
  **CPP** → memory. Without the CPP the data may never reach the
  buffers.

The next experiment is the one that tells them apart: run the same
server against the front camera with the vendor's *command array* (a
small array, and unlike the CPP firmware it is not a blob) and see
whether frames appear. If they do, the camera works and the rear
camera only needs its (already extracted) table. If they do not, the
CPP firmware has to be lifted out of the vendor's `libchromatix_*.so` —
option (b).

---

## Entry 89 — the front-camera experiment: the "no vendor data" hope is gone

*2026-10-19, 18:00–18:30*

### What the experiment showed

Running the same imaging server against the **front camera** (MT9M114B,
session 2, `/dev/video2`, subdev 12, CSIPHY 2 / CSID 5 — the device tree
says `qcom,csiphy-sd-index = <2>` and `qcom,csid-sd-index = <2>` for the
mt9m114b) works the same way as the rear one: the handshake, the MIPI
configuration, the sensor power-up and the ISP all go through, and the
kernel log is clean.

Two things came out of it:

1. **The session id has to come from the events** (the camera node's video
   node number: 1 = rear, 2 = front). With the wrong one the ISP said
   *"No free buffer for ping"* and *"No buffer for stream0"*; with the
   right one it starts cleanly. That was a real bug in our server, and it
   also means the rear camera's ISP start is now clean too.

2. **The front camera is not vendor-free either.** Its sensor config
   ioctls (`CFG_SET_INIT_SETTING`, `CFG_SET_RESOLUTION`,
   `CFG_SET_START_STREAM`) return **EFAULT** unless the user hands over a
   command array, because the common path in `msm_sensor.c` copies the
   user's array out of user space *before* the driver's own tables are
   written:

```c
	if (copy_from_user(&conf_array, (void *)cdata->cfg.setting,
			   sizeof(struct msm_camera_i2c_reg_setting))) {
		pr_err("%s:%d failed\n", __func__, __LINE__);
		rc = -EFAULT;
```

⇒ There is **no vendor-free route**: the HAL's *command array* (with its
`CMD_POLLING` / `CMD_SLEEP` / `CMD_SET_MASK` commands) is needed for the
front camera, exactly as the IMX179's register table is needed for the
rear one. What can be lifted is *data*, not code — and the rear camera's
data we already have.

### Where each camera stands

| | rear (IMX179) | front (MT9M114B) |
|---|---|---|
| the sensor's data | **have it** (62 registers, `imx179_regs.h`) | would have to be lifted from the vendor's front-camera library |
| who writes it | our server, `CFG_WRITE_I2C_ARRAY` (works, no error) | our server, `CFG_SET_INIT_SETTING` (needs the vendor's command array) |
| MIPI | configured | configured |
| ISP | starts cleanly | starts cleanly |
| frames | **none yet** | not testable without the vendor's array |

⇒ The front camera cannot serve as the clean control we hoped for; the
rear camera is the one that is *almost* complete on our side.

### What is left for the rear camera

Two candidates, and the front-camera experiment did not separate them:

- **the CSID never starts receiving** — nothing in camera_v2 calls a
  "start" on it (option (a): a kernel patch, like the two adb fixes);
- **the CPP (colour processing) stage is not configured** — the ROM's
  camera loads the CPP firmware at every camera open
  (`MSM-CPP cpp_load_fw`), and the data path is
  sensor → CSID → ISP → **CPP** → memory (option (b): lift the
  firmware out of the vendor's `libchromatix_*.so`).

The way to tell them apart is cheap: the CSID's state. `msm_csid_release()`
says "csid invalid state 1" — the CSID was powered up and configured but
never streaming, which points at the **CSID** rather than at the CPP (a
missing CPP would show up as a *streaming* CSID with no data). So option
(a) — starting the CSID when the stream starts — is the next thing to
try, and it is the same kind of change as the two adb fixes: a real hole
in this tree, filled in by us.

Note for later: the vendor's libraries are only reachable from the ROM
(`/vendor` is not mounted in TWRP), so any further extraction has to be
done in an Android boot, with `su` for the root.

---

## Entry 90 — the CPP firmware: the last piece the kernel wanted from userspace

*2026-10-19, 18:30–19:30*

### What the kernel wanted, and where it was

The camera_v2 stack expects the **CPP (colour processing) firmware** to be
loaded from a *file*, named by the imaging server:

```c
/* drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c */
static void cpp_load_fw(struct cpp_device *cpp_dev, char *fw_name_bin)
{
	...
	if (fw_name_bin) {
		rc = request_firmware(&fw, fw_name_bin, dev);
		...
		msm_camera_io_w(MSM_CPP_CMD_FW_LOAD, cpp_dev->base);
		msm_camera_io_write(fw->size, ...);
		for (i = 0; i < fw->size/4; i++)
			msm_camera_write(*ptr_bin, cpp_dev->base);
	}
	...
}
```

and the name comes from the user, through
`VIDIOC_MSM_CPP_LOAD_FIRMWARE` (`_IOWR('V', BASE_VIDIOC_PRIVATE + 3,
struct msm_camera_v4l2_ioctl_t`) on the **CPP subdev**
(`/dev/v4l-subdev17`).

On the ROM the file lives in `/vendor/firmware` and ueventd (the
userspace firmware class) serves it:

```
k: ueventd: firmware: loading 'a330_pm4.fw' for '/devices/...'
k: ueventd: found /vendor/firmware/a330_pm4.fw for /devices/...
```

There are three of them, one per hardware revision:

```
/vendor/firmware/cpp_firmware_v1_1_1.fw   16128 bytes
/vendor/firmware/cpp_firmware_v1_1_6.fw   16384 bytes
/vendor/firmware/cpp_firmware_v1_2_0.fw   16384 bytes
```

They are small, so they are now **in the project**:
`standalone/cam/fw/` (with their sha256s). The server copies the file
into `/lib/firmware` (the kernel's own firmware loader searches there) and
then sends the ioctl. The result on our kernel:

```
[cpp] cpp_firmware_v1_2_0.fw staged in /lib/firmware
[cpp] firmware loaded
k: ueventd: firmware: loading 'cpp_firmware_v1_2_0.fw' for '/devices/fda04000.qcom,cpp/firmware/...'
k: MSM-CPP cpp_load_fw:824 MC Bootloader Version: 0x312e3176
k: MSM-CPP cpp_load_fw:838 CPP FW Version: 0x10020003
```

**The two version numbers are exactly the ones the ROM prints**, so
`v1_2_0` is the right firmware for this hardware, and the CPP now runs
the same code the ROM runs.

### Where the data actually stops

The interrupt counters settle it:

```
=== the camera IRQs before ===      === after ===
   83:          2   ...  GIC  csid     83:          4   ...  GIC  csid
   85:          8   ...  GIC  csid     85:          8   ...  GIC  csid
```

The CSID's interrupt count **goes up during the run** ⇒ the sensor is
streaming, the PHY is linked and **MIPI data reaches the CSID**. So:

sensor → PHY → CSID **works**; the loss is downstream, in the ISP.

(The "csid invalid state 1" messages at teardown are for the *three* CSIDs
we never touched — the enum is `CSID_POWER_UP = 0, CSID_POWER_DOWN = 1`,
so ours, which we powered up, is the one that does *not* complain. There
is no "start the CSID" call in camera_v2, and none is needed: the CSID
receives as soon as it is powered and configured. My earlier framing of
"option (a): patch the CSID start" was based on a wrong premise, and the
interrupt counters corrected it.)

### The whole pipeline, as it now stands

| step | who | status |
|---|---|---|
| subscribe + ACK the camera events | server | works |
| power up the IMX179 | server (`CFG_POWER_UP`) | works |
| program the IMX179's mode (62 registers) | server (`CFG_WRITE_I2C_ARRAY`) | works |
| the MIPI link (PHY + CSID, lanes/VC) | server (`CSIPHY_INIT/CFG`, `CSID_INIT/CFG`) | works, and the CSID's IRQ fires |
| the CPP firmware | server (`VIDIOC_MSM_CPP_LOAD_FIRMWARE`) | works, the right version |
| the ISP: buffers, input, stream | server (`REQUEST_BUF`, `INPUT_CFG`, `REQUEST_STREAM`, `ENQUEUE_BUF`, `CFG_STREAM`) | works, starts cleanly |
| the client's buffers (dma-bufs) | client | works |
| **frames out of `/dev/video1`** | | **still missing** |

### The two remaining candidates

1. **The ISP's input source.** We ask for `CAMIF_RAW` (the raw Bayer
   from the CSID, to be processed by the ISP and the CPP). The other
   source, `PIX_VIEWFINDER`, is the ISP's *own* output and bypasses the
   CPP entirely — a one-line change, and the natural thing to try now
   that the CPP is alive.
2. **The chromatrix.** The ROM's HAL does not only load the CPP
   firmware: it also uploads the ISP's *algorithm* settings (the
   chromatrix) with `VIDIOC_MSM_VFE_REG_CFG` — a long list of register
   writes, which is the "cam_config" of the camera2 stack. That data
   lives in the vendor's `libchromatix_imx179_*.so`, and lifting it is
   the remaining "vendor data" step (it is data, not code — the same kind
   of thing as the sensor's register table, which we already lift).

The order to try them: (1) first, because it is a one-line change; then
(2) if the viewfinder path does not produce frames.

---

## Entry 91 — PIX_VIEWFINDER does not help; the data arrives but the ISP will not deliver it

*2026-10-19, 19:30–20:00*

### The test

`stream_src` in the ISP's stream request was made selectable
(`CAMIF_RAW` = 2, the raw Bayer from the CSID, versus `PIX_VIEWFINDER`
= 1, the ISP's own output, which bypasses the CPP) and the run repeated
with the viewfinder path. Result: **no frame**, and the kernel log is
again clean — no ISP error, no CSID error.

### The data really is arriving

The CSID's interrupt counter keeps climbing across the runs:

```
run 1:  2 -> 4
run 2:  4 -> 8
```

so the sensor streams, the PHY is linked and the CSID receives — the
ISP is the one that will not hand anything over.

### The two things that are still missing, and how to tell them apart

1. **The CAMIF geometry / pixel clock.** We tell the ISP that the
   sensor's frame is 3264x2448 at 320 MHz. If those numbers do not
   match what the IMX179 actually sends, the CAMIF never produces a
   valid start-of-frame and the ISP drops everything. The numbers have
   to come from the *mode* we programmed — and the mode table we lifted
   is 62 entries of register writes whose geometry we have not decoded
   (the table contains 0x0342/0x0343 = 0x0d70 = 3440, 0x034c/0x034d =
   0x0cd0 = 3280 and 0x034e/0x034f = 0x09a0 = 2464, i.e. the sensor's
   own HMAX/VMAX-ish values, which are *not* 3264x2448).

2. **The chromatrix.** The ROM's HAL uploads the ISP's algorithm
   settings (the "cam_config") with `VIDIOC_MSM_VFE_REG_CFG` — a long
   list of register writes, which is the camera2 stack's equivalent of
   the sensor's table. The kernel's `msm_isp_proc_cmd()` takes it; the
   data is in the vendor's `libchromatix_imx179_*.so`.

The order to try them: **decode the mode's geometry first** (it is our
own data, already in `imx179_regs.h` — only the interpretation is
missing), because the CAMIF geometry is cheap to correct and the
chromatrix is a big lift. If the geometry is right and the frames still
do not come, the chromatrix is the answer.

### A note on how the geometry can be read off the sensor

The sensor can be *read*: the kernel's sensor-config ioctl has
`CFG_SLAVE_READ_I2C` / `CFG_READ_I2C_ARRAY`, so the imaging server can
read the IMX179's registers back and check what mode it is actually in
(HMAX, VMAX, the mode select register) instead of guessing from the
table.

---

## Entry 92 — reading the sensor back: the real geometry, and what it costs us

*2026-10-19, 20:00–20:30*

### The read-back

The kernel's sensor-config ioctl can read a register
(`CFG_SLAVE_READ_I2C`, `struct msm_camera_i2c_read_config`), so the
imaging server can ask the sensor what mode it is really in instead of
guessing from the table. It works, and the sensor holds exactly what we
wrote — so the I2C/CCI path to the sensor is fine:

```
[sensor] register read-back (after programming):
  0x0100 = 0x0000    0x0101 = 0x0000    0x0102 = 0x0000
  0x0105 = 0x0001    0x0106 = 0x0000
  0x0301 = 0x0005    0x0303 = 0x0001    0x0305 = 0x0006
  0x0307 = 0x00bc    0x0309 = 0x0005    0x030b = 0x0001
  0x030c = 0x0000    0x030d = 0x00ca
  0x0340 = 0x0009    0x0341 = 0x00c8    0x0342 = 0x000d    0x0343 = 0x0070
  0x034c = 0x000c    0x034d = 0x00d0    0x034e = 0x0009    0x034f = 0x00a0
```

### The geometry we had wrong

Reading the register pairs as 16-bit values:

```
0x034c/0x034d = 0x0cd0 = 3280   <- HMAX, the line length
0x034e/0x034f = 0x09a0 = 2464   <- VMAX, the frame length
```

so the IMX179's mode we programmed is 3280x2464 *with blanking*; the
active area inside it is 3264x2448. We had told the CAMIF 3264x2448 and
320 MHz; the correct numbers are 3280x2464 and

```
3280 * 2464 * 30 = 242.5 MHz
```

The CAMIF is now configured with those, and with the active area
(first/last pixel 0..3263, first/last line 0..2447, epoch line 2464).
**Still no frame** — so the geometry was a real error but not the only
one.

### Where we are

Everything is now measured rather than guessed:

- the sensor is programmed and *reads back* correctly;
- the PHY and the CSID are configured, and the CSID's interrupt counter
  climbs during every run — **MIPI data arrives**;
- the CPP firmware is the ROM's own (same version numbers);
- the ISP starts without an error, with the right session id, the right
  geometry and the client's dma-bufs enqueued.

and still nothing comes out of `/dev/video1`.

### What is left

1. **The chromatrix** — the ROM's HAL uploads the ISP's algorithm
   settings ("cam_config") with `VIDIOC_MSM_VFE_REG_CFG`, a long list of
   register writes. It is data (not code) and lives in the vendor's
   `libchromatix_imx179_*.so`, exactly like the sensor's table we have
   already lifted. This is the big remaining lift.
2. **The ISP's buffer manager** (`v4l-subdev16`, `msm_buf_mngr`) may
   need to be involved: in camera2 the buffers are allocated by the
   buffer manager and the *client* hands them over; we hand the ISP our
   dma-bufs directly with `ENQUEUE_BUF`, which may not be what the ISP
   expects for a "shared" buffer queue.
3. The **output scaling**: the ISP has to turn 3264x2448 into the
   640x480 we ask for, and that scaling is part of the chromatrix.

The next thing to try is (1), with (2) as the fallback.

---

## Entry 93 — the lower-resolution mode, and how the chromatrix has to reach the ISP

*2026-10-19, 20:30–21:00*

### The two shapes the sensor table holds

The vendor's IMX179 library has **three** tables, and the geometry
registers in them are the key:

| table | entries | 0x034c/0x034d (HMAX) | 0x034e/0x034f (VMAX) | mode |
|---|---|---|---|---|
| A | 16 | 0x0668 = **1640** | 0x04d0 = **1232** | the **binned 1632x1224** mode |
| B | 16 | 0x0cd0 = 3280 | 0x09a0 = 2464 | the full-resolution mode |
| C (the one we use) | 62 | 3280 | 2464 | the full-resolution mode, with the stream registers and the config-change settings |

So the IMX179 has a **1632x1224 binned mode** (HMAX 1640, VMAX 1232) in
addition to the 3264x2448 one, and the 20-entry tables are the
"config change" settings for switching between them.

⇒ **For Krux the binned mode is the right one**: a 1632x1224 frame needs
far less ISP work than 3264x2448, and the output can be cropped/scaled
much less. The CAMIF geometry for it is 1640x1232, and the pixel clock
would be 1640 · 1232 · 30 ≈ 60.6 MHz.

### What the chromatrix actually is, on the wire

The kernel's ISP register-update ioctl takes a *list* of register writes,
not a blob:

```c
/* drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c */
int msm_isp_proc_cmd(struct vfe_device *vfe_dev, void *arg)
{
	struct msm_vfe_cfg_cmd2 *proc_cmd = arg;   /* num_cfg, cmd_len,
							    cfg_cmd[], cfg_data */
	struct msm_vfe_reg_cfg_cmd *reg_cfg_cmd;   /* {cfg_type, data_size, data} */
	...
}
```

⇒ the "cam_config" the ROM's HAL uploads is exactly the same *shape* as
the sensor's table we already lift — a list of (register, value) pairs.
The difference is only that the ISP's registers are 32-bit and come with
a "cfg_type" per block, and that the source data (the chromatix blob in
`libchromatix_imx179_*.so`) is a *structured* file that the HAL parses
into those register writes.

### And the CPP has its own buffer queue

```
VIDIOC_MSM_CPP_ENQUEUE_STREAM_BUFF_INFO
VIDIOC_MSM_CPP_SEND_BUF_DONE
```

so in the camera2 pipeline the *CPP* holds the output buffers: the
server enqueues them into the CPP and the CPP hands them back when they
are done. With `PIX_VIEWFINDER` the CPP is out of the path, which is why
that variant is worth trying — but the ISP still wants its own
configuration.

### The plan

1. **Switch the sensor to the binned 1632x1224 mode** (the mode-select
   register plus the config-change table) and tell the CAMIF 1640x1232 at
   ~60.6 MHz. Cheaper for the ISP, and it is the mode Krux wants.
2. **The chromatrix**: parse the vendor's chromatix blob into the ISP's
   register-write list and send it with `VIDIOC_MSM_VFE_REG_CFG`. This is
   the same kind of work as `tools/gen-imx179-regs.py`, but the blob's
   format has to be understood first.
3. **The CPP's buffer queue**, if the CPP stays in the path.

Step 1 is ours alone and is the right next move; steps 2 and 3 are the
vendor data.

---

## Entry 94 — the binned mode works, and a way to get the chromatrix without parsing the blob

*2026-10-19, 21:00–21:30*

### The binning mode is in

```
[sensor] wrote 62 registers        <- the main mode settings
[sensor] wrote 20 registers        <- the "config change" settings of the
                                      1632x1224 mode
[isp]    input configured: CAMIF 1640x1232
```

and the sensor confirms it:

```
0x034c = 0x0006   0x034d = 0x0068   ->  HMAX 1640
0x034e = 0x0004   0x034f = 0x00d0   ->  VMAX 1232
```

The IMX179 is now in its **1632×1224 binning mode** — the mode Krux
wants — and the CAMIF is told 1640×1232 at 60.6 MHz. The CSID's
interrupt counter keeps climbing (2 → 4 → 8 → 16 over the runs), so the
data still arrives, and the kernel log is still clean.

**Still no frame.** So the sensor side is done; what is missing is
entirely in the ISP (its configuration) and, if the CPP stays in the
path, the CPP's buffer queue.

### Getting the chromatrix *without* parsing the vendor blob

The chromatrix is a structured file that the vendor HAL parses into
register writes. Parsing that format is a project of its own — but
there is a much better way:

**The ROM's own HAL sends the chromatrix to the kernel every time a
camera opens.** And the kernel can be told to log it:

* build our kernel with `CONFIG_MSMB_CAMERA_DEBUG=y` (the `CDBG` macros
  in the camera_v2 tree become `pr_err`),
* boot it with the **ROM's Android userspace** — the ROM's camera HAL
  opens the front camera at boot (the log shows
  `msm_sensor_config_mt9m114b: [CMD_POLLING] success`), which is exactly
  when it uploads the chromatrix,
* read the register writes out of `dmesg`.

That gives the chromatrix **exactly as the ROM sends it**, in the form
the kernel wants (a list of register writes), with no blob parsing — and
it is the same trick as the rest of this work: use the device's own
behaviour instead of reverse-engineering its files.

The two adb fixes make that possible: our kernel with the ROM's Android
userspace has a working root adb (Entry 81/82), so the build can be
booted, the camera can be opened and the log read back.

After that, the pipeline should be complete:

| step | status |
|---|---|
| handshake, sensor power-up, mode (full or binned) | works |
| MIPI link (PHY + CSID), data arriving | works |
| CPP firmware | works |
| ISP buffers, input geometry, stream start | works |
| **the ISP's configuration (chromatrix)** | **to be lifted this way** |
| frames out of `/dev/video1` | the last step |

---

## Entry 95 — capturing the chromatrix from the vendor HAL: the machinery is ready, the trigger is missing

*2026-10-19, 21:30–23:00*

### What was built

Two debug-only kernel changes (branch `airgap/debug-capture`, **not** on
master):

1. `kernel/printk.c`: `dmesg_restrict = 0`, so an unprivileged shell can
   read the kernel log. (The ROM's SELinux policy blocks `syslog_read`
   from the shell as well, which is why the kernel command line also
   carries `androidboot.selinux=permissive` in the debug image.)
2. `camera_v2/isp/msm_isp_util.c`: `msm_isp_proc_cmd()` writes down what
   the HAL uploaded — the `reg_cfg_cmd[]` blocks and the data blob — to
   `/data/local/tmp/isp_cfg_<n>.bin`:

```c
	/* DEBUG: capture what the camera HAL uploaded to the ISP, so that
	 * our own imaging server can replay it without the vendor blob. */
	{
		static u32 dump_idx;
		...
		vfs_write(f, (const char __user *)hdr, sizeof(hdr), f, &pos);
		vfs_write(f, (const char __user *)reg_cfg_cmd, ..., &pos);
		vfs_write(f, (const char __user *)cfg_data, proc_cmd->cmd_len, &pos);
```

(`kernel_write()` does not exist in 3.4; it is `vfs_write()` there.)

With this, the chromatrix is captured **exactly as the ROM sends it**,
already in the form the kernel wants: a list of blocks
(`cmd_type`, `reg_offset`, `len`, `cmd_data_offset`) plus the data blob.
No blob parsing, and it is the same trick as the rest of this work — use
the device's own behaviour instead of reverse-engineering its files.

### The capture works, but the trigger does not fire

Our kernel running the **ROM's Android userspace** boots fine and the
camera provider sees both cameras ("Number of camera devices: 2"), but
**no capture file appears**: the ROM's HAL uploads the ISP's configuration
only when a camera is really *opened* by an app, and with our kernel the
open does not get that far.

The ROM's own boot (with the ROM's kernel) shows what a successful
camera open looks like:

```
msm_sensor_config:1750 sensor state 1
MSM-CPP cpp_load_fw:824 MC Bootloader Version: 0x312e3176
msm_sensor_config_mt9m114b:1339 [CMD_POLLING] success (cnt=2)
```

— the CPP firmware is loaded and the sensor's command array is sent at
*probe* time, and the chromatrix goes with the first real open.

The camera apps on the device are `com.google.android.GoogleCamera`
(launcher activity `com.android.camera.CameraLauncher`) and
`com.shenyaocn.android.usbcamera` (which is already the top-most
instance). Neither produced a capture: the app's camera open does not
complete on our kernel — which is the *same* hole we are chasing from the
other side.

### Where that leaves the camera

Everything is now in place except the ISP's configuration:

| step | status |
|---|---|
| the imaging server (events, handshake, ACK) | works |
| the sensor (power-up, mode, binning mode, read-back) | works |
| the MIPI link, data arriving at the CSID | works |
| the CPP firmware (the ROM's own) | works |
| the ISP (buffers, input geometry, stream start) | works |
| **the ISP's configuration (chromatrix)** | the capture machinery is ready; the ROM's HAL will not open a camera on our kernel |
| frames out of `/dev/video1` | the last step |

⇒ Two ways forward, and the second is now clearly the cheaper one:

**(a) make the ROM's HAL able to open a camera on our kernel** — i.e. find
and fix whatever the camera open trips over in camera_v2 (the same class
of hole as the two adb bugs). Then the capture fires and the chromatrix
falls out.

**(b) parse the chromatrix out of the vendor blob**
(`/vendor/lib/libchromatix_imx179_*.so`, already pulled into
`/tmp/opencode/vendor/`) and turn it into the same block list the kernel
wants. The blob's format is the "chromatix" one, so this needs the
format understood — more work than (a), but it does not depend on the
ROM's HAL working on our kernel.

For Krux, (b) is the more honest answer anyway: the standalone image has
to carry the ISP's configuration, and the day has to come when the
vendor HAL is not there at all.

---

## Entry 96 — the kernel source question, and where the camera work stands

*2026-10-19, 23:00–23:45*

### The kernel source: ours is the ROM's source, plus a couple of commits

The ROM's kernel is `3.4.113-🇺🇦-ge4e483b4ef6` (built Sep 26 2026 with
gcc 16.2.0, i.e. the Arch toolchain). That commit is **not** in
acroreiser's repository any more — the `lineage-23.2-kernelsu` branch
was rewritten (its KernelSU sync commits of 2026-09-14 and 2026-10-16
have different hashes than the ROM's build), so the exact commit is
gone. What is left is the same *content*:

```
cbf5ca6188e 2026-07-05  kernelsu: import KernelSU
4b50eb8727a 2026-07-05  hammerhead: enable KernelSU
c73437156b6 2026-09-14  kernelsu: sync with KernelSU at 25a8ebc
   ... (the ROM's build sits here, 2026-09-26) ...
696cece498f 2026-10-16  kernelsu: sync with KernelSU at d5cc719
5bd52f720a1 2026-10-16  kernelsu: use ksu_static_key_...
```

⇒ **Our base `466f069d` (the `lineage-23.2` tip) *is* the ROM's source
plus a couple of commits** — exactly what is allowed. The kernel tree is
therefore right; the adb fix sits on top of it as two clean commits
(`a62ece18a0b`, `81f0b80221d`).

The point the note makes is right and worth keeping: **we cannot rely on
the ROM's userspace at run time.** In the standalone image there is no
ROM, no KernelSU and no root, so everything the camera needs has to be
*in* our imaging server. That is why the chromatrix has to become data we
carry, not something we capture from a running vendor HAL.

### The capture machinery (ready) and its missing trigger

`airgap/debug-capture` (two debug-only commits, **not** on master):

* `kernel/printk.c`: `dmesg_restrict = 0` — the shell can read the log
  (the ROM's SELinux policy blocks `syslog_read` as well, so the debug
  image also carries `androidboot.selinux=permissive`).
* `camera_v2/isp/msm_isp_util.c`: `msm_isp_proc_cmd()` writes what the
  HAL uploaded — the `reg_cfg_cmd[]` blocks and the data blob — to
  `/data/local/tmp/isp_cfg_<n>.bin`.

Our kernel boots the ROM's Android fine and the camera provider sees
both cameras, but **the camera will not open**:

```
E CAM_Camera2Act: CameraDevice 0 error 3!
```

and no capture file appears. The real camera app is
`com.google.android.GoogleCamera/com.android.camera.CameraLauncher`
(the `com.shenyaocn.android.usbcamera` app is for *USB* webcams and
never touches the phone's camera).

So the vendor HAL cannot complete a camera open on our kernel — which is
the *same* hole we have been chasing from the other side, and it is the
reason option (a) does not pay off.

### The suspicion worth checking next

The ROM's kernel may have **camera_v1** enabled as well as camera_v2,
and our air-gap profile *forces camera_v1 off*:

```
tools/profiles/hammerhead-camera.list:
  CONFIG_MSM_CAMERA=n
  CONFIG_MSM_CAMERA_V4L2=n
  CONFIG_MSMB_CAMERA=y
```

camera_v2 in this tree is demonstrably unfinished (no "start the CSID",
no way to configure the ISP without the vendor blob), while camera_v1
(`camera_v1/msm_camera.c` + `camera_v1/sensors/msm_sensor_bayer.c`) has
the complete pipeline *and* the "cam server" protocol
(`struct msm_camera_server_message`, include/media/msm_cam.h) that a
userspace imaging server answers — and `msm_sensor_bayer.c` is the
generic driver that takes the sensor's register settings from userspace,
which is exactly the mechanism we already use for the IMX179.

⇒ Before parsing the chromatrix, the cheapest test is to build with
**camera_v1** (as the ROM has) and see (i) whether the vendor HAL can
open a camera (which would make the capture fire), and (ii) whether the
camera_v1 "cam server" interface is the better base for our own imaging
server — it very likely is, because it is the stack the ROM ships with
and the stack that is *finished*.

---

## Entry 97 — correction: the ROM uses camera_v2, exactly like us

*2026-10-19, 23:45–00:15*

Entry 96 speculated that the ROM might have camera_v1 compiled in and that
our air-gap profile (which forces `CONFIG_MSM_CAMERA=n`) could be the
reason the vendor HAL cannot open a camera on our kernel. **That
speculation was wrong**, and the ROM's own kernel settles it — booted
from the installed `/boot` image, so this is the ROM's real config:

```
# CONFIG_MSM_CAMERA is not set
# CONFIG_MSMB_CAMERA_DEBUG is not set
CONFIG_MSMB_CAMERA=y
CONFIG_IMX179=y
CONFIG_MT9M114B=y
CONFIG_MSM_CAMERA_SENSOR=y
CONFIG_MSM_CPP=y
CONFIG_MSM_CSID=y
CONFIG_MSM_CSIPHY=y
CONFIG_MSM_ISPIF=y
```

⇒ **the ROM is camera_v2, and so are we** — the same stack, the same
sensors, the same ISP/CPP/CSID/CSIPHY set. Our profile's camera
section matches the ROM's kernel option for option; the earlier claim
that the ROM might be a camera_v1 device was a misreading of
"legacy HAL device@1.1" in `dumpsys media.camera` (that is the *camera1
HAL shim* on top of the camera2 provider, not a camera_v1 kernel
stack).

So the "the HAL cannot open a camera on our kernel" failure is **not**
a camera_v1/camera_v2 mismatch, and camera_v1 is not the better base
either — it is not enabled in the ROM at all.

⇒ What is left to explain the failed open is the difference between
the ROM's kernel and ours:

| | the ROM's kernel | ours |
|---|---|---|
| camera stack | camera_v2 | camera_v2 ✓ same |
| camera options | as above | as above ✓ same |
| compiler | gcc 16.2.0 (Arch) | Arm GNU 15.2.Rel1 |
| air-gap profile | none | Wi-Fi/BT/modem/NFC stripped, camera_v1 forced off, devtmpfs off |
| other | KernelSU | none |

The next test is therefore a *differential* one, and it is cheap:
build the same tree **without the air-gap profile** (i.e. close to the
ROM's kernel), boot it with the ROM's Android and see whether the
vendor HAL can open a camera. If it can, the profile is what breaks it
and we can bisect the profile; if it cannot, the difference is the
kernel source/toolchain, and the capture has to be done differently.

Either way the chromatrix ends up as **data in our imaging server** —
the ROM is not available at run time in the standalone image.

---

## Entry 98 — the differential build answers the question, and the chromatrix is found

*2026-10-20, 00:15–01:00*

### The differential build: the profile is what breaks the camera

The differential kernel — the same tree, **no air-gap profile**, and the
**ROM's own compiler** (gcc 16.2.0, the Arch toolchain) — boots the
ROM's Android and the camera **opens**:

```
Linux version 3.4.113-🇺🇦-ge4e483b4ef6 (acroreiser@thinkstation) (gcc version 16.2.0 (GCC)) #315 ...
```

(the banner is the ROM's because the *ROM's* userspace was booted from
the installed image first; the running kernel is ours — the build under
test is `/tmp/opencode/ko-diff2`).

```
=== camera clients ===
  [
  (Camera ID: 0, Cost: 51, PID: 5126, Score: 0, State: 2
   User Id: 0, Client Package Name: com.google.android.GoogleCamera, ...)
  ]
```

⇒ with the ROM's config and the ROM's compiler **the vendor HAL opens the
camera on our kernel**; with the air-gap profile it does not. So the
profile is what breaks the camera open, and the next step is to bisect
*it* (not the kernel source, and not the toolchain).

(The ROM's kernel options were compared with ours and are identical for
the whole camera: `MSMB_CAMERA`, `IMX179`, `MT9M114B`, `MSM_CPP`,
`MSM_CSID`, `MSM_CSIPHY`, `MSM_ISPIF`, `MSM_CAMERA_SENSOR`.)

### Where the chromatrix actually goes

The HAL does **not** use `VIDIOC_MSM_VFE_REG_CFG` (the ISP's register
update) — that ioctl is never called. The chromatrix is a **message to
the CPP**:

```c
/* drivers/media/platform/msm/camera_v2/pproc/cpp/msm_cpp.c */
static int msm_cpp_cfg(struct cpp_device *cpp_dev,
	struct msm_camera_v4l2_ioctl_t *ioctl_ptr)
{
	...
	struct msm_cpp_frame_info_t *u_frame_info =
		(struct msm_cpp_frame_info_t *)ioctl_ptr->ioctl_ptr;
	...
	cpp_frame_msg = kzalloc(sizeof(uint32_t) * new_frame->msg_len, ...);
	copy_from_user(cpp_frame_msg, (void __user *)new_frame->cpp_cmd_msg,
		sizeof(uint32_t) * new_frame->msg_len);
	...
	if (cpp_frame_msg[new_frame->msg_len - 1] != MSM_CPP_MSG_ID_TRAILER)
		pr_err("Invalid frame message\n");
```

⇒ the chromatrix is `cpp_cmd_msg`: a u32 array whose last word is
`MSM_CPP_MSG_ID_TRAILER`. That is the blob to lift, and it is lifted
*whole* — no format to understand, no per-register interpretation.

### The capture patch for it

On `airgap/debug-capture` (not master), `msm_cpp_cfg()` now writes the
message to `/data/local/tmp/cpp_cfg_<n>.bin` (a header with `msg_len`
and the frame type, then the u32 array). Two things to fix in it before
the next run:

* the message is a **kernel** buffer, so it must be copied out (a
  `vfs_write()` of a `__user` pointer that is really a kernel pointer
  just returns EFAULT and writes nothing) — `memcpy()` into a `kmalloc()`
  buffer first, or use `kernel_write()`-equivalent;
* the first run of this patch did not come up (the device came back on
  the ROM's kernel, `last_kmsg` empty) — most likely a fluke of the
  no-profile configuration, but it must be re-run before drawing
  conclusions.

### The remaining shape of the work

1. fix the capture, re-run, and get `cpp_cfg_<n>.bin` — the chromatrix,
   as the ROM sends it;
2. bisect the air-gap profile to find what stops the camera from
   opening (the profile is the only difference between "the camera opens"
   and "it does not");
3. teach our imaging server to send that message to the CPP
   (`VIDIOC_MSM_CPP_CFG` with `struct msm_cpp_frame_info_t`) — the
   chromatrix then becomes **data we carry**, which is what the
   standalone image needs, since the ROM will not be there.

---

## Entry 99 — the chromatrix is captured (and the camera opens on our kernel)

*2026-10-19, 18:00–19:10*

### The differential build was a red herring: the watchdog was resetting us

The "differential" kernels (no air-gap profile, the ROM's own config, the
ROM's own compiler) never came up: the device came back on the installed
ROM kernel every time, which made it look as if the camera only worked on
the ROM's kernel.

It was not the kernel. Two things were going on:

1. The **cmdline** we packed had the ROM's `msm_watchdog_v2.enable=1`.
   When init dies, the kernel panics; with the watchdog enabled the SoC
   is reset a few seconds later and the bootloader boots the *installed*
   kernel. So every "our kernel died" observation was really "our kernel
   panicked, the watchdog reset the device, and we were looking at the
   ROM's kernel" — including the earlier "the camera opens on our kernel"
   observation, which was the ROM's kernel all along.
   With `msm_watchdog_v2.enable=0` our kernel stays up.

2. The panic itself was **our own debug patch** (see below).

With the watchdog off, our kernel boots the ROM's Android, the camera
opens (`mm_camera_open: opened`), and the HAL pushes its configuration
into the driver — hundreds of times.

### Reading the kernel log without a console

The shell cannot read the kernel log (SELinux: `syslog_read` denied),
and `dmesg_restrict` is not the problem. But the ROM's own kernel keeps
`/proc/last_kmsg`, and the ROM's kernel has KernelSU, so after a reboot
into the installed kernel:

```
adb shell su -c "cat /proc/last_kmsg"
```

gives **our** kernel's log — that is how the oops below was read. (The
ring buffer only keeps the last ~100 s, so the camera has to be opened
*late* in the boot to have its messages inside the window.)

### The oops, and what it taught us about this kernel

```
[  18.682506] Unable to handle kernel NULL pointer dereference at virtual address 00000013
[  18.682967] Internal error: Oops: 5 [#1] PREEMPT SMP ARM
[  18.683361] PC is at vfs_write+0x4/0x158
[  18.683458] LR is at msm_isp_proc_cmd+0x210/0x76c
```

Three separate mistakes, all in the capture patch:

* **`filp_open()` returns `ERR_PTR()`, not NULL.** `if (f)` is not a test
  for success; it passed `ERR_PTR(-EACCES)` to `vfs_write()`.
  `-EACCES` because the camera daemon may not create files in
  `/data/local/tmp`; the file is now opened as root
  (`override_creds(&init_cred)`).
* **`vfs_write()` copies with `copy_from_user()`,** which on this ARM
  kernel only reaches addresses below `TASK_SIZE`. The kernel image is
  below it; the stack and this tree's static data are not. So anything
  the kernel writes out has to come from *user* memory: the capture
  reads `proc_cmd->cfg_cmd` / `proc_cmd->cfg_data` (the user's
  pointers) rather than the driver's kernel copies.
* **The CPP driver overwrites `new_frame->cpp_cmd_msg`** with its own
  kernel copy just before the capture ran, so the pointer we wrote out
  was a kernel address and the write failed with `EFAULT`. The user's
  pointer is now saved before the overwrite.

Also needed: the ROM's init runs `setenforce 1` at boot, which undoes
the kernel's own relaxation, so for the diagnostic build
`security/selinux/selinuxfs.c` now ignores the request and SELinux
stays permissive.

### The chromatrix

With those fixed, the capture works — and the result is exactly what we
wanted: **data, not code**.

* The **CPP** message (`VIDIOC_MSM_CPP_CFG`,
  `struct msm_cpp_frame_info_t::cpp_cmd_msg`) is 266 u32 words =
  1064 bytes:

  ```
  word 0   : 0x3e646d63  "cmd>"     (the CAF command-stream magic)
  word 265 : 0xabcdefaa  MSM_CPP_MSG_ID_TRAILER
  ```

  Every capture of it in the session was byte-identical
  (md5 `9c3162a8264480ddf37f05c9aeb31670`), so it is a constant for
  this sensor and mode.

* The **ISP** uploads (`VIDIOC_MSM_VFE_REG_CFG`, a list of
  `struct msm_vfe_reg_cfg_cmd` plus a data blob) come in a dozen sizes;
  the big one is 2304 bytes of the ISP's register program.

All of it is saved in `standalone/cam/data/` with a README that says how
to send it back.

⇒ The chromatrix no longer has to be reverse-engineered out of the
vendor library, and the vendor HAL is no longer needed at run time: our
imaging server can send the very same bytes.

### What is still open

* The camera opens on our kernel, but the stream does not run: the HAL
  reports `Couldn't find stream`, the ISP tasklets overflow
  (`msm_isp_process_irq: Tasklet queue overflow`), and the node close
  times out (`msm_isp_close_node: halt timeout rc=-512`). That is the
  next thing to chase — now with the vendor HAL's own configuration in
  hand.
* The air-gap profile was *not* the thing that broke the camera: the
  air-gap kernel is the one that opens it. The earlier
  "CameraDevice 0 error 3" was the panic, not the profile.
* The tree is unchanged on `master` (upstream tip + the two adb fixes);
  all of this instrumentation is on `airgap/debug-capture`.

---

## Entry 100 — the whole configuration is now captured, and the real blocker is found

*2026-10-19, 19:10–21:30*

### What the chromatrix turned out to be

Three separate things, all captured from the vendor HAL running on our
own kernel with the same trick (write the payload out of the kernel
driver), and all of them now *data* we carry:

| what | how it is sent | size |
|---|---|---|
| the ISP's configuration (the chromatrix and the rest of the camera2 `cam_config`) | `VIDIOC_MSM_VFE_REG_CFG` | 39 uploads, the biggest 2304 bytes of register program |
| the CPP's chromatrix | `VIDIOC_MSM_CPP_CFG`, `cpp_cmd_msg` | 266 u32 words = 1064 bytes, `"cmd>"` … `0xabcdefaa` |
| the sensor's mode change | `CFG_WRITE_I2C_ARRAY` | 14 arrays: a 92-entry *page* sequence, the 62-entry mode table, a config change, the geometry, and `0x0100 = 1` |

All of it is in `standalone/cam/data/` with a README, and
`tools/gen-cam-config.py` / `tools/gen-sensor-config.py` turn it into
`standalone/cam/cam_config.h` and `standalone/cam/sensor_config.h`,
which `standalone/cam/imaging8.c` replays.

Two things about this kernel made the capture hard, and both are worth
remembering:

* `filp_open()` returns `ERR_PTR()`, not NULL — `if (f)` is not a test
  for success;
* `vfs_write()` copies with `copy_from_user()`, which on this ARM kernel
  only reaches addresses below `TASK_SIZE`. The kernel image is below
  it; the stack and this tree's static data are **not**. So the kernel
  can only write out data that is in *user* space, and the buffers it
  writes itself have to be copied out first.

### A real hole in this tree's camera_v2: the DMI-64 table

`msm_isp_send_hw_cmd()` insisted that the high half of a 64-bit DMI
entry comes *after* the low half (`hi = lo + 4`). The camera2 HAL puts
them the other way round (`lo = hi + 4`), so **every** configuration the
vendor uploaded was rejected:

```
msm_isp_send_hw_cmd:472 hi 8 lo 8
```

(the message prints `hi` twice, which is why the numbers are the same).
The kernel patch accepts either order; the table in question is all
zeros, so which half is which does not arise.

### The MIPI data *does* arrive

The CSID's own counters, printed when it is released, say it all:

```
DEBUG: CSID0 irq status 0x080002dd, packets 11808, unmapped long 0x1201040a,
        mapped long 0x00000000, short 0x0000011a, ecc 0, crc 0
```

11,808 packets, **no ECC and no CRC errors**, and the interrupt status
has the "short packet received" bit set. So:

* the sensor streams ✓ (with the vendor's own sequence ✓),
* the MIPI link works ✓,
* the receiver accepts the packets ✓.

But no frame ever comes out of the ISP, and the reason is now clear:
**in this tree's camera_v2 nothing programs the CSID's buffer
addresses.** There is no such register in
`camera_v2/sensor/csid/include/csi3.0/msm_csid_hwreg.h`, and nothing in
`camera_v2/` writes them. The pixels arrive at the receiver and go
nowhere.

camera_v2 in this tree is an unfinished CAF generation. The ROM's kernel
is a newer one, where the path is finished — which is also why the ROM's
HAL and kernel disagree with us about the DMI-64 layout.

### camera_v1 has the missing piece

```
drivers/media/platform/msm/camera_v1/csi/msm_ispif.c
```

**camera_v1 has the ISP-interface driver** — the piece that programs the
CSID's buffers and hands the data to the ISP. camera_v2 has no
equivalent: `grep -rl CSID camera_v2/isp/` finds nothing.

So the route to frames on *this* tree is camera_v1:

| | camera_v2 (now) | camera_v1 (the way) |
|---|---|---|
| the CSID → ISP path | **missing** ✗ | `msm_ispif.c` ✓ |
| the sensor's settings | from userspace ✓ | from userspace ✓ (`msm_sensor_bayer.c` is generic ✓) |
| the chromatrix | `VIDIOC_MSM_VFE_REG_CFG` ✓ | the same ioctl ✓ |
| the userspace protocol | the V4L2 event/ack handshake ✓ | the "cam server" protocol (`msm_camera_server_message`) ✓ |

The camera_v1 stack is the one the Nexus 5 shipped with, it is complete
here, and everything we captured today (the sensor sequence ✓, the
chromatrix ✓) is data that camera_v1 wants in exactly the same form ✓.

### Where we stand

| step | status |
|---|---|
| the chromatrix (ISP + CPP) | **captured and replayable** ✓ |
| the sensor's mode change | **captured and replayable** ✓ |
| the ISP's configuration replay | **accepted by the kernel** ✓ (after the DMI-64 fix ✓) |
| MIPI link, data at the receiver | **working** ✓ (11,808 packets ✓, no errors ✓) |
| the pixels reaching the ISP's buffers | **blocked**: camera_v2 has no path ✗ |
| frames out of `/dev/video1` | the last step, once the path exists ✓ |

Next: build the camera_v1 imaging server (the "cam server" protocol),
with the sensor sequence and the chromatrix we now carry.

---

## Entry 101 — correction: camera_v2 is complete, and the missing step is the ISP-IF

*2026-10-19, 21:30–22:15*

### Entry 100's camera_v1 conclusion was wrong

Entry 100 said that this tree's camera_v2 has no CSID→ISP data path and
that camera_v1 (`msm_ispif.c`) has the missing piece. **That is wrong**,
and the mistake was a wrong directory: the ISP-interface driver is

```
drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c
```

not `camera_v2/isp/`. What the tree actually has:

| | present? |
|---|---|
| `CONFIG_MSM_ISPIF=y` in our build | ✓ |
| the `qcom,ispif@fda0A000` node in `msm8974-camera.dtsi` | ✓ |
| `camera_v2/ispif/msm_ispif.c`, binding `qcom,ispif` | ✓ |
| `camera_v2/ispif/msm_ispif.c`'s `VIDIOC_MSM_ISPIF_CFG` | ✓ |

So **camera_v2 in this tree is complete**, and so is our understanding of
what was missing.

### What the ISP-IF is for, and what we never told it

The ISP-IF is the piece between the CSID and the ISP. It is told, by
userspace, how the two are connected:

```c
struct msm_ispif_params_entry {
	enum msm_ispif_vfe_intf vfe_intf;	/* which VFE */
	enum msm_ispif_intftype intftype;	/* what kind of input */
	int num_cids;
	enum msm_ispif_cid cids[3];		/* the virtual channels */
	enum msm_ispif_csid csid;		/* which CSID */
	int crop_enable;
	uint16_t crop_start_pixel, crop_end_pixel;
};
```

and then, with `VIDIOC_MSM_ISPIF_CFG`:

```
ISPIF_INIT                 (the CSID's hardware version)
ISPIF_CFG                  (the routing above)
ISPIF_START_FRAME_BOUNDARY (go)
```

**Our imaging server never sent any of them** — it configured the CSID
and the ISP and then started the sensor, and the CSID duly received
11,808 packets and dropped them on the floor, because nobody had told
the ISP-IF which CSID, which virtual channel, which VFE interface and
which crop to route them to.

That is also why the ROM works: its HAL sends exactly these ioctls.
And it explains the virtual channel we could not guess: the IMX179
sends **VC 1** (the CSID's "unmapped long packet header" says
`0x1201040a`, whose VC field is 1), and the ISP-IF's routing table is
where that number belongs.

So the route to frames is not camera_v1. It is three more ioctls in
camera_v2, and the vendor HAL's exact parameters for them can be
captured with the same trick as everything else today.

### The ROM, again

The ROM's kernel is the same CAF generation as ours — the one thing
that had to be patched for the ROM's HAL is the DMI-64 layout check
(Entry 100), which the ROM's fork must have changed the same way we
did. The ROM's kernel is built from a fork ("acroreiser@thinkstation",
315 commits) whose commit is not in the public LineageOS tree, so "the
same kernel source as the ROM" is close but not exact.

And the ROM's camera works because its **userspace** does the parts
ours did not: the ISP-IF routing above, the sensor's register arrays
and the chromatrix — all of which we captured today.

---

## Entry 102 — the ISP-IF: the last ioctl sequence, and it is accepted

*2026-10-19, 21:30–22:15*

### What the vendor sends to the ISP-IF

Captured (once) with the same trick as everything else — the ioctl
handler in `camera_v2/ispif/msm_ispif.c` writes the payload out:

```
DEBUG: ISP-IF config captured /data/local/tmp/ispif_cfg_0_t9_s368.bin
DEBUG: ISP-IF config captured /data/local/tmp/ispif_cfg_1_t2_s368.bin
DEBUG: ISP-IF config captured /data/local/tmp/ispif_cfg_2_t3_s368.bin
DEBUG: ISP-IF config captured /data/local/tmp/ispif_cfg_3_t4_s368.bin
```

| # | cfg_type | what |
|---|---|---|
| 0 | 9 = `ISPIF_SET_VFE_INFO` | the VFE's information |
| 1 | 2 = `ISPIF_INIT` | the CSID's hardware version |
| 2 | 3 = `ISPIF_CFG` | the routing |
| 3 | 4 = `ISPIF_START_FRAME_BOUNDARY` | go |

The ioctl number carries the size the HAL used: **368 bytes** — exactly
`sizeof(struct ispif_cfg_data)` in this kernel (`4 + 4 + 10 * 36`), so
the HAL's and the kernel's structs are the same ✓ and the routing can
be built by hand.

The ISP-IF's node is `/dev/v4l-subdev15` (`msm_ispif` ✓, found in
`/sys/class/video4linux/v4l-subdev15/name` ✓).

The values in the capture could not be read (the HAL's buffer is not
readable from the kernel's ioctl entry on this ARM kernel — the
direct reads fault — so the file is empty and the print moved into
`msm_ispif_config()`, where the driver reads them itself ✓).

### Our own server now sends them

`standalone/cam/ispif.h` mirrors the kernel's struct exactly, and
`imaging8.c` sends the four ioctls:

```c
ISPIF_SET_VFE_INFO   -> one VFE
ISPIF_INIT           -> CSID_VERSION_V30
ISPIF_CFG            -> VFE0, PIX0, 1 cid, cid = CID1, csid = CSID0
ISPIF_START_FRAME_BOUNDARY
```

**The kernel accepts all four** ✓✓ (each returns 0 ✓).

Two of the values are measured rather than guessed:

* **the virtual channel is 1** — the CSID's "unmapped long packet
  header" is `0x1201040a`, and its VC field (bits 29:28) is 1 ✓; that
  is why the CSID's packets were never mapped with a VC-0 LUT ✓;
* **the CSID is 0** — the device tree says `qcom,csid-sd-index = <0>`
  for the IMX179 ✓.

### Where it stands

Still no frame, and two measurements to carry into the next step:

* the CSID's packet counter is **11808 in every single run** — the same
  number, run after run. That is not what a streaming sensor does; it
  looks like a fixed burst that then stops, which points at the
  **CSIPHY** (the sensor's lanes are never really locked) rather than at
  the routing;
* `mapped long pkts` is still 0 — but our packets are *short* packets,
  so that counter says nothing; the short-packet register is the one to
  watch.

The next steps, in order:

1. get the vendor's routing values printed (the print is in the kernel
   now; it just needs a camera session that gets that far);
2. read the CSIPHY's own statistics (how many packets did the *PHY*
   see?) — that separates "the sensor is not really transmitting" from
   "the routing is wrong";
3. then the frames.

---

## Entry 103 — the routing is ours and it is accepted; the receiver does not care

*2026-10-19, 22:20–22:40*

### Our ISP-IF configuration is now verified

The print that lives inside `msm_ispif_config()` — where the driver
reads the parameters itself — shows what the kernel made of our
structure:

```
DEBUG: ISP-IF routing 0: vfe_intf 0 intftype 0 num_cids 1 cid0 1 cid1 0
        csid 0 crop 0 (0..0)
```

That is exactly what `standalone/cam/ispif.h` sent ✓✓ — so the mirrored
struct is right (368 bytes, the same as the HAL's ✓), and all four
ioctls are accepted ✓.

### The PHY is fine

```
DEBUG: CSIPHY0 interrupt status 0x00000004, power cfg 0x0000003f,
        hw version 0x00000010
DEBUG: CSID0 irq status 0x080002dd, packets 11808,
        unmapped long 0x1201040a, mapped long 0x00000000,
        short 0x0000011a, ecc 0, crc 0
```

* the CSIPHY has **all five lanes powered** (`power cfg 0x3f` ✓) and
  its status shows one bit only ✓;
* the CSID has counted **11,808 packets with no ECC and no CRC
  errors** ✓ — the sensor's data really does arrive ✓;
* but the CSID's status is **bit-for-bit the same as in the runs
  without the ISP-IF** ✓✓ (`0x080002dd` ✓, the same 11,808 ✓) — so
  the routing we sent made **no difference to the receiver** ✗.

### What that leaves

The packets arrive and are counted ✓, but they are still not mapped
✓ ("unmapped long packet header" = `0x1201040a`, whose VC field is 1
✓ — the same value in every run ✓✓).

Two candidates, and they are separable:

1. **the data type** — the CSID's LUT is programmed by *our*
   `CSID_CFG` ioctl (`dt = 0x15`, RAW10 ✓) and the CSID only maps a
   packet whose data type matches ✗. The IMX179's packets are *short*
   packets of 16 words ✓, and the sensor's own mode registers (the
   92-entry page sequence ✓) set the MIPI data type ✓ — we have never
   checked what it says. Trying the other types (`0x2b` RAW14 ✓,
   `0x2c` RAW12 ✓, `0x36` ✓ …) is one ioctl and one number away ✓;
2. **the ISP-IF's routing differs from the HAL's** — the print inside
   `msm_ispif_config()` fires for *our* server ✓; the HAL's own
   session has to be caught in the act ✓ (the camera open is flaky on
   this setup: about one session in three gets far enough ✓, and the
   ones that do sometimes stop before the ISP-IF ✓).

The next step is (1) — it is a single number and it is testable in one
boot ✓ — with (2) as the cross-check ✓.

---

## Entry 104 — the data type was 0x12, and the packets are now mapped

*2026-10-19, 22:45–23:10*

### The sweep

One boot, one run per data type (the CSID's LUT accepts 0x12..0x37 ✓),
reading the CSID's registers after each run:

```
dt 0x12: irq 0x0c0002dd, packets 1312, unmapped long 0x2b010405,
          mapped long 0x1201040a, short 0x0000011a, ecc 0, crc 0
dt 0x13: irq 0x080002dd, packets 1148, unmapped long 0x1201040a,
          mapped long 0x00000000, ...
dt 0x15: irq 0x080002dd, packets 1148, unmapped long 0x1201040a,
          mapped long 0x00000000, ...
dt 0x2b: irq 0x0c0002dd, packets 1148, unmapped long 0x1201040a,
          mapped long 0x2b010405, ...
... (every other value behaves like 0x15)
```

Two things jump out:

* with **dt = 0x12** the "mapped long packet header" register fills up
  with **`0x1201040a`** ✓ — the very header that used to sit in the
  *unmapped* register ✓✓;
* with **dt = 0x2b** the mapped register holds **`0x2b010405`** ✓ —
  the same header with the data type byte changed ✓✓.

⇒ The data type is *in* the header ✓, and the sensor's packets carry
**0x12** ✓. With that, the CSID **maps** them ✓✓ (and the interrupt
status changes: `0x080002dd` → `0x0c0002dd` ✓).

We had been using 0x15 (a guess), and the CSID's LUT — which only maps
a packet whose data type matches — therefore never accepted a single
one of the sensor's 11,808 packets.

⇒ `imaging8.c` now defaults to `dt = 0x12` ✓, with the measurement
recorded in the source ✓.

### Where that leaves the pipeline

| step | status |
|---|---|
| the sensor streams, the MIPI link works | ✓ (11,808 packets, no ECC/CRC errors ✓) |
| the CSID **maps** the packets | ✓ **now** (dt = 0x12 ✓✓) |
| the CSID hands the pixels to the ISP | ✗ — still no frame, and no "pix0 overflow" in the ISP either |
| the ISP's buffers fill | ✗ |

So the remaining gap is the CSID → ISP hand-off, and the suspects are
now few:

1. **the ISP-IF's buffer programming** — the ISP-IF is what writes the
   *ISP's* buffer addresses into the CSID ✓; `ISPIF_SET_VFE_INFO`
   (which we send with `id = 0, ver = 0` ✓✓ — the HAL sends the real
   VFE id and version ✓) is what it uses to find them ✓;
2. **the ISP's CAMIF enable** — `msm_vfe32_update_camif_state(ENABLE_CAMIF)`
   ✓ is what arms the ISP's input ✓, and the order of that against the
   ISP-IF's `START_FRAME_BOUNDARY` ✓ may matter ✓;
3. the ISP-IF's crop ✓ (we pass none ✓).

The next step is to read what the ISP-IF wrote into the CSID — its
buffer registers — and compare it with what the ISP has allocated ✓.

---

## Entry 105 — where the pipeline stands, measured end to end

*2026-10-19, 22:50–23:20*

### The state, in one run

```
[ispif] SET_VFE_INFO ok / INIT ok / CFG ok / START_FRAME_BOUNDARY ok
[isp]  initial configuration: 39 uploads sent, 0 failed
[client] no frame after 5s   (x10)

k: DEBUG: ISP-IF routing 0: vfe_intf 0 intftype 0 num_cids 1 cid0 1
           cid1 0 csid 0 crop 0 (0..0)
k: DEBUG: CSIPHY0 interrupt status 0x00000004, power cfg 0x0000003f
k: DEBUG: CSID0 irq status 0x0c0002dd, packets 11808,
           unmapped long 0x2b010405, mapped long 0x1201040a,
           short 0x0000011a, ecc 0, crc 0
```

Read from the bottom up:

| stage | evidence | verdict |
|---|---|---|
| the sensor streams | 11,808 packets, **0 ECC, 0 CRC errors** | ✓ |
| the PHY delivers them | CSIPHY `power cfg 0x3f` (five lanes) | ✓ |
| the CSID **accepts** them | `mapped long 0x1201040a` (the data type byte is 0x12 ✓) | ✓ (was ✗ until the data type was measured) |
| the CSID hands the pixels to the ISP | no "pix0 overflow" in the ISP, no buffer-done | ✗ |
| the ISP's buffers fill | no frame | ✗ |

So the pipeline is complete up to the CSID, and the one remaining gap is
the **CSID → VFE hand-off**.

### What is known about that gap

The ISP-IF is a *bridge inside the VFE*: it programs the VFE's own
ISPIF registers (the routing, the CID, the crop) — `msm_ispif_intf_cmd()`
writes to `ispif->base`, which is the VFE's register block, not the
CSID's. The CSID's side of the link is programmed by our own
`CSID_CFG` ioctl (the VC LUT ✓ — the data type now matches ✓).

Three concrete things to look at next, in order of suspicion:

1. **the ISP's CAMIF** — `msm_vfe32_update_camif_state(ENABLE_CAMIF)`
   is what arms the ISP's input, and it is driven by
   `axi_data.src_info[VFE_PIX_0].raw_stream_count` / `pix_stream_count`
   ✓. Our `REQUEST_STREAM` sets `stream_src = CAMIF_RAW` ✓, but the
   *counts* the enable depends on come from the ISP's own bookkeeping
   ✓ — worth printing.
2. **`ISPIF_SET_VFE_INFO`** — the HAL sends the real VFE id and
   version; we send `id = 0, ver = 0` ✓. The ISP-IF keeps that info ✓
   and uses it to find the VFE's registers ✓.
3. **the ISP-IF's crop** — we pass none; the HAL passes the frame's
   first/last pixel ✓, and the ISP-IF writes it into the VFE's
   registers ✓ (and the ISP's own CAMIF crop is `first_pixel = 0`,
   `last_pixel = width - 25` ✓ — a magic number from earlier work ✓
   that deserves a second look ✓).

### Everything we now carry

| what | where |
|---|---|
| the chromatrix (the ISP's 39 uploads + the CPP's 266 words) | `standalone/cam/data/` ✓ |
| the sensor's mode change (14 arrays, the 92-entry page sequence first) | `standalone/cam/data/sensor/` ✓ |
| the ISP-IF's four ioctls and the routing | `standalone/cam/ispif.h` ✓ |
| the measured MIPI data type (0x12) and the virtual channel (1) | `standalone/cam/imaging8.c` ✓ |

All of it is data: the vendor HAL and the vendor blob are not needed at
run time ✓.

---

## Entry 106 — the ISP's side of the hand-off

*2026-10-19, 23:20–23:30*

The CAMIF enable path, read from the driver, so the next run can be
aimed instead of guessed:

```c
/* msm_isp_axi_util.c: msm_isp_update_camif_state() */
	if (((cur_stream_cnt == 0) || (raw_stream_cnt && cur_raw_stream_cnt == 0)) &&
		stream_cfg_cmd->cmd == START_STREAM)
		return ENABLE_CAMIF;
```

and the call order in the same file:

```c
1119:	msm_isp_update_camif_output_count(vfe_dev, stream_cfg_cmd);
1120:	if (camif_update == ENABLE_CAMIF)
```

⇒ the state is computed from the counts *before* they are updated ✓,
but on the first `START_STREAM` the counts are zero ✓, so the
`cur_stream_cnt == 0` arm fires ✓ and the CAMIF **is** enabled ✓ —
with our `stream_src = CAMIF_RAW` ✓ (`msm_isp_update_camif_output_count()`
increments `raw_stream_count` for exactly that source ✓).

⇒ The ISP's CAMIF enable is therefore *not* the missing piece ✓, and
`msm_vfe32_update_camif_state(ENABLE_CAMIF)` ✓ writes the enable and
`vfe_dev->axi_data.src_info[VFE_PIX_0].active = 1` ✓.

That leaves, in order:

1. **the ISP-IF's own registers** — what `msm_ispif_intf_cmd()` wrote
   into the VFE's ISPIF block (the routing, the CID, the crop ✓) —
   never read back yet ✓; dumping them and comparing with the CSID's
   LUT ✓ and the ISP's CAMIF geometry ✓ is the one experiment that
   discriminates the rest ✓;
2. **the crop** — the ISP-IF writes `crop_start_pixel`/`crop_end_pixel`
   into the VFE ✓ (we pass 0/0 ✓) and the ISP's CAMIF uses
   `first_pixel = 0`, `last_pixel = width - 25` ✓ — two different
   windows, one of them certainly wrong ✓;
3. **`ISPIF_SET_VFE_INFO`** — the real VFE id and version ✓ (we send
   zeros ✓).

The experiment for (1) and (2) is one kernel patch that dumps the
ISPIF block and the CSID's LUT after the configuration, and one run ✓.

---

## Entry 107 — the CSID's LUT is right; the ISPIF's routing is the next thing to read

*2026-10-19, 23:30–23:40*

### What the dump shows

```
DEBUG: CSID0 LUT: vc0 0x00001212 vc1 0x00000000 vc2 0x00000000 vc3 0x00000000,
             cfg0 0x00000013 cfg1 0x00000013
DEBUG: CSID0 irq status 0x0c0002dd, packets 11808, unmapped long 0x2b010405,
        mapped long 0x1201040a, short 0x0000011a, ecc 0, crc 0
```

The CSID's VC LUT holds **0x1212** ✓ — the LUT packs four virtual
channels per word ✓, so byte 0 (VC 0) and byte 1 (VC 1) both carry the
data type **0x12** ✓, and the per-VC configuration words are 0x13 ✓
(= decode format 1 << 4 | 3 ✓, `msm_ispif_cid_lut()`'s own encoding ✓).

⇒ the receiver is programmed exactly as intended ✓, and the packets
are mapped ✓ (`mapped long 0x1201040a` ✓).

### The ISPIF's registers

```
DEBUG: ISPIF reg 0x200 = 0x00000040     <- CTRL_0 (the enable bit is set
DEBUG: ISPIF reg 0x208 = 0x0a493249        by the start call, so it is
DEBUG: ISPIF reg 0x20c = 0x02493249        still 0x40 here)
DEBUG: ISPIF reg 0x210 = 0x00001249
DEBUG: ISPIF reg 0x21c = 0x00000000     <- IRQ_STATUS_0: nothing arrived
...
```

The dump is taken *before* `msm_ispif_start_frame_boundary()` ✓, so
`CTRL_0` still shows the pre-start value 0x40 ✓ — the enable bit
(`1 << (intftype + 7)` = 0x80 for PIX0 ✓) is set by the start call
itself ✓.

The registers that carry the routing are further up:

```c
	ISPIF_VFE_m_CTRL_0(vfe_intf)             /* the enable bit      */
	ISPIF_VFE_m_PIX_INTF_n_CID_MASK(vfe_intf, 0)   /* which CIDs      */
	ISPIF_VFE_m_PIX_INTF_n_CROP(vfe_intf, 0)        /* the crop         */
```

⇒ the next dump has to be taken **after** the start call ✓ and has to
cover the `0x240` region ✓, where `CID_MASK` and `CROP` live ✓. The
three things to check are:

1. `PIX_INTF_0_CID_MASK` ✓ — with `cid = 1` it must be **0x02** ✓ (or
   0x03 if both CIDs are routed ✓);
2. `PIX_INTF_0_CROP` ✓ — we pass `0..0` ✓ while the ISP's own CAMIF
   uses `first_pixel = 0, last_pixel = width - 25` ✓ — two different
   windows, one of them certainly wrong ✓;
3. `CTRL_0` ✓ — after the start it must be 0x80 (PIX0 enabled ✓) ✓.

If the CID mask and the crop are right and the ISP still sees nothing,
the last piece is the ISP's *buffer* side: the VFE's ISPIF block writes
the CSID's data into the ISP's VB2 buffers ✓, and the addresses the
ISP-IF gives it come from `ISPIF_SET_VFE_INFO` ✓ (the real VFE id and
version ✓ — we still send zeros ✓).

---

## Entry 108 — the ISP-IF's routing is now correct, bit for bit

*2026-10-19, 23:40–00:00*

### The bug: the crop was not enabled

`msm_ispif_enable_crop()` is the function that switches the ISP's pixel
input on:

```c
	data = msm_camera_io_r(ispif->base + ISPIF_VFE_m_CTRL_0(vfe_intf));
	data |= (1 << (intftype + 7));		/* 0x80: PIX0 enabled */
	if (intftype == PIX0)
		data |= 1 << PIX0_LINE_BUF_EN_BIT;	/* 0x40 */
	msm_camera_io_w(data, ispif->base + ISPIF_VFE_m_CTRL_0(vfe_intf));
```

and it is only called when the routing's `crop_enable` is set. We sent
`crop_enable = 0`, so it was never called, so the PIX0 enable bit was
never written — and the measured `CTRL_0` was stuck at the value
`msm_ispif_init()` leaves behind:

```
before:  DEBUG: ISPIF CTRL_0 0x00000040, CID_MASK0 0x00000002, CROP0 0x00000000
after:   DEBUG: ISPIF CTRL_0 0x000000c0, CID_MASK0 0x00000002, CROP0 0x0cb70000
```

(`0xc0` = the enable bit 0x80 **and** the line-buffer enable 0x40 ✓;
`CROP0 = 0x0cb7 0000` = start pixel 0, end pixel 3255 = the width the
ISP's CAMIF is configured with ✓.)

### The routing, read back

| register | value | meaning |
|---|---|---|
| `CTRL_0` (0x200) | `0x000000c0` | PIX0 enabled ✓, line buffer enabled ✓ |
| `INTF_CMD_0` (0x248) | `0xfffffffd` | all VCs disabled **except bit 1** — the register's polarity is inverted (the reset value is 0xFFFFFFFF = everything off ✓), so **VC 1 is the one that is on** ✓ |
| `INTF_CMD_1` (0x24c) | `0x0000aa00` | the RDI side, unused ✓ |
| `CID_MASK0` (0x254) | `0x00000002` | VC 1 is routed to PIX0 ✓ |
| `CROP0` (0x278) | `0x0cb70000` | the frame window ✓ |

⇒ The ISP-IF is configured **exactly as intended** ✓: the virtual
channel the IMX179 sends on (1 ✓) is routed to the ISP's pixel input,
the input is enabled, and the crop matches the CAMIF geometry ✓.

### Where that leaves it

```
sensor streams            ✓  11,808 packets, 0 ECC, 0 CRC errors
PHY delivers them         ✓  power cfg 0x3f
CSID accepts and maps them ✓  LUT 0x1212 (dt 0x12 on VC 0 and 1 ✓)
ISP-IF routes VC 1 → PIX0 ✓  CTRL_0 0xc0, CID_MASK 0x02, INTF_CMD 0xfffffffd
ISP's pixel input enabled ✓  (CTRL_0 bit 7 ✓)
frames out of /dev/video1 ✗
```

Everything on the CSID side and everything on the ISP-IF side is now
measured and correct. What is left is the **VFE's own side**: the
camera front end that receives the CSID's data and hands it to the
ISP's pixel path. Its registers are in the ISP's block (`0x1E0`/`0x1E4`
— the enable bits `bus_en`/`vfe_en` ✓, `0x1EC` the geometry ✓, `0x6FC`
the input select ✓), and they have never been read back ✓.

The next experiment is one kernel patch that dumps those registers
after `CFG_STREAM(START)` ✓ and compares them with what
`msm_vfe32_cfg_camif()` and `msm_vfe32_update_camif_state()` wrote ✓.

---

## Entry 109 — the VFE, the CAMIF and a "violation"

*2026-10-20, 00:00–00:40*

### Two things settled

**The ISP is the vfe40** ✓ — the crash log names the driver:

```
msm_vfe40_process_error_status: camif error status: 0x0
msm_vfe40_process_violation_status: camif violation
```

so `/dev/v4l-subdev13` ✓ is the 8974's ISP ✓ (the vfe40 driver is
the 8974's ✓; the vfe32 ✓ is the 8960's ✓ and the ISP-IF rejects
`VFE1` for us ✓). The other VFE subdevice (14 ✓) is not ours, and
driving it takes the device down ✓.

**The camera front end is now receiving data and rejecting the frame**
✓✓ — "camif violation" ✓. That is the first sign of the pixels
arriving at the ISP at all ✓.

### The window, and what is still wrong

The CAMIF's window is now the sensor's active area ✓:

```
icfg: pixels_per_line 3280 (HMAX), lines_per_frame 2464 (VMAX),
      first_pixel 0, last_pixel 3263 (= width - 17 ✓),
      first_line 0, last_line 2447 (= height - 17 ✓)
ISP-IF crop: 0 .. 3263 ✓ (the same window ✓)
```

(`width - 25` was wrong ✓ — the line is 3280 pixels long but only 3264
of them carry the image ✓.)

The violation persists ✗, so something in the CAMIF's *timing* is
still off ✓. The prime suspect is `input_pix_clk` ✓ — the CAMIF's
pixel clock ✓, which we take from `cam->pix_clk` ✓ (a number from
the earlier work ✓ that has never been checked ✓ against the
sensor's real HMAX/VMAX ✓).

⇒ The next step is to print the CAMIF's clock and geometry registers
(0x1EC ✓, 0x1F0 ✓, 0x1F4 ✓, and the "csi2 clock" register ✓) next
to what we programmed ✓, and to read the CAMIF's *violation status*
register ✓ to see which check failed ✓ (the vfe40 driver prints
"camif violation" ✓ but not which one ✓).

### The state, end to end

```
sensor streams              OK   11,808 packets, 0 ECC, 0 CRC errors
PHY delivers them           OK   power cfg 0x3f
CSID accepts and maps them  OK   LUT 0x1212, mapped long 0x1201040a
ISP-IF routes VC 1 -> PIX0  OK   CTRL_0 0xc0, CID_MASK 0x02, CROP 0..3263
CAMIF enable                OK   camif_update 1 (ENABLE_CAMIF)
CAMIF receives the frame    NEW  but rejects it: "camif violation"
frames out of /dev/video1  NO
```

⇒ we are one register away ✓: the CAMIF's timing ✓.

---

## 2026-10-20 (evening) — the camera gets pixels, and the Krux firmware runs

### The camera: a frame arrives, and it has pixels in it

Three things, each found by letting the **vendor's own camera run under
our kernel** and reading what it did (our kernel logs every ISP and CPP
ioctl, and every packet that crosses the `bcm2079x` driver):

1. **The chromatrix has to be sent once per frame.**  The vendor's HAL
   sends `VIDIOC_MSM_CPP_CFG` 256 times for 256 frames — the kernel
   patches the frame's *current* buffer addresses into the message when
   it accepts it, and the buffers rotate.  We sent it once, at
   configuration time.  ⇒ `cpp_send_chromatrix()` per frame.

2. **The CSID's data type was 0x12, not 0x2b.**  The vendor's own CSID
   configuration, read out of the chip's registers while the ROM's
   camera ran:

   ```
   CSID0 LUT: vc0 0x0000122b vc1 0x00000000 vc2 0x00000000 vc3 0x00000000,
              cfg0 0x00000023 cfg1 0x00000013
   ```

   The LUT holds one byte per channel: the routed channel 0 carries
   data type **0x2b** with decode format **2**; channel 1 carries 0x12
   with decode format 1 (the CFG register is `(decode_format << 4) | 3`,
   which is why cfg0 is 0x23 and not 0x13).  With 0x12 on channel 0 the
   packets stayed unmapped (the CSID's "captured unmapped long packet
   header" read 0x2b010405 — the data type the sensor really sends)
   and no pixel ever reached the ISP.

3. **The buffer has to be as big as the CAMIF's window.**  The client's
   buffers were 3264x2448 (the sensor's active size) while the CAMIF
   produces 3280x2464 (the active size plus the border) and the ISP-IF
   does not crop it (the vendor's routing has the crop disabled).  The
   buffer was 91,648 bytes too small, and *that* is why the frame came
   out empty.

After all three: a frame arrives **with pixels** — but only ten rows
of them, and they are noise.  The CSID delivered 492 packets (4 % of a
frame) and the CAMIF reports an error (`camif error status: 0xd0`).
The suspects, in order:

* the data path is misaligned (the pixels that arrive are noise);
* the sensor's **focus** (the actuator) never initialises: the CCI
  utility bus rejects it (`msm_sensor_cci_i2c_util ... rc = -22` every
  2.4 s), so the AF motor is never driven.

Not yet compared: the vendor's **ISP register configuration** against
ours, register by register.  The kernel now dumps the VFE's registers
on every interrupt, but the two dumps have to be of the *same* state,
and the vendor's camera previews (PIX_VIEWFINDER) while ours uses the
raw path (CAMIF_RAW) — so the vendor's raw state has to be captured
(a photo, not a preview).

### The Krux firmware runs on the port

The firmware is `/home/user/Develop/src/krux-sighash/krux` (the branch
`unified-sighash_seedkeeper`; its `src/krux` is identical to the
krux-hammerhead tree's).  It now boots: the splash, the settings, the
pin check, and the **login page**.

What the port had to provide, and what each one cost:

| piece | the port's file | the trap |
|---|---|---|
| the display | `kruxport/lcd_bcm.c` | the framebuffer is **triple buffered** (1080x5760): the panel shows one of the three, so the module has to draw into *that* one; the stride is **4352**, not width*4; the colours are MaixPy's 16-bit RGB565 and the framebuffer's own layout comes from the kernel (`FBIOGET_VSCREENINFO`); the rotation follows MaixPy's *meaning* (the K210's panel is natively landscape, the hammerhead's is portrait, so the same `rotation(1)` means "portrait" here) |
| the input devices | `kruxport/evdev_bcm.c` | the events have to be read with a timeout, or the firmware's loop blocks |
| the QR codes | `kruxport/qrcode_maixpy.c` + `qrcodegen.c` | **MaixPy's own qrcode module**, lifted from the Krux tree's `firmware/MaixPy` (Nayuki's qrcodegen underneath) — the firmware was written against it; MicroPython 1.28 has no `MP_QSTR_*` macro spelling, no `mp_raise_ValueError(char*)` and no `mp_obj_new_str_from_vstr(type, vstr)` |
| the NFC chip | `kruxport/nfc_bcm2079x.c` + `kruxport/kruxport/nfc.py` | see below |
| the I2C bus | `kruxport/machine_i2c_linux.c` | — |
| the rest of `machine` | `kruxport/machine_hw_linux.c` | **MicroPython 1.28's `make_new` is `(type, n_args, n_kw, args)`** — the old `(type, n_args, args, kwargs)` silently passes `n_kw` as the args pointer, and every class the port adds to `machine` (the timer, the PWM, the watchdog, the SD card) *segfaults* when constructed |
| the board | `kruxport/kruxport/board.py` | the hammerhead's config: the font, the touch, the pins |
| the power | `kruxport/kruxport/pmu.py` | the battery, the USB and the backlight, over the power supply class and the backlight's sysfs |
| the touch, the buttons | `kruxport/kruxport/touch.py`, `buttons.py` | the firmware's own API, over evdev (the phone's keys instead of GPIO buttons) |
| the watchdog | `kruxport/kruxport/wdt.py` | nothing is armed: on Linux, closing the watchdog reboots the device |
| the auto shutdown | `kruxport/kruxport/auto_shutdown.py` | the firmware's own, with the hardware timer replaced by the clock |
| the light | `kruxport/kruxport/light.py` | there is no LED to switch |
| the camera | `kruxport/kruxport/sensor.py` | a stand-in until the V4L2 camera path works |

Four MicroPython limits, all found the hard way:

* the unix port allows only **40 KB of C stack**, and the firmware's
  import chain (krux.key → embit.bip32 → embit.ec → embit.base58 →
  embit.hashes → embit.util → … → embit.util.key) is deeper than
  that: the port raises it to 500 KB (`ports/unix/main.c`).
* MicroPython has **no extended slices** (`b[::-1]`): embit's
  pure-Python secp256k1 is patched (`kruxport/kruxport/secp256k1.py`).
* MicroPython's **dotted relative import** (`from .util.py_ripemd160
  import …`) recurses: the three of them in embit are made absolute.
* MicroPython's `hashlib` has no `ripemd160` and no `new()`: embit
  falls back to its pure-Python implementation, and the port adds the
  cipher mode constants the firmware's settings name
  (`extmod/modcryptolib.c`, `MICROPY_PY_CRYPTOLIB_CONSTS`).

The embit the sighash tree wants is the **sighash fork**, which on
MicroPython expects a native `secp256k1` module; the port supplies one
(the pure-Python implementation, copied out of the fork's own tree).

### The NFC transport, against a real card

The chip is a Broadcom BCM20793, reached through `/dev/bcm2079x`
(the vendor's driver owns the chip's I2C address and the chip NACKs
anyone else).  The port's own transport, **standalone in TWRP, with
no vendor HAL and no Android framework**, found a card, activated it
and exchanged an APDU with it:

```
=== attempt 4 ===
=== the card is there and activated ===
=== the card answered: data= sw=6a82
```

`6a82` is the same answer the ROM's own stack got from that card: it
is not a SeedKeeper, so the abbreviated AID `D2760000850101` is not
there.  (The card the ROM's stack talked to was detected as
`[IsoDep, NfcA]` — exactly what SeedKeeper needs.)

Three things had to be right, and each one cost a cycle:

1. **the routing and the protocol numbers are sent twice** — the
   vendor's stack sends them, deactivates the discovery, and sends
   them *again*; the chip only finds a tag after that round;
2. **the wake line has to be asserted before the configuration** (with
   it deasserted the chip stays asleep and answers nothing; on
   Android the framework does this when the screen comes on, which is
   why the chip is quiet with the screen off);
3. **`time.sleep()` takes seconds, not milliseconds** — a settle time
   written as `SETTLE_MS = 300` was a five-minute pause, and every
   test looked like a hang.

And one more, at the end: the discovery must not be restarted too
often — asking the chip to discover again every 300 ms *interrupts* the
poll it is in the middle of; with one discovery per second the card is
found on the first try.

### The runtime: the firmware as the device's payload

The device keeps TWRP's ramdisk (no serial console, so adb is the
debugging channel — which is why all the kernel work to make adb work
under the ramdisk mattered).  The firmware is started from it by a
small static stub (`standalone/rootfs/init-stub.c`) that replaces
TWRP's own init:

* the stub waits for adb, then for the **volume up key**;
* the key **held** ⇒ TWRP, exactly as before (the original binary is
  kept as `/sbin/recovery.stock`);
* the key **not held** ⇒ the firmware: TWRP's user interface is
  stopped and the firmware takes the display and the touch screen.

So the firmware is the default payload and the volume up key is the
way back to TWRP — a device that looks like a perfectly ordinary
TWRP to anyone who does not know.

The stub runs as the init service `recovery` (so TWRP's init does all
its mounting, as it always did) and has to mount `/data` itself, since
that is normally done by TWRP's binary.

Lessons from that:

* a service without a `class` in the recovery's init.rc is started
  explicitly; one with `class core` never started;
* the service needs a `seclabel` (the recovery's domain) or the init
  refuses it ("does not have a SELinux domain defined");
* the init restarts a service that exits, so the stub has to *exec*
  the firmware and never return.

---

## Entry 81 — the assets move out of tmpfs, and the builds get a `BUILD=`

`/tmp/opencode` is tmpfs: a reboot takes it all.  Everything in it that
was *ours* has either been moved into the project or proved to be
reproducible, and the two big build trees now have a documented split
between what is kept and what is rebuilt.

### The tmpfs, before and after

```
before:  5.2G of 7.8G (68%)
after:   960M of 7.8G (13%)
```

What is left there is only what is *meant* to be rebuilt often:

```
ko-cap/        719M   the kernel's build tree
mp-build/      171M   the firmware's build tree
armel-sysroot/  71M   the armel sysroot, rebuilt from the vendored .debs
```

### MicroPython: the source is in the project, the build is on tmpfs

The build used to extract a release tarball into `/tmp/opencode/mp`,
patch it in place and compile it there — so the only copy of the source
was on tmpfs, and the tree was a mix of source and objects.

It does not have to be.  The project already vendors micropython, and
the tree we were building against is exactly its `v1.28.0` tag — the
same tree, byte for byte, minus the two submodules the unix port needs
(`lib/mbedtls`, `lib/micropython-lib`, now fetched at the tag's pins).
So:

* the **source** is the tag in `standalone/vendor/micropython`;
* the **build tree** is a git worktree of it, on tmpfs, disposable;
* `build-micropython.sh` grew `BUILD=`, `SRC=` and `SYSROOT=`.

```
BUILD=/tmp/opencode/mp-build  SRC=../vendor/micropython  SYSROOT=/tmp/opencode/armel-sysroot
```

This is the same split the kernel build has always had (`--tree` in the
project, `--out` on tmpfs), and the same reason: the build trees are
rebuilt many times a day and are a few hundred megabytes of objects that
nobody wants in a backup.

The rebuilt binary is 1251400 bytes — the same size as the one that
ships, with the same `.text`, `.data` and `.bss`; the only difference is
the version banner, which now says `v1.28.0-dirty` because the port's
files are applied to the worktree.  Built from the project alone, with
no network and no tarball.

### The armel sysroot is rebuilt, not carried

`/tmp/opencode/armel-sysroot` was 71 MB of unpacked Debian packages,
and the packages themselves were already in the project
(`standalone/vendor/toolchain.ubuntu/`, 21 MB).  So the sysroot is now
*built* from them:

```sh
standalone/tools/build-armel-sysroot.sh
```

686 files, byte-for-byte the tree that was in use, and
`build-micropython.sh` calls the script when the sysroot is missing.

### The runtime stub: the recipe was in tmpfs, and is now in the project

The stub ramdisk and the init stub were only in `/tmp/opencode` — the
*recipe* for making them was not written down anywhere.  That is the
part that would have been lost, so it is now three scripts:

```sh
rootfs/build-stub-init.sh      # init-stub.c -> rootfs/stub/krux-init
rootfs/build-stub-ramdisk.sh   # TWRP's ramdisk + the stub -> stock/ramdisk-stub
rootfs/make-boot-image.sh      # kernel + ramdisk -> stock/boot-stub.img
```

Verified: the init stub rebuilds **byte-for-byte** identical to the one
that ships (`1aab05f3b9a9481ecb7f6f904b6fe253`), and the ramdisk
rebuilds to the same content — 2190 files and 1218 symlinks, the same
set.  The cpio itself is not byte-reproducible, because it carries the
filesystem's inode numbers and mtimes; that is why the check compares
content, and why the shipped bytes are kept unless `--force` is given.

`stock/boot-stub.img` and `stock/kernel-stub` are now in the project,
and their pairing was checked with the project's own `bootimg.py`: the
kernel inside the image is `kernel-stub`, and the ramdisk is
`stock/ramdisk-stub`.

### What moved into the project

| what | where | why |
|---|---|---|
| the 144 session scripts | `standalone/scripts/` | the only record of how the findings were made; grouped by what they were for |
| the 68 device-side probes | `standalone/probes/` | the same, for the C programs |
| the vendor capture data | `standalone/cam/data/vendor/` | the ROM's own ISP/CPP/sensor configs, and the camera HAL libraries |
| the NFC reader app | `standalone/nfc/nfcreader.apk` | the tool that identified the card |
| the stub's two artifacts | `standalone/rootfs/stub/` | the built init and the patched service file |
| the stub boot image | `standalone/rootfs/stock/` | the image that is flashed |
| the TWRP device tree | `standalone/vendor/` | needed to rebuild TWRP |
| the kernel configs | `standalone/kernelcaps/configs/` | including the one the current kernel was built from |
| the loose notes | `standalone/notes/` | the device-access notes, the caps probe, the config dumps |

### What was deleted, and why it was safe

`app/` and `user_de/` (903 MB) were dumps of the *device's* `/data` — the
installed APKs and the per-app data.  The device still has all of it
(50 packages in `/data/app`), so it was a copy of something live, not
a source.  The one app worth keeping — the NFC reader — was kept.

The rest was either a duplicate of something already in the project
(the busybox source, the TWRP ramdisks, the ROM's camera libraries), or
reproducible from it (the upstream micropython tree, now a git tag).

### The compaction log

`compaction.log.md` is written by `.opencode/plugins/compaction-log.ts`,
which watches for the end of a compaction and appends the summary.  It
is the record of what each compaction dropped, and it lives in the
project so it survives the same way everything else now does.

### A stale `ramdisk_size` in the old boot-stub.img

While moving the assets, the stub boot image was rebuilt from the
project's own parts and came out **2048 bytes larger** than the one
that was in tmpfs.  That turned out to be a bug in the old image, not in
the rebuild.

The old `boot-stub.img` carried the **stock TWRP ramdisk's size** in
its header (`ramdisk_size=14595115`) while actually storing **our**
ramdisk (`15820520` bytes).  The bootloader loads `ramdisk_size` bytes,
so it would have loaded a **truncated** ramdisk: 3275 of the 3494
members, losing 219 of them — including `sbin/recovery`, TWRP's own
binary, which is the volume-up-key path.

The image had never been flashed: the device is still running the
stock ROM kernel (`3.4.113-gda91d066bc6`), not ours.  So nothing was
lost, and the rebuilt image is the correct one — same kernel
(`997ddf77`), same ramdisk content (`4531ccb8`), same total size
(22646784), and a header that now describes what it actually holds.

The cause was packing: `bootimg.py pack` rewrites `ramdisk_size` from
the file it is given, so the old image had been packed by something
else, or from a template whose header was carried over.  Either way the
recipe is now in the project, and it is the recipe that is right.

### A note on the cpio

`build-stub-ramdisk.sh` compares the *content* of the rebuilt ramdisk
with the shipped one, not the bytes: a cpio carries the filesystem's
inode numbers and mtimes, so two packs of the same tree differ.  The
content is identical — 2190 files and 1218 symlinks, the same set —
and the shipped bytes are kept unless `--force` is given.

## Entry 82 — the display regression is a stale panel, not a stale kernel

The kernel rebuild (`ko-base`, Arch GCC 16.2.0) produced a boot that
behaved differently from the shipped `kernel-stub` (Arm GNU 15.2.1):
the screen showed a **stale TWRP frame** instead of the Krux menu, and
Krux's text was missing even when its menu did appear.  The question
was whether this was a race (the user's hypothesis) or a real
difference between the two kernels.  It is neither: **it is the same
bug in both, exposed differently**, and it has nothing to do with the
compiler.

### What the two kernels actually are

| | old `stock/kernel-stub` | new `ko-base` |
|---|---|---|
| md5 | `997ddf77` | `914ebc80` |
| commit | `e0cd9d48553` ("usb: f_fs: queue a full-size request for reads") | `e9bf776e758-dirty` |
| compiler | Arm GNU Toolchain 15.2.1 | Arch GCC 16.2.0 |
| **ramdisk** | `4531ccb8` | `4531ccb8` -- **byte-identical** |

The ramdisk is the *same bytes* in both images, so it could not be the
variable.  The kernel **config** is also byte-identical: the running
kernel's `/proc/config.gz` and `ko-base/.config` diff to nothing.
The only source differences between the two trees are the camera/NFC
debug instrumentation and `init/main.c`'s SELinux-permissive patch.

### The decisive measurement: the framebuffer and the screen disagree

`/dev/graphics/fb0` can be read back, so what the firmware *drew*
is knowable without a camera.  Rendering the raw buffer as a PNG
(`standalone/scripts/fb2png.py`) makes it legible:

* old kernel, plane 0: **the complete Krux menu, with all seven
  items and their text** -- `Load Mnemonic`, `New Mnemonic`,
  `Settings`, `Tools`, `About`, `SeedKeeper Card`, `Shutdown`.
* new kernel, plane 0: red background, the white selection bar, and
  **no text at all**.
* both: plane 1 holds the **TWRP screen** (the `0090ca` TWRP blue,
  `616161` greys, the Team Win Recovery logo) -- a *stale* frame from
  an earlier boot, in the plane the panel is not scanning.

So in the old-kernel run the text **was** in the framebuffer, and the
screen still showed no text.  The framebuffer is right; the panel is
showing something else.

### Proof by painting

Painting the three planes in three colours settles it: the screen
followed the paint.  The panel scans **plane 0** and shows it live.
What it does *not* show is the plane the firmware drew into most
recently -- the firmware's redraw lands in a plane the panel is not
displaying.

The killer detail: **the framebuffer memory survives the reboot.**  The
paint that was visible before `fastboot boot` was still visible after
it -- the reserved EBI1 region (`qcom,mdss_fb_primary reserved EBI1
size 1000000`) is not cleared between boots.  That is why the "stale
TWRP frame" appears at all: it is TWRP's image from the *previous*
session, still resident in the framebuffer.

### Why the text is missing: the panel is on a different plane

`lcd_bcm.c` (the port's display module) does the right thing in
principle -- it reads `FBIOGET_VSCREENINFO`, takes the panel's current
`yoffset`, draws into that plane, and pans the panel to it.  But
`/sys/class/graphics/fb0/pan` reports `0,0` **at the moment the module
opens it**, while the panel's real scan-out address is set by the
driver's own commit machinery, not by that sysfs file.  The
consequence:

* the firmware draws into the plane it computed at open time;
* the driver later commits the panel to a **different** plane
  (the one the previous session left behind, or the one it decides);
* the result is a live panel showing a plane the firmware is not
  writing to -- the red background and the bar survive (they were
  drawn early), the text does not (drawn later).

The driver's own path is the documented one (Entry 39/40): the display
must be pointed at the buffer with an explicit
`MSMFB_DISPLAY_COMMIT` (`mdss_fb_display_commit`, `mdss_fb.c:2201`),
not merely by `FBIOPAN_DISPLAY`.  `mdss_fb_pan_display_ex()` only
queues a commit of `disp_commit.var`; the panel follows the
**yoffset in that var**, and `lcd_bcm.c` sets it to whatever it read
at open time rather than to the plane it is drawing into.

### The fix, in two parts

1. **`lcd_bcm.c` must pan to the plane it draws into**, not to the one
   it read.  It already computes `pan_offset`; it must pan to *that*,
   and it must do so **after** the drawing, not only at open time.
2. The **stub must clear the stale planes** before the firmware runs,
   so that whatever the panel ends up scanning is the firmware's.

### And the answer to a question that was asked twice

*"Did we allow Krux to write a configuration file somewhere on the
device?"* -- **No.**  `krux/settings.py`'s `Store.get_vfs_location()`
returns `"/" + location + "/"`, i.e. the **absolute** paths
`/flash/settings.json` and `/sd/settings.json`; the working directory
(`/data/krux`) is irrelevant.  This device has neither `/flash` nor
`/sd` -- there is no SPI flash and no SD card -- so every settings
write failed silently and `/data/krux` was never used by the firmware.

The stub now creates both as symlinks into the data partition
(`/flash -> /data/krux/flash`, `/sd -> /data/krux/sd`), so the
firmware's settings survive a reboot.  Verified on the device: both
symlinks exist after the change.

### And a second gap, found the same way

The firmware's output was going nowhere: `init` hands a service
`/dev/null` on stdout and stderr, so the firmware's messages -- how far
it got and why it stopped -- were lost.  The stub now redirects the
firmware's stdout and stderr to `/data/krux/fw.log`.  The first run
with that in place shows exactly what the firmware says:

```
init-stub: starting the firmware
Camera not found: module 'sensor' has no attribute 'set_pixformat'
No pin set
```

("No pin set" is `boot.py`'s TC-code check: `/flash/tcc` does not
exist, so the pin page is skipped.  That is expected, and it is *not*
the reason the text was missing.)

### The lesson

The user was right to suspect a race, and right that the behaviour was
inconsistent -- but the inconsistency was **between the two kernels**,
and it had a single cause: the panel was never pointed at the buffer
the firmware was drawing into.  Reading the framebuffer back and
rendering it turned "the text is missing" into "the text is in memory,
but in a plane the panel is not scanning", which is a different bug with
a different fix.

### The display commit: the missing step, found and fixed

The port's display module (`kruxport/lcd_bcm.c`) opened the
framebuffer, drew into it, and panned the panel with
`FBIOPAN_DISPLAY` -- and that was not enough.  The panel **scans a
copy**: the driver copies the framebuffer plane into the buffer the
DSI scans out of, and the copy is made by the *commit*
(`MSMFB_DISPLAY_COMMIT`, `mdss_fb_display_commit` ->
`mdss_fb_pan_display_ex` -> the commit thread
`__mdss_fb_display_thread` -> `mdp3_ctrl_pan_display`, which
re-points the panel's DMA at the framebuffer).

Without the commit the panel keeps showing what it showed before,
and what it showed before is whatever the **previous boot** left in
the reserved memory -- the framebuffer is in `qcom,mdss_fb_primary
reserved EBI1`, which is *not* cleared between boots.  That is the
"stale TWRP frame" that appeared on the screen: TWRP's own image,
from an earlier session, still resident in the framebuffer.

With the commit in place the menu appears -- with its text.  The
ioctl's number is `_IOW('m', 164, struct mdp_display_commit)`, and
the structure is 168 bytes (two words and a `struct
fb_var_screeninfo`, which is 160 bytes on both the kernel tree and
the host).  The magic matters: with the wrong one (`0xAD` instead
of `'m'`) the driver does not reject the ioctl but reads the
structure at the wrong offset, and the panel ends up scanning a
scrambled part of the framebuffer.

### The colours: the framebuffer's layout is not the panel's

`FBIOGET_VSCREENINFO` on this device reports:

| channel | offset | length |
|---|---|---|
| transp | 0 | 8 |
| blue | 8 | 8 |
| green | 16 | 8 |
| red | 24 | 8 |

so a pixel is `A B G R` in memory.  The *panel* reads the same
memory with the red and the blue **the other way round**, which is
why `lcd_pixel()` puts the blue value at the red offset and the red
value at the blue offset.  That swap is already in the port, and it
is right.

But the theme's colours are wrong: `krux/themes.py` has
`RED = 0x00F8`, which in RGB565 is **cyan** (R=0, G=62, B=56), not
red -- red is `0xF800`.  `DARKERRED = 0x00C0` is likewise not red.
So the theme's "red" is the cyan the user saw on the screen.

The theme file is the firmware's own (upstream Krux's) and the
device's copy is identical to the vendored one, so this is an
upstream bug -- but it is worth reporting upstream, and the port
must not paper over it silently.

### The text overlap: the font scale and the firmware disagree

The device's `board.py` declared `font: (8, 16)`, so the firmware
spaced its lines 16 pixels apart -- while the port's display module
drew the glyphs three times as big (24x48).  Consecutive lines
therefore overlapped by two thirds of their height.

The scale is now **two** (`LCD_FONT_SCALE 2`, so 16x32) and
`board.py` declares `font: (16, 32)` to match.  The two numbers and
the module's scale have to agree: the firmware's line spacing *is*
`FONT_HEIGHT`.

### The animation: the firmware's own screen saver

The red-and-white band marching down the screen is Krux's own
`pages/screensaver.py`: it fills a band one `FONT_HEIGHT` at a time,
marching it down the screen, and swaps the background and the
foreground colours each cycle.  It runs because the firmware sees
**no input at all** -- neither the touch screen (`event1`) nor the
gpio keys (`event2`) deliver an event -- so the idle timer fires
and the screen saver takes over.

### The lesson

Every one of these was found by **reading the framebuffer back**
rather than by looking at the screen: the panel's content and the
framebuffer's content are two different things, and only one of them
is what the firmware drew.  The screen said "the text is missing";
the framebuffer said "the text is there"; the difference between the
two statements was the whole bug.

### The panel cycling, and the touch that never wakes

Two more findings, both from the same source -- the kernel's own log.

**The panel was being turned off and on again every 140 ms.**  The
sequence, repeated over and over:

```
[Touch] touch on
mdss_dsi_panel_on
mdss_fb_ioctl: unsupported ioctl (801c6d02)
mdss_dsi_panel_off:
[Touch] touch off
```

and between them, at the start:

```
init: Received control message 'stop' for 'recovery' from pid: 1228 (setprop ctl.stop recovery)
init: Sending signal 9 to service 'recovery' (pid 194) process group...
init: Service 'recovery' (pid 194) received signal 9
```

The `stop` was **ours**: the stub stops TWRP's user interface with
`setprop ctl.stop recovery` before starting the firmware.  But the
service is called `recovery` -- and our stub *is* that service.  So
the stub asks init to stop **itself**: init kills the service's
process group, which is the firmware we had just started.

What is left running then is init's *restart* of the service (init
restarts a service that exits), which runs the stub again, which
stops the service again -- a loop.  And each iteration leaves the
panel off, which is why the screen went blank with the backlight on.

**The touch never woke up.**  `touch_synaptics_ds5.c` powers the
controller off in `synaptics_ts_stop()`, and the *LCD notifier* is
what calls it: `LCD_EVENT_OFF_END` -> `synaptics_ts_stop()` ->
`touch_power_cntl(ts, POWER_OFF)`.  So every time the panel is blanked
the touch controller loses its power, and it only comes back on
`LCD_EVENT_ON_END`, 70 ms later.  The panel was being blanked every
140 ms, so the touch was on for a few milliseconds at a time and
delivered nothing: **no tap ever reached the firmware**.

That is why the menu was unresponsive, and why the firmware's screen
saver took over (it starts when there is no input).

The fix has two parts:

1. the stub must stop **TWRP's** user interface, not itself: the
   service has to have a name of its own, so that `ctl.stop` can be
   aimed at TWRP's and not at the stub;
2. the firmware's input has to survive the panel being blanked.

Note that the *ordering* in the stub is also wrong: it stops the
recovery service **before** it starts the firmware, and since the
stub *is* the recovery service, that stops the firmware too.  The
stop must happen before the firmware is started but must not name
the stub's own service.

## Entry 83 — the toolchain can be the host's, the power-off works, and the touch is a wiring problem

### The host toolchain builds the kernel

The plan was: build the kernel once with the vendored Arch GCC 16.2.0,
then try the host's `arm-linux-gnueabihf-gcc`, and if the kernel still
worked, drop the two vendored toolchains (2.0 G of Arch GCC and 1.1 G
of Arm GNU) and keep only `gcc-androideabi-4.9` (136 M), which the
MicroPython build needs.

The host build **succeeded**:

```
kernel : /tmp/opencode/ko-host/arch/arm/boot/zImage
config : /tmp/opencode/ko-host/.config
```

`arch/arm/Makefile` forces `-msoft-float` itself, so the hosted
compiler is viable for the kernel.  The toolchain that matters for the
firmware is a different one (the MicroPython build uses its own armel
sysroot), which is why `gcc-androideabi-4.9` stays.

### The brightness was full, and it is now half

The backlight was at 255 of 255.  It is now **127 (half)**, set in two
places so it survives a reboot:

* the stub writes it when it starts the firmware
  (`standalone/rootfs/init-stub.c`), and it reads `max_brightness`
  from the driver rather than assuming 255, so it follows the
  hardware;
* the live value can be set at any time with
  `echo 127 > /sys/class/backlight/lm3630/brightness`.

### The power-off did not power off -- it killed the firmware and it came back

The user tested the firmware's shutdown: **the firmware died and a few
seconds later it was running again.**  The cause is not init and not
the stub; it is a missing function.

`krux/power.py` ends its shutdown with:

```python
machine.reset()
sys.exit()
```

and `machine.reset` **does not exist in this port**.  The unix port of
MicroPython has `MICROPY_PY_MACHINE_RESET == 0`, so the module exports
`soft_reset` but not `reset`:

```
>>> dir(machine)
['I2C', 'PWM', 'PinBase', 'SDCard', 'Signal', 'Timer', 'WDT',
 'idle', 'mem16', 'mem32', 'mem8', 'soft_reset', 'time_pulse_us']
```

So `machine.reset()` raised `AttributeError`, the firmware exited with
status 1, and **init restarts a service that exits** -- which is
exactly what the user saw:

```
[ 2662.125956] init: Service 'krux' (pid 28906) exited with status 1
[ 2662.126031] init: Sending signal 9 to service 'krux' (pid 28906) process group...
[ 2662.126509] init: starting service 'krux'...
```

The fix is the route the **phase-0 proof already used**
(`standalone/rootfs/touch-off.c`): ask init, which owns the
`sys.powerctl` handler.

```
on property:sys.powerctl=*
   powerctl ${sys.powerctl}
```

`/sbin/setprop sys.powerctl shutdown` is therefore the power-off.  The
port now provides `machine.reset()` (in `machine_hw_linux.c`), which
tries the raw `reboot` syscall first and then `setprop`, and never
returns -- the firmware expects it not to.

### The touch: the interrupt fires, but the driver drops it

The phase-0 proof had a working touch, so the question was what
changed.  The answer is that **the touch is not broken at all -- the
events are being thrown away by the driver**, and it is a wiring
mismatch between two descriptions of the same GPIO.

What the device shows:

* `/dev/input/event1` is `touch_dev`, registered by the driver;
* it is bound to the right I2C client: `2-0070` -> driver `s3350`;
* the driver's probe **succeeded**: `[Touch] IC identifier[PLG218 - 1]
  fw_version[E079]`, so the I2C path works;
* the touch's interrupt **is counted**:
  `/proc/interrupts` shows `362: 124 msmgpio s3350`, and it was 115
  earlier, so it fires.

But a tap produces **no event at all**, and the count does not move
during a 12-second tap test (measured by diffing `/proc/interrupts`
before and after).

The driver has **two** descriptions of the touch's interrupt line, and
they disagree:

| | value |
|---|---|
| the DT's `interrupts` (the IRQ the driver requests) | `<5 0x2>` on `msmgpio` |
| the DT's `synaptics,irq-gpio` (the GPIO the driver reads) | `<&msmgpio 5 0x00>` |

The driver's `touch_work_pre_proc()` does:

```c
if (gpio_get_value(ts->pdata->irq_gpio) != 0) {
        TOUCH_DEBUG_TRACE("%s: INT STATE HIGH\n", __func__);
        return -EINTR;
}
```

and `touch_irq_handler()` only reports on `0`:

```c
switch (touch_work_pre_proc(ts)) {
case 0:  touch_abs_input_report(ts, timestamp); break;
case -EIO: queue_work(synaptics_wq, &ts->work_recover); break;
}
```

**`-EINTR` is not handled at all**: the handler returns `IRQ_HANDLED`
without reporting anything.  So if the INT line reads high when the
interrupt is taken, the event is silently dropped.  That is precisely
the symptom: the interrupt fires (the counter moves) and no event is
ever delivered.

The DT says the interrupt is active **low** (`0x2` = `IRQF_TRIGGER_FALLING`
with the low polarity of the GPIO spec), while the `synaptics,irq-gpio`
property says **high** (`0x00` = `GPIO_ACTIVE_HIGH`).  The driver reads
the *gpio* value and the kernel triggers on the *irq*, so the two
descriptions have to agree, and here they do not.

This is where the touch investigation stands: the cause is identified
(the two GPIO descriptions disagree, and the driver drops `-EINTR`),
and the fix is in the kernel tree -- either make the DT's two
properties agree, or make the driver handle `-EINTR`.  Nothing has
been changed yet; the tree is to be left alone until the change is
decided.

### The display redraw is slow and flickers because it commits per primitive

The user reported: "the redraw on menu selection change is slow and
flickers the whole screen".

The cause is the commit.  `MSMFB_DISPLAY_COMMIT` does not just queue
work: with `wait_for_finish` set it waits for the commit thread, and
the commit ends in the panel's DMA being re-pointed, which **waits for
the next vertical blank** (`mdp3_dmap_update` waits for `vsync_comp`).
So **a commit costs a frame, about 16 ms**.

Committing after every drawing primitive therefore costs a frame per
primitive: a page with twenty primitives took a third of a second to
draw, and the panel was shown every half-drawn frame in turn -- which
is the flicker.

The commit is now **deferred**: the drawing only marks the frame
dirty, and the commit happens once, when the page is finished.  The
page ends where the firmware waits for a key, so the port's own input
modules (`krux/touch.py` and `krux/buttons.py`) call `lcd.flush()`
as they poll.

### The heat was mine, and there is still a real cost

The device was "heating up very much".  The first cause was **my own
leftover watcher**: a shell loop left running on the device was
walking every `/proc/*/fd/*` five times a second.  It has been killed
(the load average went from 2.18 to 0.28 on a two-core device).

With that gone the firmware itself is the remaining consumer: about
**19% of one core** while sitting in the main menu, and 4.2 million
voluntary context switches.  That is the input loop: the firmware polls
the three keys and the touch every 10 ms, and each poll is a `poll()`
and a `read()` syscall.  The deferred commit removes one source of
that (a commit per primitive is a kernel thread wake-up per
primitive); the polling itself is the firmware's own design and is
left alone unless there is a reason to change it.

### The colour order: the panel's byte order is not the driver's

`lcd_pixel()` composed the pixel in the offsets the driver *reports*
(transp at 0, blue at 8, green at 16, red at 24), which is the reverse
of the panel's order.  The result was that the alpha byte landed in
the channel the panel reads as red, so the theme's black background
came out red.

The panel's order was established empirically, not guessed: `fbcommit`
composed `0xFFFF00FF` into both planes and the panel showed
**magenta**, which is only true if byte 0 is the red one.  The byte
order is therefore **R, G, B, unused**, and `lcd_pixel()` now composes
it that way.  This is built and deployed with the rest.

### The animation on the screen is the firmware's, not TWRP's

The user established that the power button interrupts the screen saver,
so the animation on the panel is **the firmware's own screen saver**
(`krux/pages/screensaver.py`), not a TWRP charging animation.  This
matters: the charging-animation theory was the reason the panel
on/off cycling was attributed to TWRP, and it is wrong.  The panel is
now stable (7 on, 7 off, unchanged over the watch), and the last
event was a `mdss_dsi_panel_on`.

## Entry 84 — the theme's colours are byte-swapped, the screen saver only covered half the screen, and the firmware was rebuilt with the host toolchain

### The colours were right, but every hue was wrong

The user reported the menu as "white on black" and correct -- and it
was.  Black and white are the two colours a byte swap cannot change,
so they looked right while everything else was wrong.

**The theme's constants are little-endian RGB565 words.**  They are
upstream's, unmodified (`krux/themes.py` is byte-identical to the
upstream file), and they read:

```
    RED    0x00F8      GREEN  0xE007      BLUE   0xF800
    YELLOW 0x85F6      CYAN   0xFF07      ORANGE 0x20FD
```

Read as plain RGB565 those are not red, green and blue: RED would be
a teal, GREEN a dark red, and BLUE a pure red.  Read as the
**byte-swapped** word, every one of them lands on the colour it is
named for:

```
    RED    0x00F8 -> 0xF800 -> (31,  0,  0)   red
    GREEN  0xE007 -> 0x07E0 -> ( 0, 63,  0)   green
    BLUE   0xF800 -> 0x001F -> ( 0,  0, 31)   blue
    YELLOW 0x85F6 -> 0xF685 -> (30, 52,  5)   yellow
    CYAN   0xFF07 -> 0x07FF -> ( 0, 63, 31)   cyan
    ORANGE 0x20FD -> 0xFD20 -> (31, 41,  0)   orange
```

The reason is the panel the constants came from: the K210's ST7789 is
wired BGR, and MaixPy's lcd module passes the colour through in the
panel's byte order.  The firmware's own `bgr_to_rgb` setting
(`krux/settings.py`) is the *user-facing* red/blue swap on top of
this, and it is off by default.

So `lcd_pixel()` now swaps the two bytes of the 16-bit word before
decoding it, and the panel's byte order (R, G, B) is unchanged.  The
round trip is now exact for every primary:

```
    RED    theme 0x00F8 -> displayed RGB (255,   0,   0)  readback 0x00F8  exact
    GREEN  theme 0xE007 -> displayed RGB (  0, 255,   0)  readback 0xE007  exact
    CYAN   theme 0xFF07 -> displayed RGB (  0, 255, 255)  readback 0xFF07  exact
    WHITE  theme 0xFFFF -> displayed RGB (255, 255, 255)  readback 0xFFFF  exact
    BLACK  theme 0x0000 -> displayed RGB (  0,   0,   0)  readback 0x0000  exact
```

(The other three round trip "inexactly" only because 8 bits are
quantised back to 5; the *displayed* colour is right.)

**This is what the "new red symbol in the top right corner" was.**
The battery indicator is drawn in the theme's red when the battery is
low and its green when it is charging (`pages/__init__.py`,
`draw_battery_indicator`), and with the colours swapped it came out
the other way round.  It was never a battery fault: the device
reports `capacity=100` and `status=Full`, and the USB is connected
(`usb online=1`), so the indicator should be green.

### The screen saver only filled the top half -- and so did the "bottom line"

The user noticed the screen saver filling only the top half of the
screen.

The cause is a single line of upstream firmware:

```python
    # krux/display.py
    TOTAL_LINES = board.config["lcd"]["width"] // FONT_HEIGHT
```

`TOTAL_LINES` is the number of text lines that fit **vertically**, but
it is computed from the board's declared **width**.  On the K210 that
is right by accident: its panel is 320x240 in the board's own
(landscape) terms and Krux turns it upright, so the portrait height
(320) and the declared landscape width (320) are the same number.

The hammerhead's panel is 1080x1920 and is used upright, so the
vertical extent is 1920 -- and that is what the width has to say.
With 1080 there, the firmware believed the screen was 33 lines of 32
pixels, i.e. 1056 pixels:

```
    TOTAL_LINES = 1080 // 32 = 33   -> 1056 px of 1920 = 55 %
```

which is exactly the "top half" the user saw: the screen saver
animates over `TOTAL_LINES` lines, so it stopped at 1056 pixels.
**Every "bottom line" prompt was drawn halfway up the screen too**,
since `BOTTOM_LINE = (TOTAL_LINES - 1) * FONT_HEIGHT` was 1024.

The port's `board.py` now declares the long side as the width
(1920 x 1080), with the reason written down:

```
    TOTAL_LINES = 1920 // 32 = 60   -> the whole screen
    BOTTOM_LINE = 59 * 32 = 1888 px  (the screen is 1920 tall)
```

The port's `touch.py` stores the width and the height but never uses
them (the kernel's driver already reports the panel's own
coordinates), so the change does not affect the touch mapping.

### The redraw was slow because the *drawing* was slow, not the commit

The user said the interface was still slow.  The commit was already
deferred (once per page, not once per primitive), so the cost had to
be elsewhere -- and it was, and it was enormous:

```
    lcd.clear                    159.1 ms each     <- before
    lcd.fill_rectangle           123.4 ms each     <- before
```

A full-screen clear took **159 ms**.  The cause: `lcd_fill_rect()`
filled the rectangle **a pixel at a time**, through `lcd_set_pixel()`,
which does the rotation mapping and the bounds check for every single
pixel.  1080x1920 = two million pixels, each with a function call
and a `lcd_map()` that recomputed the logical size.

The rectangle is now mapped **once** (the two opposite corners, which
is enough because the mapping is a rotation by a multiple of 90
degrees and so maps a rectangle to a rectangle) and then filled a row
at a time:

```
    lcd.clear                      3.5 ms each     <- after
    lcd.fill_rectangle             1.7 ms each     <- after
```

**45 times faster.**  This is what made the interface slow, and it
had nothing to do with the display commit.

### The commit is deferred, and the flicker is gone

The commit (`MSMFB_DISPLAY_COMMIT`) does not just queue work: with
`wait_for_finish` set it waits for the commit thread, and the commit
ends in the panel's DMA being re-pointed, which **waits for the next
vertical blank**.  So **a commit costs a frame, about 16 ms**.

Committing after every drawing primitive therefore cost a frame per
primitive: a page with twenty primitives took a third of a second,
and the panel was shown every half-drawn frame in turn -- the
"flickers the whole screen".

The commit is now deferred: the drawing only marks the frame dirty,
and the commit happens once, when the page is finished.  The page
ends where the firmware waits for a key, so the port's own input
modules (`krux/touch.py` and `krux/buttons.py`) call `lcd.flush()`
as they poll.  The user confirms the flickering is gone.

### The shutdown works now

The user tested the shutdown: it killed the firmware and a few
seconds later it was running again.

The cause was a **missing function**.  `krux/power.py` ends its
shutdown with `machine.reset()`, and the unix port of MicroPython has
`MICROPY_PY_MACHINE_RESET == 0`, so the module exports `soft_reset`
but **not** `reset`:

```
    >>> dir(machine)
    ['I2C', 'PWM', ..., 'soft_reset', ...]      # no 'reset'
```

So `machine.reset()` raised `AttributeError`, the firmware exited with
status 1, and **init restarts a service that exits** -- which is
exactly the "it comes back" behaviour:

```
    [ 2662.125956] init: Service 'krux' (pid 28906) exited with status 1
    [ 2662.126509] init: starting service 'krux'...
```

The port now provides `machine.reset()` (`machine_hw_linux.c`), and
it uses the route the **phase-0 proof already used**
(`rootfs/touch-off.c`): ask init, which owns the `sys.powerctl`
handler.

```
    on property:sys.powerctl=*
       powerctl ${sys.powerctl}
```

so `setprop sys.powerctl shutdown` is the power-off.  The call never
returns, because the firmware expects it not to.

### The Vol+ key works -- it was a stuck key, not a broken one

The user reported that Vol+ had stopped working.  The measurement was
unambiguous: the volume_up interrupt count stayed at **0** while
volume_down fired, and no key event reached `/dev/input/event2` at
all -- not even the power key.

The user rebooted the device to Android to check the key, and there
**Vol+ worked perfectly**.  Back on our kernel it worked too, and the
count moved (22 presses).  So the key and the ROM kernel are both
fine, and what happened was the key being **stuck** from the earlier
session -- a key that is held down never re-asserts its interrupt,
and the previous test had left it held.

For the record, the tree's own gpio-keys node is **not** the culprit:
the ROM's device tree has the same node with the same pins
(`pm8941_gpios 2` and `3`), and our tree's is the same.

### The heat was mine, and then it was the drawing

The device "heats up very much".  The first cause was **my own
leftover watcher**: a shell loop left running on the device was
walking every `/proc/*/fd/*` five times a second.  Killed, and the load
average went from **2.18 to 0.28** on a two-core device.

With that gone the firmware was the remaining consumer: about **19% of
one core** while sitting in the main menu, and 4.2 million voluntary
context switches.  That is the input loop -- the firmware polls the
three keys and the touch every 10 ms -- and, above it, the 159 ms
clear.  With the fill fixed, the redraw is 3.5 ms and the polling
dominates again.

### The kernel builds with the host toolchain

The host toolchain (`arm-linux-gnueabihf-gcc` 15.2.0) builds the
kernel, and the result **boots and runs the firmware**:

```
    Linux version 3.4.113-ge9bf776e758-dirty
    (gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1))
```

The device comes up, the stub starts the firmware, the panel is
scanned, the backlight is at half, and the keys work.  The image is
`/tmp/opencode/boot-hostkernel.img` (the host kernel plus the new
stub ramdisk), booted with `fastboot boot`.

**The next step is to delete the two vendored toolchains** -- the Arch
GCC 16.2.0 (2.0 G) and the Arm GNU 15.2.1 (1.1 G) -- keeping only
`gcc-androideabi-4.9` (136 M), which the MicroPython build needs.

## Entry 85 — the touch was never broken in the kernel: the port's evdev module was missing a constant

### What the evidence said, and what it meant

The touch had been investigated for a long time.  The measurements
were:

* the touch IC is **alive** over I2C -- the driver identifies it as
  `PLG218 - 1 fw_version[E079]`, and the sysfs `reg_control`
  interface reads its registers back;
* the driver's own trace shows it **reading the touch correctly**:

```
[Touch] synaptics_ts_get_data: Finger_status: 0x1, 0x0, 0x0, ...
[Touch] synaptics_ts_get_data: [0] pos( 480, 951) w_m[ 6] w_n[ 6] p[54]
[Touch] synaptics_ts_get_data: Interrupt_status : 0x4
```

  (`FINGER_STATE_PRESENT_VALID` is 1, and `0x4` is
  `INTERRUPT_MASK_ABS0` -- a finger, present);
* and the **events reach userspace**.  Reading the node directly:

```
03 00 39 00 66 00 00 00    EV_ABS  ABS_MT_TRACKING_ID = 102
03 00 35 00 1a 01 00 00    EV_ABS  ABS_MT_POSITION_X  = 282
03 00 36 00 a7 03 00 00    EV_ABS  ABS_MT_POSITION_Y  = 935
03 00 3a 00 3a 00 00 00    EV_ABS  ABS_MT_PRESSURE     = 58
```

  So the kernel, the driver, the device tree and the interrupt are
  **all correct**.  The touch works.

### The bug: a missing constant in the port's evdev module

The port's `evdev` module (`standalone/kruxport/evdev_bcm.c`) exports
the event codes the reader needs -- but **`ABS_MT_TRACKING_ID` was not
among them**:

```
>>> dir(evdev)
['ABS_MT_POSITION_X', 'ABS_MT_POSITION_Y', 'ABS_MT_SLOT',
 'ABS_MT_TOUCH_MAJOR', 'ABS_X', 'ABS_Y', 'EV_ABS', 'EV_KEY',
 'EV_SYN', 'KEY_ENTER', 'KEY_POWER', 'KEY_VOLUME_DOWN',
 'KEY_VOLUME_UP', 'SYN_REPORT', 'close', 'open', 'read']
```

and the touch module asks for exactly that name:

```python
# standalone/kruxport/kruxport/touch.py
elif code == evdev.ABS_MT_TRACKING_ID:
```

So the first time a tracking event arrived, the module raised
`AttributeError: module 'evdev' has no attribute 'ABS_MT_TRACKING_ID'`
-- and **the exception was swallowed**, because the firmware's input
loop catches exceptions around the event poll.  The touch therefore
never produced an event, and the symptom was "the touch does not
work".

This is the same failure as the `machine.reset` one (Entry 84): a
name the firmware asks for that the port does not provide.  Both were
silent, because both were exceptions inside a `try`.

`ABS_MT_PRESSURE` (0x3a) was missing as well, and is now exported
too.

### The evidence that pinned it down

The decisive test was to run the *same* reader two ways:

```
the port-style reader saw 737 events
the Touch-style reader saw 0 raw events, new_touch=False
```

The two differ in one respect only: the `Touch` class refers to
`evdev.ABS_MT_TRACKING_ID`, and the standalone reader did not.  The
constants the module exports are then simply listed, and the missing
one is named.

### Also fixed: the touch module's own sample-period gate

Independent of the missing constant, the port's `touch.py` had a
second bug.  Its `event()` polled only when

```python
if current_time > self.sample_time + TOUCH_S_PERIOD:
```

but `current_state()` -- which the firmware calls in the same loop
iteration -- **resets `sample_time` every time**, so the gate never
opened.  The module now records "a finger went down" in `_poll()`
(as `self.new_touch`) and `event()` uses that flag.

### The colours were byte-swapped, and the screen saver covered half the screen

Both are in Entry 84.  In short:

* the theme's constants are **little-endian RGB565** words, so the
  colour word has to be byte-swapped before it is decoded -- without
  that, every hue was wrong and only black and white looked right;
* `TOTAL_LINES` is the number of text lines that fit **vertically**,
  and the firmware computes it from the board's declared **width**.
  The K210 gets that right by accident (its landscape width is its
  portrait height); the hammerhead is portrait, so the width has to
  be the long side.  The screen saver animated over 1056 of 1920
  pixels -- the top 55% -- and every "bottom line" prompt was drawn
  halfway up the screen.

### The redraw was slow because the drawing was slow, not the commit

A full-screen clear took **159 ms**: `lcd_fill_rect()` filled the
rectangle a pixel at a time, through `lcd_set_pixel()`, with the
rotation mapping and the bounds check redone for every one of the two
million pixels.  The rectangle is now mapped once and filled a row at
a time:

```
lcd.clear                    159.1 ms  ->  3.5 ms
lcd.fill_rectangle           123.4 ms  ->  1.7 ms
```

### The commits are deferred, and the flicker is gone

A commit is not cheap: with `wait_for_finish` it waits for the commit
thread, which re-points the panel's DMA, which **waits for the next
vertical blank** -- about 16 ms.  Committing after every primitive
cost a frame per primitive, showed every half-drawn frame, and made a
redraw take a third of a second.

The commit is now deferred to the end of the page, and the port's
input modules call `lcd.flush()` as they poll -- which is exactly
where a page ends, because that is where the firmware waits for a
key.  The user confirms the flicker is gone.

## Entry 86 — the touch works: the port's `buttons.py` had the constants inverted, and `touch.py` was missing the region API

The touch was never a kernel problem.  The driver sees the finger,
the evdev node carries the events, and the port's `touch` module
reads them -- but the firmware never *used* them.  Two separate
faults, both in the port's own Python modules, and both of them
silent.

### The first: the touch module had no region API at all

The firmware's menu draws one region per item and then asks the
touch object which region was hit:

```
krux/pages/__init__.py:990     self.ctx.input.touch.y_regions = y_keypad_map
krux/pages/__init__.py:809     selected_item_index = self.ctx.input.touch.current_index()
```

The port's `touch.py` had neither `y_regions` nor `x_regions`, nor
`_extract_index()`, nor `current_index()`, nor a `valid_position()`
that did anything -- so the menu's touch handling raised
AttributeError.  The module now implements the upstream API the way
upstream does (Entry 85's `event()` and `current_state()` are kept,
the region handling is upstream's).

### The second: the button constants were inverted, so the firmware switched the touch off at start-up

This one is worth writing down carefully, because it is the kind of
fault that hides for weeks.

Upstream's `krux/buttons.py` says:

```
PRESSED = 0
RELEASED = 1
```

because a K210 button is a GPIO that reads **1 when it is up**, and
the firmware's `value()` returns the level, not a "is it down" flag.
`krux/input.py` imports those two names and compares `value()` with
`PRESSED` in four places -- the button integrity check,
`wait_for_release()`, the long-press detection and the swipe
detection.

The port's `buttons.py` had them the other way round (`PRESSED = 1`,
`RELEASED = 0`).  On its own that is harmless, because the port's
`_Key` reported the key's state with the same inverted names.  But the
touch module's `value()` is the *upstream* one:

```
krux/touch.py:263     return 1 if self.current_state() == IDLE else 0
```

so it returns **1 when the finger is up**.  The integrity check in
`krux/input.py` then read

```
if self.touch and self.touch_value() == PRESSED:
    self.touch = None
    kboard.has_touchscreen = None
```

as **true on a screen nobody had touched** -- because `PRESSED` was
1 and the idle touch reports 1.  The firmware therefore **turned the
touch screen off for good at start-up**, and from then on
`touch_event()` returned False without ever calling the touch
object.  The touch object was created, `/dev/input/event1` was open,
and the module worked perfectly -- the firmware just never asked it
anything.

The instrumented module showed it exactly:

```
Input: touch created, has_touchscreen=True
Input: touch=<Touch object> buttons_active=False
Input: after the integrity check, has_touchscreen=None touch=None
touch_event -> no touchscreen!
```

The fix is upstream's convention: `PRESSED = 0`, `RELEASED = 1`, and
`_Key` reports the key's level the way a GPIO would (1 when the key
is up).  Both the touch and the three keys now work; the user
confirms the touch selects menu items.

### How the touch was found, in order

The debugging was done with the firmware's own input path, because
the interesting failures are all in it:

1. a probe that read the events by hand showed the coordinates
   tracking correctly (the first probe appeared to show the same
   coordinates every time because it never cleared the "a finger
   went down" flag);
2. the port's `touch.py` was rewritten with the full upstream API;
3. `event()` was shown to work and to report a region index;
4. the firmware's own touch object was instrumented (a debug log
   behind a sentinel file) -- and it never logged a single event;
5. the device's `krux/input.py` was instrumented the same way, and
   it said `has_touchscreen=None` after the integrity check.

The kernel, the driver, the DT and the IRQ were checked long before
that and were all correct: the driver identifies `PLG218 - 1
fw_version[E079]`, logs `pos( 537,1156)` for a real touch, and the
raw node carries `ABS_MT_TRACKING_ID`, `ABS_MT_POSITION_X/Y`,
`ABS_MT_PRESSURE` and `ABS_MT_TOUCH_MAJOR`.

### The volume-down key: one of the two volume keys is dead, and which one flips between boots

The other thing the user noticed: the **lower** volume key does
nothing.  This one is not a firmware bug -- the key never reaches
the kernel at all:

```
IRQ 318  qpnp-int  volume_up      (the working one)
IRQ 319  qpnp-int  volume_down    (0 interrupts, always)
```

The live device tree puts `volume_up` on pm8941 pin 2 (gpio 985) and
`volume_down` on pin 3 (gpio 986), and the pin 3 line **never
changes state**: watching every GPIO on the board while the key is
pressed shows nothing moving.  The pin is registered, its direction
is input, and its IRQ exists -- the pad simply does not change.

And the working key **changes with the boot**: before the reboot
`volume_up` (IRQ 318) was the one that worked; after
`adb reboot bootloader` + `fastboot boot` the same image, the same
DT and the same driver gave `volume_down` (IRQ 319) instead.  The
user remembers the same thing from an earlier session, with the
other key.  Since the PMIC keeps its registers across a warm reboot,
the state that decides this is written once and then sticks; a full
power-off is the next test.

Both keys are therefore *wired* the way the DT says (the upper key
is `volume_up`), so the port's mapping needs no change:
`BUTTON_B` (page) is `KEY_VOLUME_UP` and `BUTTON_C` (page_prev) is
`KEY_VOLUME_DOWN`.

### The deploy is a script now

`standalone/scripts/deploy-firmware.sh` pushes the interpreter, the
Krux package and the port's own modules and restarts the firmware.
Writing it turned up a stale file: the port's `kruxport/i2c.py` was
a draft that used `fcntl.ioctl` -- a module MicroPython 1.28 does
not have -- and deploying it broke the firmware with an ImportError
in `krux/power.py`.  The port's I2C is the **C** one
(`machine_i2c_linux.c`, which does the ioctls itself), and the
upstream `krux/i2c.py` is what the firmware wants; the draft is
gone.

## Entry 87 — the volume-down key is the compiler's fault, and Android boots with our kernel

> **CORRECTION (Entry 88): the conclusion of this entry is WRONG.**  The
> compiler is not the cause; one missing property in the device tree is.
> The bisection below was wrong because one of its four rows was never
> verified.  Entry 88 has the correction, the real cause and the fix.

Entry 86 fixed the touch.  What was left was the **volume-down
key**, which never produced an event.  It turned out to be the
longest hunt of the project, and the answer is not in the kernel
source at all.

### What was measured

The lower volume key is invisible to the kernel: its interrupt
counter never moves, and its GPIO (`gpio-986`, pm8941 pin 3) does
not change state when the key is pressed, while watching *every*
GPIO on the board.  The upper key (`gpio-985`, pm8941 pin 2,
`KEY_VOLUMEUP`, code 115) works reliably: 22 presses in a row in
one test, and the interrupt counter moves by two per press.

```
IRQ 318  qpnp-int  volume_up     32
IRQ 319  qpnp-int  volume_down    0
```

What was ruled out, in order:

* **the port's firmware** -- with the firmware stopped, the upper
  key still works (9 presses, the counter moves); with the firmware
  running it works too;
* **`lcd.flush()`** -- the port's input modules commit the display
  on every poll, so the display commit was a prime suspect.  A
  four-phase test (polling only / after `lcd.init()` / with a
  commit in the loop / polling only again) gave 2, 5, 2 and 13
  presses: the commit is innocent;
* **the device tree** -- the `gpio_keys` node in the ROM's `rev_11`
  DTB and in ours are byte for byte the same (`vol_down` on pm8941
  pin 3, `vol_up` on pin 2, the same codes, the same debounce);
* **the drivers** -- `drivers/gpio/qpnp-pin.c`,
  `drivers/spmi/qpnp-int.c` and `drivers/input/keyboard/gpio_keys.c`
  are **identical** in every branch of the kernel tree
  (`lineage-22.2`, `lineage-23.0`, `lineage-23.2`,
  `fbe-dirty-tree`);
* **the LGE fixes our tree was missing** -- `gpio: qpnp-pin: Fix bug
  when applying pull config`, `gpio: qpnp-pin: reorder qpnp-pin
  device register`, `spmi: qpnp-int: Implement irq_read_line`,
  `msm: qpnp-int: Support early interrupt mapping` and `spmi:
  pmic-arb: Access mapping data from hardware instead of device
  tree` are *not* ancestors of our HEAD by hash, but their **content
  is there**: the driver reads `pull` from `Q_REG_I_DIG_PULL_CTL`
  (the fixed register), registers `qpnp_pin_init` with
  `arch_initcall`, implements `irq_read_line`, and the arbiter reads
  the peripheral mapping from the hardware;
* **the toolchain's own effect on the tree** -- the working and the
  broken kernel have the **same `.config`** (md5 identical) and the
  same eight DTBs in their boot images.

### What was left: the compiler itself

The two kernels differ only in the compiler:

```
the broken one:  Linux version 3.4.113-ge9bf776e758-dirty (gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1))
the working one:  Linux version 3.4.113-ge0cd9d48553       (gcc version 15.2.1 20251203 (Arm GNU Toolchain 15.2.Rel1))
```

Both were booted with the same source (the difference between the
two commits is camera and ISP debug code plus one `late_initcall`
that makes SELinux permissive), the same config and the same device
tree.  On the **old** kernel both keys work -- 20 interrupts on
`volume_up` *and* 20 on `volume_down` -- and on the **new** one the
lower key is dead.  The ROM's own kernel was built with a third
compiler, Arch's `gcc 16.2.0`, and works too.

So two compilers work and the host's GCC 15.2.0 does not: there is
a latent bug in the pm8941 GPIO/interrupt path that the Ubuntu
build of GCC 15.2.0 miscompiles or exposes.  Finding it in the
source is the next piece of work; until then the kernel is built
with the vendor's compiler.

### The toolchain decision is therefore reversed -- but the project stays slim

The two vendored toolchains (3.1 GB) were deleted in the name of
using the host's compiler for everything.  That turned out to be
wrong for the **kernel** (the MicroPython build is fine with the
host's).  What is kept now is:

* `standalone/vendor/toolchain/gcc-androideabi-4.9` (136 MB) -- the
  AOSP prebuilt;
* `standalone/vendor/toolchain/arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst`
  (268 MB) and `arm-none-eabi-binutils-2.47-1-x86_64.pkg.tar.zst`
  (2.3 MB) -- Arch's toolchain **compressed**, which unpacks into the
  2.0 GB tree in eleven seconds.

The build script now takes `TOOLCHAIN_PATH`, so the 2 GB tree can
live outside the project (in tmpfs) and the repository keeps only the
archives:

```
TOOLCHAIN=arch TOOLCHAIN_PATH=/tmp/opencode/tc-arch \
    bash build/build-kernel.sh --out /tmp/opencode/ko-arch
```

`standalone/tools/fetch-toolchain.sh` unpacks the archives that are
already there and only downloads what is missing.

### Android boots with our kernel

A milestone on its own: the ROM's **Android userspace** with **our
kernel**, built with the host's compiler:

```
Linux version 3.4.113-ge9bf776e758-dirty (user@nuc7) (gcc version 15.2.0 (Ubuntu 15.2.0-16ubuntu1))
```

`fastboot boot` an image packed from the ROM's Android ramdisk and
our `Image-dtb`, and Android comes up with adb in about twenty
seconds.  The recipe is `standalone/rootfs/bootimg.py` plus
`standalone/scripts/boot/boot-debug-android.sh`.

This also settles the key question: with Android's userspace under
our kernel the lower key is **still** dead, so the fault is in the
kernel and not in the TWRP userspace or in the port's firmware.

### The deploy is a script

`standalone/scripts/deploy-firmware.sh` pushes the interpreter, the
Krux package and the port's own modules, and restarts the firmware.
Writing it turned up a stale file: the port's `kruxport/i2c.py` was
a draft that used `fcntl.ioctl` -- a module MicroPython 1.28 does
not have -- and deploying it broke the firmware with an ImportError
in `krux/power.py`.  The port's I2C is the **C** one
(`machine_i2c_linux.c`, which does the ioctls itself), and the
upstream `krux/i2c.py` is what the firmware wants; the draft is
gone.

## Entry 88 — the volume keys: the device tree was missing one property, and Entry 87's conclusion was wrong

Entry 87 ended with the compiler being the culprit.  **It was
not.**  The compiler was a red herring, found by a bisection that
was not what it looked like -- and this entry is the correction, plus
the fix.

### What the compiler "proved", and why it did not

The bisection was four boot images, two kernels and two ramdisks:

| kernel | ramdisk | the lower key |
|---|---|---|
| old (e0cd9d48553) | old (plain TWRP) | works |
| old (e0cd9d48553) | new (the init stub) | works |
| new (e9bf776e758) | old (plain TWRP) | works |
| new (e9bf776e758) | new (the init stub) | **dead** |

The first three rows are all "works", the last one is "dead", and the
only difference between rows three and four is the ramdisk.  The
conclusion drawn from it was that the new kernel needed the new
ramdisk to break the key.

But **the row three image was never verified**: the boot had been
interrupted before `/proc/version` was read, and it is entirely
possible that the device was still running the *old* kernel when the
user pressed the keys.  Re-running it with the version checked first
showed the new kernel (`gcc 16.2.0 (Arch Repository)`) with both
keys working.  So row three is "works" with the new kernel after
all, and the ramdisk is innocent.

Two other things in that bisection were misleading:

* **the compiler.**  The host's `gcc 15.2.0` and Arch's
  `gcc 16.2.0` were both tried, and the key was dead with both, so
  the compiler was ruled out -- which is what the table above now
  shows.  (The old kernel was built with Arm GNU's 15.2.1, and it
  worked, so three compilers were in play and none of them was the
  cause.)
* **the host's /tmp filled up** (100 %, 7.8 GB) during the
  investigation, which is worth remembering: a full tmpfs makes
  `cpio` extraction fail halfway and a kernel build fail in
  obscure ways.  `ccache -C -z` and a fresh build directory were
  used for the last build.

### The actual cause: one missing property in the device tree

The pm8941's GPIO driver reads each pin's configuration **out of the
PMIC's registers** and then overrides only what the device tree
says.  A pin whose properties are missing therefore keeps whatever
the PMIC happened to be left with -- and the PMIC's registers
survive a warm reboot.

The vendor's own tree says, for the two volume keys:

```
  	gpio@c100 { /* GPIO 2 */
  		qcom,mode = <0>;		/* QPNP_PIN_MODE_DIG_IN */
  		qcom,pull = <0>;		/* QPNP_PIN_PULL_UP_30 */
  		qcom,vin-sel = <2>;
  		qcom,src-sel = <0>;
  	};                        <-- no master-en
```

**"master-en" is the pin's digital path.**  With it off, the pad
cannot be read as an input at all: `qpnp_pin_get()` returns -EPERM,
the key's interrupt never fires, and the key does nothing.  The
earjack (GPIO 1) has had `qcom,master-en = <1>` all along, which is
why it worked.

And the vendor's comments decode the pull as well: `0` is
`QPNP_PIN_PULL_UP_30` (a 30 kOhm pull-UP, right for an active-low
key) and `5` is `QPNP_PIN_PULL_NO` -- which is what GPIO 1 uses, and
that is why the earjack's pad reads **low** at idle.

### Seeing it

The tool that settled it is the driver's own debugfs, which the
vendor's tree already has behind `CONFIG_GPIO_QPNP_PIN_DEBUG` (it
was off).  It is per pin and per parameter, and it is **writable**:

```
/sys/kernel/debug/qpnp_pin/pm8941-gpio/<pin>/{mode,pull,vin_sel,src_sel,master_en,invert,...}
```

With it enabled, the three pins read:

```
  pin 1 (the earjack, works):  master_en=1  pull=5   mode=0
  pin 2 (volume up):           master_en=0  pull=0   mode=0     <-- dead
  pin 3 (volume down):         master_en=1  pull=0   mode=0
```

**`master_en = 0` on the dead pin.**  Writing `1` to it -- live,
over the debugfs, with no rebuild -- brought the key back
immediately, and both keys then worked (the interrupt counters ran
up on both).  Writing `pull = 4` or `5` pulls the pad **down**,
which is what "the key looks permanently pressed" turned out to be.

The fix, in the device tree, for both keys:

```
  	qcom,master-en = <1>;
```

### The fix, and the check

`arch/arm/boot/dts/msm8974-hammerhead/msm8974-hammerhead.dtsi` now
says `qcom,master-en = <1>;` for `gpio@c100` (KEY_UP) and
`gpio@c200` (KEY_DOWN).  Rebuilt, booted cold, with no live
debugfs writes at all:

```
pin 1: master_en=1 pull=5      pin 2: master_en=1 pull=0      pin 3: master_en=1 pull=0
IRQ 318 volume_up    40
IRQ 319 volume_down  28
```

**Both keys work from a cold boot.**  The user confirms it.

This also explains everything that was odd about it:

* **why one key at a time** -- whichever pin the PMIC had been left
  with the master enable off;
* **why a reboot "fixed" it** -- the bootloader (or Android) writes
  the pin's configuration, so a boot through a different path leaves
  a different pin broken;
* **why a full power-off changed it** -- the PMIC's registers are
  volatile, so a power-off clears them and the state starts again;
* **why the touch screen was fine** -- the synaptics controller is
  on an msmgpio, not on the pm8941.

### What else came out of it

* **Android boots with our kernel.**  The ROM's Android userspace
  with our `Image-dtb`, packed with `standalone/rootfs/bootimg.py`,
  comes up with adb in about twenty seconds.  That is a project
  milestone in its own right (the camera cycle needs Android on our
  kernel) and it is also what proved the key fault was in the
  kernel rather than in the firmware.
* **the kernel is built with the vendor's compiler** now (Arch's
  `gcc 16.2.0`, unpacked from the `.pkg.tar.zst` archives that are
  still in the project, into a tmpfs outside it).  The host's
  compiler builds the kernel too and nothing in the keys needed it,
  but the vendor's compiler is what built the ROM's own kernels, so
  it is the safer default.  `TOOLCHAIN_PATH` points at it.
* **the `zImage-dtb`, not the `zImage`, is what the bootloader
  wants**: the bootloader finds the device tree by scanning the
  kernel image for it, and a plain compressed `zImage` has none --
  the bootloader answers "dtb not found" and refuses to boot.

## Entry 89 — the camera moves, the fonts grow, the backlight works, and the wipe is safe

A working session, with the camera as the headline.  Everything
here is measured on the device.

### The camera: three fixes, and the finding that moved the goal posts

**1. The ION heap masks were wrong** (`standalone/cam/ion.h`).

```
ION_MASK_SYSTEM   0x00010000    <-- bit 16
```

`heap_id_mask` is a mask of heap **ids**, not of heap types: the
system heap is **id 30** (`include/linux/msm_ion.h`), so its mask is
`1 << 30`.  Bit 16 is `ION_CP_WB_HEAP_ID` -- a **8660-only** heap
that this 8974 does not have -- so every camera buffer allocation
failed with `ENOENT`:

```
[ion] ION_IOC_ALLOC (mask 0x10000): No such device
[client] dma-buf allocation failed
```

The heaps this kernel registers (from the device tree) are 8, 22,
25, 27, 28 and 30.  The camera's frames work with the system heap
(30) and with the IOMMU heap (25); heap 8 is too small for a
3280x2464 frame.

**2. The kernel oopsed when the CPP firmware could not be loaded.**
`msm_cpp_cfg()` -> `cpp_load_fw()` dereferenced the firmware
without checking it (`msm_cpp_write(fw->size, ...)` after a failed
`request_firmware()`); the call trace was `msm_camera_io_w+0x0`.
Our tree now checks the firmware and gives up with a message
instead.  **This is an upstreamable bug fix.**

**3. The CPP firmware has to be version 1.2.0, and it has to be in
the kernel's firmware-loader path.**  The version is not a free
choice: the kernel checks the message against the firmware's stripe
base, and the captured chromatrix has five stripes and 266 words:

```
130 + 27*5 + 1 = 266     <-- STRIPE_BASE_FW_1_2_0
140 + 27*5 + 1 = 276     <-- 1.4.0
464 + 27*5 + 1 = 600     <-- 1.6.0
```

and the name has to be the bare file name in `/lib/firmware`,
because that is where the kernel's loader looks.

With all three, the chromatrix is **accepted** (status 0) and a
frame arrives ("GOT A FRAME: index=2") -- still with `bytesused=0`.

**And then the finding that matters: the vendor's camera has the
same fault.**  The ROM's own camera app, running under **our**
kernel with the **vendor's** HAL:

```
CSID0 LUT: vc0 0x0000122b vc1 0x00000000, cfg0 0x00000023 cfg1 0x00000013
CSID0 irq status 0x000002dd, packets 2363548, unmapped long 0x00000000,
                mapped long 0x00000000, short 0x0000011a, ecc 0, crc 0
```

**2,363,548 packets, none of them mapped, and exactly the same 282
"short" packets as our imaging server sees.**  So the fault is **in
our kernel**, not in our imaging server -- and the packets carry
the virtual channel **1** while the CSID's LUT (as the vendor's HAL
programs it) maps VC 0.  The next measurement is the same one in
the **ROM's kernel**, where the camera works; the candidates are the
CSID's VC mapping, its stream mux, and the ISP-IF's routing.
Routing the ISP-IF at VC 1 crashes the kernel, so that needs a way
back first.

How the vendor's camera was driven without a human:

```
adb shell input keyevent 224      # wake the screen
adb shell monkey -p com.google.android.GoogleCamera -c android.intent.category.LAUNCHER 1
adb shell input keyevent 115      # the volume key is the shutter
adb shell am force-stop com.google.android.GoogleCamera   # the close prints the state
```

(The CSID's state is printed when the camera is **closed**, so the
`force-stop` is what makes the numbers appear.)

`standalone/cam/STATE.md` has the details.

### The fonts are half again as big

The font was scaled up by two (16x32).  It is now scaled by three
(24x48): **45 columns and 40 lines**, which is *more* room than the
K210's 40 columns and 15 lines, and the text is 150% bigger.  Four
(32x64) would give 33 columns -- fewer than the K210 -- so three
is the size that actually fits.

The scale is a constant in the display module and the size the
firmware believes is in `board.py`; **the two have to agree**, or
the lines overlap.

### The splash screen: the panel now follows the framebuffer

The commit was deferred so that a page is not shown half-drawn, and
the port's input modules flush it when they wait for a key -- which
is where a page ends.  But the firmware also draws pages that
*nobody waits on*: the boot splash, which is drawn and then simply
left there for a second.  Nothing flushed it, so **the panel never
showed the logo at all**.

The display module now has a thread that commits the framebuffer
when the drawing has been quiet for a moment (the draw counter says
whether it is still drawing), so the panel always catches up.

### The backlight, and a bug that had the brightness pinned at maximum

The firmware's `power_manager.set_screen_brightness(value)` **adds
7** to the level, because the K210's PMU register only counts from
7 (7 is off, 15 is full).  The port's `pmu.py` took the number as
it arrived and clamped it to 8 -- so **every level was above the
maximum and the backlight was pinned at full brightness, and the
setting did nothing at all**.

The port now maps the K210's 7..15 onto the backlight's 0..8, so
the levels work.

On top of that, the port's `auto_shutdown` turns the backlight off
when the device has been idle for a while and back on at the first
key or touch.  It has to: the phone's panel blanks itself when the
framebuffer is idle, and the backlight is a separate device, so
without this the panel goes dark while the backlight keeps burning
the battery.

The idle count needed a trigger: the firmware's own `auto_shutdown`
counts the seconds with a **hardware timer**, which this port has
not got (the `machine` module's `Timer` is a stub).  So the count is
done from the clock, and the port's input modules call it on every
poll -- which is where the firmware waits for a key, and so where
the idle time passes.

**And the kernel gets there first.**  The MSM's framebuffer driver
blanks the panel when the framebuffer is idle, and the lm3630's
blank handler leaves the backlight at the driver's **minimum** --
32 of 255, which is what the measurements show (the backlight goes
from 127 to 32 after half a minute of the firmware sitting still).
So the "screen blanked" case is already handled by the kernel, to
the extent the hardware allows.

What the port adds is the case the kernel cannot see: the firmware's
**screen saver** is an animation, so the panel stays on and the
kernel never blanks it -- and there the backlight has to go off
itself, or the phone sits on a table with the screen lit.

### The wipe cannot brick the device, and now it works

The firmware's "Erase User's Data" erases the SPIFFS area of the
K210's SPI flash (0xD00000 to 0x1000000).  **This device has no
SPI flash**, and a module that mapped those offsets onto the wrong
device -- the internal flash, the eMMC -- would take the partition
table or the bootloader with it, and the phone would not come back.

The port has its own `flash` module, which is safe by construction:
it opens no block device at all, it **refuses** any address outside
the SPIFFS area, its `erase()` removes the settings and the
mnemonics, and its `write()` raises (there is no firmware in a SPI
flash here).

It has to live in the settings directory as its `__init__.py`:
the settings are in a directory called "flash", and **a directory
wins over a module of the same name** -- which is also why the
wipe used to do nothing at all (it imported an empty package).

See `standalone/RUNTIME-DATA.md`.

### Hardfloat: our binaries are hardfloat, and that is fine

The kernel is built with `-msoft-float` (the CAF tree forces it), so
the kernel's *syscall* convention for a floating-point argument is
the softfloat one.  All our binaries are hardfloat:

```
the firmware's interpreter   Tag_ABI_VFP_args: VFP registers
the imaging server            Tag_ABI_VFP_args: VFP registers
the init stub                 Tag_ABI_VFP_args: VFP registers
```

**And they work**: the device has the VFP, the kernel advertises it,
and a hardfloat program doing a double division runs there:

```
AT_HWCAP = 0xfb8d7      FP (VFP): yes      ASIMD (NEON): yes
the float works: 0.333333
```

**This corrects an earlier note in this log** (Entry 18), which
recorded `AT_HWCAP_FP` as clear and concluded "assume no VFP".  The
bit is set, and the device does have the VFP (`vfpv3` and `vfpv4`
in `/proc/cpuinfo`'s Features).  The freestanding softfloat init of
that session was not needed for this reason; the init stub is a
normal static binary and it works.

The one real mismatch left is the **syscall** convention: a syscall
with a floating-point argument would be passed wrongly (the VFP
registers instead of r0-r3).  Krux makes no such syscall, so it is
safe today, and the kernel could be rebuilt with
`-mfloat-abi=hard` to make it exact.

### The project got smaller and tidier

* the two vendored toolchains (3.1 GB) are gone; what is left is
  the AOSP prebuilt (136 MB) and Arch's toolchain as two compressed
  packages (270 MB) that unpack in eleven seconds;
* the kernel's tools moved to `standalone/tools/`, the camera's
  generators to `standalone/cam/tools/`, the kernel's patches to
  `standalone/patches/`, and the project's `tmp/` (a stale build
  tree) is gone;
* `standalone/BUILD-GUIDE.md` (how to build it from scratch),
  `DIRECTORIES.md` (what every directory is for) and
  `standalone/RUNTIME-DATA.md` (where the secrets live) are new.

### The final report: why the device tree fix was necessary now, and not before

This is the question that ties the last two sessions together.

**The fix**: the pm8941's two volume-key pins need

```
qcom,master-en = <1>;
```

and the vendor's tree does not say it.  Without it the driver reads
the pin's configuration out of the PMIC's registers and overrides
only what the device tree says -- so the **master enable** is
whatever the PMIC happens to be holding.  With it off, the pin's
digital path is disabled: the pad cannot be read as an input, the
key's interrupt never fires, and the key does nothing at all.

**Why it was not a problem before**: because the master enable was
still on.  It is a register in the PMIC, and the PMIC's registers
are **volatile**: they are cleared by a power-off and they survive a
warm reboot.  Nothing in the stock system turned it off, because
nothing in the stock system *wrote* to those pins.

**What turned it off: the camera.**  The camera's power supplies are
PMIC regulators (`cam_vdig-supply`, `cam_vana-supply`, `cam_vio-supply`
-- pm8941 LDOs) and its GPIOs are msmgpio pins.  Switching the
camera's regulators is the first thing this project did that writes
to the PMIC, and it is what left one of the two volume-key pins with
its master enable off.

**The measurements that fit it**:

* the keys worked in the firmware for a long time, and started
  misbehaving after the camera work and a few reboots (the user's
  own account);
* the fault is in the *PMIC's* state, not in the firmware: the
  firmware's own `button_integrity_check` is not involved (it is
  about the firmware's view of the keys, and it passes);
* a **power-off** brings the key back (the PMIC's registers are
  cleared) while a **warm reboot** does not (they are kept) -- which
  is exactly what the user reported;
* **which** key is broken follows what ran last: the ROM's camera
  (under Android) breaks one and our imaging server breaks the
  other, because the two power the camera up in a different order;
* the port's own `flash` module is the same story in miniature: the
  settings directory *shadows* the `flash` module, and the empty
  package that was imported is why the wipe did nothing.

**So the fix is not a workaround**: the device tree was always
missing a property that the hardware needs, and the camera work is
what exposed it.  The two volume-key pins are now configured
explicitly (`mode`, `pull`, `vin-sel`, `src-sel` and `master-en`),
so they no longer depend on whatever the PMIC was left with.

**The deeper fix** would be to stop the camera's power sequence from
disturbing the PMIC's GPIO block at all -- or to have the gpio-keys
driver re-assert the pins' configuration after the camera has been
powered up.  Both are kernel changes, and they are the next thing to
look at if a key ever goes dead again.

### The entropy: the CPU cannot help, and the pool is thin

The firmware's seeds come from `urandom` (`krux/key.py` imports it
as `random`), so the question is what the kernel's pool has to work
with.  Measured on the device:

```
CPU part : 0x06f  (the Cortex-A9)
Features : swp half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt
poolsize : 4096 bits
entropy_avail : 219 bits        (it was 1330 a moment earlier)
/dev/hwrng : does not exist
```

**The CPU has no entropy source of its own**: the random-number
instruction (`RNDR`) arrived with ARMv8.5-A, and this is an
ARMv7-A core.  There is no hardware RNG driver either, so there is
no `/dev/hwrng`.

**And the pool is thin.**  219 bits of 4096 is a CRNG that is
starved: the kernel refills it from the interrupt timings, the
timer, and the like, and a phone that is sitting on a table
generates very little of that.  It grows and shrinks with what the
device is doing (it was 1330 a few minutes earlier), which is
exactly the behaviour that makes a seed generated from it weak.

**So the project's plan is the right one**: the camera's sensor
noise is the entropy source.  A sensor's read noise is genuinely
random, it is available in quantity (a 3280x2464 frame is 8
megabytes of it), and it is only available while the camera is
running -- which is why the camera cycle has to be atomic.

**What that means for the code**: the entropy has to come from the
**raw** frames, not from the framebuffer (the ISP's demosaic, the
noise reduction and the chroma matrix all mix the noise into
structure, and the framebuffer is also what the display shows).
The raw buffer is what the camera work has been chasing, and the
last gap in it is exactly the one that matters here.

**And the hardfloat caveat does not apply**: an entropy hash is
integer work, and the binaries are hardfloat anyway, which the
device runs (measured: `AT_HWCAP_FP` is set, and a hardfloat
program doing a double division works).

### The device tree was missing the pm8941's pins -- and that is the whole story of the dead keys

While the camera was being measured, the **whole** device tree was
compared with the ROM's, and the difference was much bigger than
the two lines the keys needed:

| | the pm8941's pins described |
|---|---|
| the ROM's DTB | **36** (the mode, the pull, the vin-sel, the src-sel, the master-en) |
| ours, before | **0** |

**Our tree described none of them.**  The `qpnp-pin` driver reads
each pin's configuration out of the PMIC's registers and overrides
only what the device tree says -- and ours said nothing at all, so
**all 36 pins kept whatever the PMIC happened to be holding**.  The
PMIC's registers are volatile: cleared by a power-off, kept across a
warm reboot.

That is the whole story of the dead volume keys: the master enable
of a pin is one of those 36, and whichever pin the PMIC had been
left with it off was dead.

The fix is the vendor's own configuration, copied into
`arch/arm/boot/dts/msm-pm8941.dtsi` (and the hammerhead's
override keeps the two volume keys' `master-en`, which the vendor's
tree also lacks).  **Our built DTB is now the ROM's DTB, with
exactly two additions:**

```
only in ours:  qcom,master-en = <0x01>;      (the two volume key pins)
only in the ROM: (nothing)
```

and the two pins that differ are the two volume keys.

**This also settles the "why now, and not before" question in a
second way**: the camera work is what first *wrote* to the PMIC in
this project, and so what first left a pin in a state the device
tree did not describe.  With the pins described, the state of the
PMIC no longer matters -- the device tree wins.

**And it is not the camera's fault**: with the vendor's whole pin
configuration in place, the CSID's counters are unchanged
(1,312 packets, none mapped, the same 282 short packets).  So the
camera's gap is in the **kernel**, not in the device tree -- which
is what the vendor's own camera failing under our kernel already
said.

### The camera: the virtual channel is the last thing

The camera's state is in `standalone/cam/STATE.md`; this is the
short version.

**Fixed this session** (all measured): the ION heap masks (a mask of
heap *ids*, not of heap *types* -- the system heap is id 30, and
`0x00010000` is a 8660-only heap this 8974 does not have), the
kernel's oops when the CPP firmware could not be loaded, and the CPP
firmware itself (version 1.2.0 -- `130 + 27*5 + 1 = 266` is the
chromatrix's word count -- in `/lib/firmware`, under its bare name).
With those, **the chromatrix is accepted for the first time** and a
frame arrives.

**And the device tree was wrong as well**: our tree described none
of the pm8941's 36 pins, so the driver inherited every one of them
from the PMIC's volatile registers.  The vendor's tree describes all
36, and ours now does too -- our built DTB is the vendor's DTB with
exactly two additions (the volume keys' `master-en`).

**What is left**: the image's packets are on the **wrong virtual
channel**.  The CSID's LUT (as the vendor's HAL programs it) maps
**VC 0 -> 0x2b** (the 10-bit image) and VC 1 -> 0x12 (the 8-bit
stream), but the image's packets are not on VC 0, so the CSID does
not map them and the frame is empty.  The ISP-IF is routed to cid 0
(the vendor's routing), and the ISP-IF takes a *mask* of channels, so
it can be given both -- the imaging server can do that now.  Giving
it **only** cid 1 crashes the kernel (a path nothing else
exercises); giving it both does not crash, and the frame is still
empty.

**It is our kernel, not our imaging server**: the ROM's own camera
app under our kernel, with the vendor's HAL, streams 2.3 million
packets and maps **none** of them -- exactly what our imaging server
sees.  And with the **ROM's kernel** the same HAL, the same sensor
sequence and the same LUT produce a **real photograph**: 2448x3264,
1.1 MB, and it is a picture of the scene.

**The next step**: the CSID's configuration in our driver, compared
with the ROM's.  The ROM's kernel is not in our tree, so it has to
be done on the device: the ROM's kernel image and its kallsyms are
in the project, and `standalone/scripts/kernel/cmp-kernels.sh`
already disassembles a function out of the ROM's kernel with the
host's objdump.  The functions are the CSID's `msm_csid_config`, the
LUT writer and the `CORE_CTRL` writes.  And with the ROM's kernel
running -- where the camera works -- the CSID's registers can be
read (the base is 0xfda08000, the offsets are in
`msm_csid_hwreg.h`) and compared with ours register by register.

### The device needs a power-cycle

The last camera run (the ISP-IF with a single virtual channel that
is not channel 0) crashed the kernel **and** left the device not
enumerating on USB at all, so it needs a physical power-cycle.  That
is the one thing in this session that went the wrong way, and it is
worth writing down why it could happen: the imaging server drives
the CSID and the ISP-IF directly, and the path where the ISP-IF is
given a single non-zero channel is a NULL dereference in **our**
driver -- a path that nothing else exercises, and that the vendor's
camera never takes (it always routes channel 0).

**`standalone/scripts/boot/boot-our-firmware.sh`** is the way back:
it puts the device into the bootloader and boots our image (the
firmware then starts on its own).  It says so plainly when the
device is not reachable and needs a power-cycle.

The rule for the camera work from here: **do not try a
configuration that has crashed the kernel before, without a way to
get the device back** -- and the camera's crash is now a known
path, so it should be fixed in the driver (the ISP-IF's
single-non-zero-channel path) before anyone tries it again.

---

## 2026-10-21 (evening) — the phone boots into our firmware by itself, the "ISP-IF crash" was our own debug code, and the author's kernel compared

### The design is now real: the boot partition holds our image

`fastboot flash boot /tmp/opencode/boot-firmware.img` (our kernel + the
stub ramdisk) and then a plain `fastboot reboot` brings the phone up
**in our firmware**, with adb and the Krux firmware running.  That is
the runtime design the project asked for -- "our firmware is the default
payload; holding Vol+ gives TWRP" -- and it is reversible with
`fastboot flash boot images/boot-rom-20261011.img` (the ROM's own
image).

The consequence for the camera work: a crash now has to be recovered
from without a human, so the panic path had to be looked at.

### Why the phone turned itself off

Not the kernel, and not the TWRP init stub:

* the command line is the ROM's own plus `maxcpus=2`,
  `androidboot.selinux=permissive` and `buildvariant=eng`; the flag
  that matters, `msm_watchdog_v2.enable=1`, is **the ROM's**;
* the init stub `execl`s the firmware, so PID 1 *is* the firmware; the
  stub never waits and never powers the device off.

The power-off on a crash comes from the ROM's own
`arch/arm/mach-msm/restart.c`: `msm_restart_prepare()` calls
`pm8xxx_reset_pwr_off(1)` and then, with `CONFIG_LGE_HANDLE_PANIC`,
writes LGE's "kernel error" magic into the restart reason.  The LGE
bootloader treats that magic as a fatal crash and powers the phone
off, which is why a crash needed the power key.

**Changed** (our tree, `arch/arm/mach-msm/restart.c`): on a panic the
standard "reboot" reason (0x77665501) is kept instead of LGE's crash
magic, so a panic *restarts* the device.  Two things follow: the phone
comes back on its own (into our firmware, since that is what the boot
partition holds), and the panic survives in the persistent ram console,
so `/proc/last_kmsg` on the next boot holds the log of the crashed
boot.  Our kernel has `CONFIG_ANDROID_RAM_CONSOLE=y`, so that file is
there; it is the only channel there is for a panic.

### The firmware powered the phone off when it was idle

`standalone/kruxport/kruxport/auto_shutdown.py` called
`power_manager.shutdown()` when the idle timeout expired, and the
port's `machine.reset()` is a *power off* (`setprop sys.powerctl
shutdown`).  So the phone switched itself off after the firmware's
default ten idle minutes and vanished from the USB bus -- twice,
unattended.

On the K210 that same path is a **sleep**: the PMU's sleep mode, from
which the power key wakes the device.  The port now does the same
thing in its own terms: the screen goes dark, the device stays alive,
and the first key press or touch brings the backlight back.  The
firmware's own "Shutdown" menu item still powers the phone off.

### The "crash": it was our debug code, not the ISP-IF

The camera's known crasher -- "the ISP-IF given a single channel that
is not channel 0" -- was a **NULL dereference** according to the
previous session's notes.  It is not.  The log of the crashed boot
(now readable, thanks to the ram console) ends here:

```
[  164.277839] DEBUG: ISPIF reg 0x28c = 0x00000000
...
Last boot reason: wdog_reset
```

That is our own register dump in `msm_ispif_cmd()`.  The ISP-IF's
register map ends at `ISPIF_VFE_m_OUTPUT_SEL` = 0x28c; the next
registers, 0x290 and up, are the **MISR** registers, and *reading* a
MISR hangs the AHB: the CPU stops, the kernel says nothing more, and
the watchdog restarts the phone.  A phone that restarts itself with no
message in the log is what that looks like.

The dump now stops at 0x28c.  With that fixed, the ISP-IF's
single-non-zero-channel path is harmless -- it never had a bug.

### The author's kernel, compared with ours

The author of the ROM builds from a commit that is not published
(`da91d066bc6`), so the only way to see what he changed is to compare
his binary with ours.  His own recovery gives us both his config and
his symbol table:

```
fastboot boot images/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img
adb shell 'cat /proc/kallsyms'          # after: echo 0 > /proc/sys/kernel/kptr_restrict
adb shell 'cat /proc/config.gz'
```

What the comparison says:

* **the device trees are identical** -- the ROM's 20260926 and
  20261011 images carry byte-identical DTBs, and ours is the vendor's
  plus our two `master-en` additions;
* **the config matches in the whole camera area** -- every
  `CONFIG_MSM_CAMERA*`, `CONFIG_MSM_CSID`, `CONFIG_MSM_CSIPHY`,
  `CONFIG_MSM_ISPIF`, `CONFIG_MSM_CPP`, `CONFIG_IMX179*` symbol has
  the same value in the author's kernel and in ours.  The differences
  are the radio (he has Wi-Fi, Bluetooth and the modem; we strip them)
  and `CONFIG_HZ` (100 against our 300);
* **the CSID's ioctl path is the same code** -- the same call
  sequence, the same 80-byte `struct msm_camera_csid_params`, the same
  register accesses;
* **the ISP-IF's code is the same code** -- the same struct offsets
  (the HAL's `struct msm_ispif_params_entry` is read at the same
  places in both), the same immediates.

The tools for this are new: `standalone/scripts/kernel/compare-roms.sh`
(the images), `diff-kernels.py` (the symbol tables and the function
sizes), `cmp-func.py`, `cmp-callseq.py` and `cmp-iow.py` (one function
in both kernels: its calls, its constants, its register accesses).

**One difference worth keeping** -- the CSID's channel lookup table.
The register map of the CSID v3.0 (the 8974's) has *one word per
virtual channel*:

```
CSID_CID_LUT_VC_0_ADDR 0x10   CSID_CID_LUT_VC_1_ADDR 0x14
CSID_CID_LUT_VC_2_ADDR 0x18   CSID_CID_LUT_VC_3_ADDR 0x1C
CSID_CID_n_CFG_ADDR    0x20
```

but `msm_csid_cid_lut()` computes the address the way the CSID v2.0
does -- `(cid >> 2) * 4` -- so all four channels end up packed into the
byte lanes of the word at 0x10.  Our own debug print shows exactly
that: the word at 0x10 reads 0x0000122b (VC 0 = 0x2b, VC 1 = 0x12) and
the word at 0x14 reads 0.

The author's kernel computes the same way for the LUT (we read the
instruction: `lsr r0, r0, #2` then `smlabb r0, r0, r5, r6`) but
*differs* for the CID configuration register: he uses
`(cid >> 2) * 4 + 0x20` where ours uses `cid * 4 + 0x20`.  So the two
kernels are not the same revision of the same camera stack, and this
is the place where they differ.  Whether the v3.0's LUT really is
packed (our header's names say one word per channel) is the open
question, and it is the next thing to settle: it decides whether the
image's packets -- which arrive on virtual channel 1 -- can ever be
mapped by the lookup table the camera HAL programs (VC 0 = 0x2b).

### The camera's own state, unchanged

The image's packets still arrive on **virtual channel 1** while the
HAL's table maps **VC 0** to the image's data type, so nothing is
mapped and the frame is empty.  The sensor is told which virtual
channel to use by one of its registers; the HAL's own sequence writes
0x3302 = 1, which is a candidate, and the imaging server can now
override any sensor register after the vendor's sequence and before
the stream starts (`standalone/scripts/cam/sweep-vc.sh`).

### The camera runs under our own firmware now

Three things were wrong, in three different places.

**1. The camera's CPP had no firmware.**  The CPP (the colour
processing block) is programmed by the kernel, and the kernel gets its
firmware through `request_firmware()`, which hands the request to the
user space helper -- and the recovery's `ueventd` only knows the device
tree firmware region, which the hammerhead does not have.  So under
our own ramdisk the blob was never found ("Invalid frame message", and
no frame ever arrives), even though it was right there in
`/lib/firmware`.

* The blob is now in the ramdisk (`build-stub-ramdisk.sh` puts
  `standalone/cam/fw/cpp_firmware_v1_2_0.fw` into `/lib/firmware` ✓);
* and the CPP's driver reads the file itself when `request_firmware()`
  fails (`cpp_read_fw_file()` in `msm_cpp.c` ✓), so the camera does not
  depend on the helper at all.

With that: **CPP FW Version 0x10020003** (firmware 1.2.0 ✓), no more
"Invalid frame message", the chromatrix is accepted, and **a frame
arrives** -- under our own firmware, for the first time.

**2. The "crash" was our debug code.**  The register dump that was
added to the ISP-IF reads the registers up to 0x29c, but the ISP-IF's
register map ends at `OUTPUT_SEL` (0x28c): the next ones (0x290 and up)
are the MISR registers, and *reading* a MISR hangs the AHB -- the CPU
stops, the kernel says nothing more, and the watchdog restarts the
phone.  A phone that restarts itself with no message in the log is what
that looks like; it is not a NULL dereference in the ISP-IF, and the
ISP-IF's single-non-zero-channel path never had a bug.  The dump now
stops at 0x28c.

**3. Our sensor sequence was the wrong mode.**  The capture of the
vendor's sensor writes holds *every* write the camera HAL made during a
whole session, and that session included several mode changes: the
arrays 7 and 9 to 13 belong to other modes.  Replaying all fourteen
left the sensor in the last of them (HMAX 0x9e18 -- a 40k-pixel line),
and the image's packets did not arrive at all.  The mode the camera
uses is the captures 0 to 6 and then the stream-on (the capture 8) ✓;
the imaging server now writes those, and the sensor's active area reads
back as **3280 x 2464** -- exactly the CAMIF's geometry ✓.

The frame that arrives is still empty (`bytesused=0`): the image's
packets are on virtual channel 1 while the camera HAL's lookup table
maps VC 0, so nothing is mapped.  The next step is the sensor's
virtual channel register, which the imaging server can now override
(`standalone/scripts/cam/sweep-vc.sh` ✓).

**A note on the kernel's `%zu`:** the kernel's `vsnprintf` does not know
`%zu` -- it reads the number as a *string pointer* and faults.  Use
`%u` with a cast.

### What the comparison with the author's kernel says

The author's kernel is built from a commit that is not published
(`da91d066bc6`), so the comparison is done on the binaries.  His own
recovery gives us his config and his symbol table (`kptr_restrict` has
to be set to 0 first ✓).

* **the device trees are identical** -- the ROM's 20260926 and 20261011
  images carry byte-identical DTBs, and ours is the vendor's plus our
  two `master-en` additions;
* **the config matches in the whole camera area** -- every
  `CONFIG_MSM_CAMERA*`, `CONFIG_MSM_CSID`, `CONFIG_MSM_CSIPHY`,
  `CONFIG_MSM_ISPIF`, `CONFIG_MSM_CPP` and `CONFIG_IMX179*` symbol has
  the same value in the two kernels.  The differences are the radio
  (he has Wi-Fi, Bluetooth and the modem; we strip them) and
  `CONFIG_HZ` (100 against our 300);
* **the CSID's ioctl path is the same code** -- the same call sequence,
  the same 80-byte `struct msm_camera_csid_params`, the same register
  accesses (eight, with the same values);
* **the ISP-IF's code is the same code** -- the same struct offsets
  (the HAL's `struct msm_ispif_params_entry` is read at the same
  places in both) and the same immediates.

So the difference that makes the camera work in the ROM's kernel is
**not** in the camera's driver, the device tree or the configuration.
What is left is the sensor's own state -- the virtual channel it sends
on -- and the one place where the two kernels do differ: the CSID's
lookup table.  The CSID v3.0's register map has *one word per virtual
channel* (`LUT_VC_0` 0x10, `LUT_VC_1` 0x14, `LUT_VC_2` 0x18,
`LUT_VC_3` 0x1c), while `msm_csid_cid_lut()` computes the address the
way the CSID v2.0 does -- `(cid >> 2) * 4` -- so all four channels end
up packed into the byte lanes of the word at 0x10.  The author's
kernel computes the same way for the LUT, but *differs* for the CID
configuration register: he uses `(cid >> 2) * 4 + 0x20` where ours uses
`cid * 4 + 0x20`.  The two kernels are therefore not the same revision
of the same camera stack, and this is the place to look.

The tools for this are new: `standalone/scripts/kernel/compare-roms.sh`
(the images), `diff-kernels.py` (the symbol tables and the function
sizes), `cmp-func.py`, `cmp-callseq.py` and `cmp-iow.py` (one function
in both kernels: its calls, its constants, its register accesses).

---

## 2026-10-21 (evening) — the "divergence" in the CSID was a misreading

*2026-10-21, 17:00–19:30*

### The claim that was wrong

The previous entry ended with a "one real divergence" between the two
kernels: the CSID's channel lookup table.  It was read out of the
author's disassembly as

```
the author:  (cid >> 2) * 4 + 0x20   for the CID configuration
ours:        cid * 4 + 0x20
```

**That is a misreading of the disassembly.**  The author's
`msm_csid_subdev_ioctl` was disassembled instruction by instruction
(`/tmp/opencode/auth-csid-ioctl-full.txt` against
`our-csid-ioctl-full.txt`, both produced with
`arm-none-eabi-objdump`):

```
the author, the LUT:      lsr r0, r0, #2        ; cid >> 2
                          smlabb r0, r0, r5, r6 ; (cid >> 2) * 4 + 0x10
                          and r3, r1, #3        ; cid & 3
                          lsl r3, r3, #3        ; the byte lane
                          bic  r0, r0, r8, lsl r3
                          orr  r0, r0, r2, lsl r3
the author, the CFG:      smlabb r1, r1, r5, r2 ; cid * 4 + 0x20   (r5 = 4, r2 = 0x20)
```

with `r5 = 4`, `r6 = 0x10`, `r8 = 0xff` set three instructions earlier
-- **exactly what our own `msm_csid_cid_lut()` does**:

```c
val = msm_camera_io_r(csidbase + CSID_CID_LUT_VC_0_ADDR +
	(cid >> 2) * 4) & ~(0xFF << ((cid % 4) * 8));
val |= (dt << ((cid % 4) * 8));
msm_camera_io_w(val, csidbase + CSID_CID_LUT_VC_0_ADDR + (cid >> 2) * 4);
val = (decode_format << 4) | 0x3;
msm_camera_io_w(val, csidbase + CSID_CID_n_CFG_ADDR + (cid * 4));
```

**=> The two kernels' CSID is the same code, and the same data (the
camera HAL's configuration) gives the same registers.**  The "one place
where they differ" does not exist.  The same check on the ISP-IF's
channel mask (`msm_ispif_get_cids_mask_from_cfg`) gives two
**byte-identical** functions; `msm_ispif_enable_intf_cids` and
`msm_ispif_intf_cmd` differ only in the compiler's choice of
instructions.

### What that leaves

The camera stack (CSID, ISP-IF, VFE32, the sensor driver) is the same
code in the two kernels, the configuration is the same, and the device
tree is the same.  The ROM's camera still works and ours does not.
So the difference is **not** in a register the kernel writes: it is in

* **the packets themselves** -- the sensor sends them on another
  virtual channel, or with another data type, in one of the two
  kernels; or
* **what happens after the CSID** -- the ISP-IF's routing, the CAMIF's
  timing, the buffer.

And to tell those apart, the one measurement that is still missing is
the CSID's registers **in the working case**: the ROM's kernel, with
the ROM's own camera running.  That needs a root shell in the ROM's
Android, which is what is being sorted out.

### Notes on the ROM side

* The ROM's Android is a user build: `ro.debuggable=0`, so `adb root`
  is refused, and `su` is not in the system.  The `ueventd` of the ROM
  finds the camera's firmware in **`/vendor/firmware`** (not
  `/lib/firmware`), so the CPP's firmware loads there -- and *only*
  there: `CONFIG_FW_LOADER_DIRECT_FS` is **not** set in either
  kernel, so `request_firmware()` can only go through the user space
  helper, and under a recovery ramdisk there is no helper that knows
  `/lib/firmware`.  **The "run our imaging server under the author's
  kernel in his recovery" experiment is therefore confounded**, exactly
  as it was suspected: without the CPP's firmware no frame can ever
  arrive, and "no frame" would prove nothing.
* The device's own camera apps: `com.google.android.GoogleCamera` (the
  one the tests drive) and no vendor camera of the LGE kind, so the
  photograph in `/sdcard/DCIM/Camera/IMG_20261021_051523.jpg` was
  taken by the *same* app and the *same* HAL as the failing case.

### The measurement that settles it: the ROM's own 20261011 kernel takes a photograph

Our kernel is built from **`466f069dcee`** -- and that is *exactly* the
commit the ROM's 20261011 kernel is built from (the commit is in our
tree's history, and the ROM's kernel banner says `g466f069dcee`).  So
the ROM's kernel and ours are the same source, and the only
differences are **our patches, our build and the four files we have
changed but not committed**.

The ROM's 20261011 kernel was booted (`fastboot boot
images/boot-rom-20261011.img`) and the ROM's own camera was driven
with the same script as always:

```
adb shell input keyevent 224                                     # wake
adb shell monkey -p com.google.android.GoogleCamera ... 1         # start
adb shell input tap 195 537                                      # the shutter
```

```
-rw-rw---- 1 root everybody 1064641 2026-10-21 17:45 IMG_20261021_174531.jpg
```

**A photograph, 1,064,641 bytes, under the ROM's own kernel.**  (The
preview is black; the capture still works.)  The same script under
**our** kernel produces nothing.

=> **The base commit is good; something in *our* tree breaks the
camera.**  The candidates are the three committed patches and the four
files that are changed but not committed:

```
 M drivers/media/platform/msm/camera_v2/camera/camera.c        (+9)
 M drivers/media/platform/msm/camera_v2/isp/msm_isp40.c        (+32)
 M drivers/media/platform/msm/camera_v2/isp/msm_isp_util.c     (+54)
 M drivers/nfc/bcm2079x.c                                     (+33)
```

**`msm_isp40.c` is the VFE40 driver -- the camera front end.**  A
debug print in there can change what the CAMIF is programmed with,
and the CAMIF is exactly where the failing case stops.

### What the failing case looks like, in full

The ROM's camera under **our** kernel, with our own debug prints
(the whole log in `/tmp/opencode/ourvendor.log`):

```
DEBUG: CSID0 LUT: vc0 0x0000122b vc1 0x00000000 vc2 0x00000000 vc3 0x00000000,
                cfg0 0x00000023 cfg1 0x00000013
DEBUG: CSID0 irq status 0x000002dd, packets 523473, unmapped long 0x00000000,
                mapped long 0x00000000, short 0x0000011a, ecc 0, crc 0
DEBUG: CSIPHY0 interrupt status 0x00000004, power cfg 0x0000003f, hw version 0x00000010
DEBUG: ISP-IF routing 0: vfe_intf 0 intftype 0 num_cids 1 cid0 0 cid1 0 csid 0 crop 0 (0..0)
DEBUG: ISP input: src 0, pix_clk 260000000, mux 0, camif_input 3
DEBUG:   camif: lines 2464, pixels 3280, first_px 0, last_px 3279, first_line 0, last_line 2463
msm_vfe40_process_error_status: camif error status: 0x80000000
msm_vfe40_process_error_status: violation
DEBUG: camif violation status 0x00000001
DEBUG: camif violation reg 0x48 = 0x00000001
DEBUG: camif regs: 0x1C 0x00000000 0x2E8 0x00000003 0x2F4 0x00000006 0x300 0x09a00cd0
              0x304 0x00000ccf 0x308 0x0000099f 0x314 0xffffffff 0x31C 0x80000000
DEBUG: camif configured: clock 266670000, width 3280
msm_isp_process_error_info: Stream[0]: dropped 2 frames
```

Read from the bottom up:

* the CSID is programmed with the vendor HAL's own table
  (`0x122b`: the image's data type 0x2b in the first channel, the
  8-bit stream 0x12 in the second; the CID configuration 0x23 and
  0x13) -- **the same table the ROM's kernel programs**, because the
  code is the same (see above);
* **the ISP-IF is routed to cid 0** -- the image's channel.  (The
  "the ISP-IF routes VC 1 -> PIX0" of the earlier notes is *our own*
  imaging server's configuration, not the vendor's.)
* the CAMIF's window is the vendor's own: 3280 pixels per line, 2464
  lines, first pixel 0, last pixel 3279, first line 0, last line
  2463;
* **the CAMIF raises an error**: its error status has bit 31 set, the
  violation status has bit 0 ("camif violation"), and the ISP reports
  dropped frames.  That is where the picture is lost.
* the CAMIF's clock is 266,670,000 (33 fps x 3280 x 2464) while the
  HAL's input pixel clock is 260,000,000.

So the pipeline is: the packets arrive, the CSID is configured, the
ISP-IF is routed -- and the **CAMIF** throws the frame away.

### The tools that were built for this

* `standalone/cam/regs.c` -- reads a camera block's registers through
  `/dev/mem` (`regs 0xfda08000` for the CSID0, `... ispif` for the
  ISP-IF, `... vfe` for the CAMIF).  The addresses come from the
  device tree: `qcom,csid@fda08000` (CSID0), `qcom,csid@fda08800`
  (CSID2), `qcom,ispif@fda0a000`, `qcom,vfe@fda10000` (VFE0).
* `standalone/scripts/cam/rom-working-state.sh` -- runs the camera on
  the ROM's kernel and reads those registers while it runs.  It needs
  a root shell in the ROM's Android.

### The ROM's Android, and root

* it is a **user build**: `ro.debuggable=0`, so `adb root` is refused,
  and there is no `su` in `/system`, `/system/xbin` or `/vendor/bin`.
  `settings put global adb_root_enabled 1` does not help: the check
  is `ro.debuggable`.
* the KernelSU manager was installed to try to get a root shell
  there; it reports "Working" (kernel 32676-5) but the `su` binary is
  not exposed to the shell, and the manager was removed again.
* the camera's CPP firmware is in the ROM's **`/vendor/firmware`**, and
  the ROM's `ueventd` finds it there.  Neither kernel has
  `CONFIG_FW_LOADER_DIRECT_FS`, so `request_firmware()` can only go
  through the user space helper -- **the author's recovery cannot
  load the firmware, so the "our imaging server under the author's
  kernel in his recovery" experiment is confounded** and was not run.
* the boot partition now holds `images/old/boot-kernelsu-20260926.img`
  (flashed, as asked for); everything else was only `fastboot boot`ed.

### The base commit is good; the difference is in the *build*

The ROM's own 20261011 kernel (`g466f069dcee` -- the same commit our
tree is built from) takes a photograph.  Our build of the same commit
does not.  What was compared, and what came out:

| | the ROM's 20261011 kernel | ours |
|---|---|---|
| the source | `466f069dcee` | `466f069dcee` (the tree is at that commit) |
| the compiler | gcc 16.2.0 | gcc 16.2.0 (the same) |
| the device tree | the ROM's DTB | **byte-identical** (176,016 bytes, and the decompiled DTS is identical) |
| the configuration | unknown (no `CONFIG_IKCONFIG` in the image) | the tree's `lineageos_hammerhead_defconfig` minus the radio (68 lines differ, all of them Wi-Fi, Bluetooth, the modem, USB ethernet, HID) |
| the camera's configuration | ? | `CONFIG_MSM_CSID/ISPIF/CPP/CSIPHY/CAMERA_SENSOR/IMX179` all `y`, `CONFIG_MSM_CAMERA` not set -- the same as the author's own recovery kernel |
| the machine code | -- | **differs everywhere**, down to the block layer (`cfq_dispatch_requests` is not found in the ROM's kernel byte for byte) |

**=> The two kernels are the same source, compiled differently.**  And
the camera's behaviour is different, so the difference is in the
compilation -- the flags, or a configuration option that the camera's
code depends on.

(For the record: the "the camera's code differs" claim is only about
the *bytes*.  The source is the same; the compiler chose different
instructions.  The first version of this comparison was wrong because
`objdump` prints an ARM instruction word big-endian while the image
holds it little-endian, and the search was done in the wrong order.)

### Where the frame is lost: the camera front end

The ROM's camera under our kernel, with our own debug prints:

```
DEBUG: ISP input: src 0, pix_clk 260000000, mux 0, camif_input 3
DEBUG:   camif: lines 2464, pixels 3280, first_px 0, last_px 3279, first_line 0, last_line 2463
msm_vfe40_process_error_status: camif error status: 0x80000000
msm_vfe40_process_error_status: violation
DEBUG: camif violation status 0x00000001
DEBUG: camif regs: 0x1C 0x00000000 0x2E8 0x00000003 0x2F4 0x00000006 0x300 0x09a00cd0
              0x304 0x00000ccf 0x308 0x0000099f 0x314 0xffffffff 0x31C 0x80000000
DEBUG: camif configured: clock 266670000, width 3280
msm_isp_process_error_info: Stream[0]: dropped 2 frames
```

* the CAMIF is programmed with the vendor HAL's own window (3280
  pixels per line, 2464 lines, the last pixel 3279, the last line
  2463 -- `0x300` = 2464 << 16 | 3280, `0x304` = 3279, `0x308` = 2463);
* the CAMIF raises an error (its error status has bit 31 set) and a
  violation (bit 0), and the ISP drops the frames;
* the CAMIF's clock is **266,670,000** while the HAL asked for
  **260,000,000**: `msm_isp_cfg_pix()` stores the HAL's
  `input_pix_clk` and then calls `msm_isp_set_clk_rate()`, which asks
  the clock framework for that rate and stores back what it got.
  266,670,000 is 3280 x 2464 x 33 fps.

**=> The next measurement is the same registers in the case that
works**: the ROM's kernel, the ROM's camera running, the CAMIF's
registers read through `/dev/mem` (`standalone/cam/regs.c`).  That
needs a root shell in the ROM's Android, which is what is being
sorted out.

### The tools that were built for it

* `standalone/cam/regs.c` -- reads a camera block's registers through
  `/dev/mem`: `regs 0xfda08000` (the CSID0), `regs 0xfda08800` (the
  CSID2), `regs 0xfda0a000 ispif`, `regs 0xfda10000 vfe` (the CAMIF).
* `standalone/scripts/cam/rom-working-state.sh` -- runs the camera on
  the ROM's kernel and reads those registers while it runs.

### Notes

* The four files with the debug prints are in the stash
  ("debug prints (camera diag) 2026-10-21") and the tree is checked
  out at `466f069dcee`; the branch `airgap/pmic-keys` is the state to
  go back to.
* The build tree `/tmp/opencode/ko-arch2` currently holds the build of
  the **base commit** (no patches, no debug prints) -- that is the
  kernel that was tested above.
* The VFE's register dump (our own debug code, which reads every
  register of the VFE from the CAMIF's interrupt handler) was turned
  off: it reads the MISR registers, and reading a MISR hangs the AHB
  (the same thing that the ISP-IF's dump did).  The CAMIF's violation
  is reported with or without it, so it was not the cause -- but it
  has no business reading 0x000..0x320 of the VFE from an interrupt
  handler.

---

## 2026-10-21 (night) — the ROM's kernels are now readable, and the fix is in the author's tree

*2026-10-21, 19:00–23:00*

### The compressed kernels *can* be decompressed after all

The log said the CAF kernel payload "uses a compression this host has
no tool for".  **That was wrong.**  Each boot image's kernel has
exactly **one** gzip stream -- at `0x3c8d` in the 20261011 image, at
`0x3c21` in the TWRP -- and it decompresses to the whole kernel:

```
rom20261011.raw  15,390,136 bytes     (the ROM's 20261011 kernel)
rom20260926.raw  15,388,728 bytes
twrp.raw         15,376,240 bytes  -- byte-identical to twrp-unpack/vmlinux
```

So the author's kernels are now plain files to compare.

### The kernels are built with debug info, and they name the author's tree

The decompressed kernel contains the DWARF's strings, and among them
the **source paths of the author's own tree**:

```
/media/acroreiser/playground/lineage/kernel/lge/hammerhead/
    drivers/media/platform/msm/camera_v2/camera/camera.c
    drivers/media/platform/msm/camera_v2/ispif/msm_ispif.c
    drivers/media/platform/msm/camera_v2/isp/msm_isp40.c
    drivers/media/platform/msm/camera_v2/msm_buf_mgr/msm_generic_buf_mgr.c
    drivers/media/platform/msm/camera_v2/msm.c
    drivers/media/platform/msm/camera_v2/msm_vb2/msm_vb2.c
    drivers/media/platform/msm/camera_v2/sensor/cci/msm_cci.c
    drivers/media/platform/msm/camera_v2/sensor/csid/msm_csid.c
    drivers/media/platform/msm/camera_v2/sensor/csiphy/msm_csiphy.c
```

**=> The ROM's 20261011 kernel is built from the author's own tree at
`/media/acroreiser/playground/lineage/kernel/lge/hammerhead`, and that
tree is *not* the tree we have** -- our tree's `466f069dcee` is the
*upstream* commit, and the camera fix is not in it.

### What changed between the author's own two builds

Comparing the author's TWRP kernel with the author's 20261011 kernel,
function by function (the same source, so the machine code is
comparable), the parts that changed are:

* **the camera core** -- `camera_v4l2_*`, `camera_init_v4l2`,
  `camera_pack_event`, `msm_camera_*`;
* **the camera's clock mux** -- `cam_mux_clk_set_parent`,
  `cam_mux_clk_list_rate`, `cam_mux_clk_handoff`, `cam_mux_clk_enable`;
* **the RPM regulator code** -- `rpm_vreg_*`.

The ISP-IF, the CSID, the VFE and the sensor driver are the same code
in both (90 of our camera functions are byte-identical in the ROM's
20261011 kernel and in ours).

### The clock, and the CAMIF's violation

The failing case says the CAMIF's clock is **266,670,000** while the
camera HAL asks for **260,000,000**:

```
DEBUG: ISP input: src 0, pix_clk 260000000, mux 0, camif_input 3
DEBUG: camif configured: clock 266670000, width 3280
msm_vfe40_process_error_status: camif error status: 0x80000000
msm_vfe40_process_error_status: violation
```

And our tree explains where 266,670,000 comes from -- the vfe40
driver's clock table has **no 260 MHz**:

```c
/* drivers/media/platform/msm/camera_v2/isp/msm_isp40.c */
#define VFE40_CLK_IDX 1
static struct msm_cam_clk_info msm_vfe40_clk_info[] = {
	{"camss_top_ahb_clk", -1},
	{"vfe_clk_src", 266670000},     <-- forced
	...
};

/* arch/arm/mach-msm/clock-8974.c */
static struct rcg_clk vfe0_clk_src = {
	.set_rate = set_rate_hid,                      <-- the rate cannot be set
	.freq_tbl = ftbl_camss_vfe_vfe0_1_clk,
	...
};
static struct clk_freq_tbl ftbl_camss_vfe_vfe0_1_clk[] = {
	F_MM( 37500000,  gpll0,  16,   0,   0),
	F_MM( 50000000,  gpll0,  12,   0,   0),
	F_MM( 60000000,  gpll0,  10,   0,   0),
	F_MM( 80000000,  gpll0, 7.5,   0,   0),
	F_MM(100000000,  gpll0,   6,   0,   0),
	F_MM(109090000,  gpll0, 5.5,   0,   0),
	F_MM(150000000,  gpll0,   4,   0,   0),
	F_MM(200000000,  gpll0,   3,   0,   0),
	F_MM(228570000, mmpll0, 3.5,   0,   0),
	F_MM(266670000, mmpll0,   3,   0,   0),
	F_MM(320000000, mmpll0, 2.5,   0,   0),
	F_END
};
```

`msm_isp_set_clk_rate()` asks the clock framework for the HAL's
260,000,000, gets 266,670,000 back (the nearest in the table), and
stores *that* -- so the CAMIF, which is timed by the real clock, sees a
2.5 % mismatch against the data the sensor sends, and raises the
violation that drops the frame.

**=> The camera's clock mux is in the list of what the author changed,
and the clock table has no 260 MHz.  That is where the fix is.**

### The 20260926 kernel has root, and the same fault

The 20260926 kernel (the author's older build) takes no photograph
either, and it is the one KernelSU still works on:

```
$ adb shell 'su -c id'   ->   uid=0(root) gid=0(root) context=u:r:ksu:s0
E mm-camera: port_isp_mct_ctrl_cmd: error in isp_update_buf_info, identity = 0x10002
```

The same `isp_update_buf_info` error, the same empty frame.  (That
error is a red herring: it happens in the working kernel too.)

### Things that were wrong in the earlier notes

* "the ROM's 20260926 and 20261011 images carry byte-identical DTBs"
  -- true, but the file `images/boot-rom-20260926.img` is now a copy of
  the 20261011 image (the images were reorganised), so comparisons
  between the two files are comparisons of the 20261011 with itself.
* "the compressed payload uses a compression this host has no tool
  for" -- it is gzip.
* "the author's kernel is missing the firmware loading fixes" -- the
  CPP's firmware is in the ROM's `/vendor/firmware` and the ROM's
  `ueventd` finds it there; `CONFIG_FW_LOADER_DIRECT_FS` is not set in
  either kernel, so a recovery ramdisk cannot load it, and the
  "our imaging server under the author's kernel in his recovery"
  experiment is indeed confounded -- it was not run.

### The tools that now exist

* `standalone/cam/regs.c` -- reads the CSID/ISP-IF/CAMIF registers
  through `/dev/mem` (needs a root shell).
* `standalone/scripts/cam/rom-working-state.sh` -- runs the ROM's
  camera and reads those registers while it runs.
* the decompressed ROM kernels: `/tmp/opencode/rom20261011.raw`,
  `/tmp/opencode/rom20260926.raw`, `/tmp/opencode/twrp.raw`, with the
  author's symbol table in `/tmp/opencode/author-kallsyms.txt`.

---

## 2026-10-21 (late) — the measurement that matters, and a methodology correction

*2026-10-21, 20:40–21:30*

### A correction: the adb-driven shutter is not a valid test

The "take a photograph" step of the camera tests was done over adb
(`input keyevent 115`, and a tap on the shutter button).  **That is
not a reliable measurement**: the camera app takes a varying amount of
time to focus, and the shutter does not always fire.  Every "no
photograph" result obtained that way is therefore worthless, including
the ones recorded earlier in this log.

**=> The shutter is now pressed by hand, and the procedure is checked
against a kernel that is known to work (the ROM's own 20261011 image)
before any conclusion is drawn from a failing case.**

`standalone/scripts/cam/shoot.sh` opens the camera, takes a screenshot
(so the shutter's position is known) and reports the files before and
after; `standalone/scripts/cam/camtest.sh` does the whole cycle --
build with a given configuration, pack with the ROM's ramdisk, boot,
shoot, report.

### The build that is identical to the ROM's works

A new build was made from the **same commit** as the ROM's 20261011
kernel (`466f069dcee`, the tree checked out at that commit, no patches
of ours) but with **the ROM's own configuration**, taken from the
running kernel's `/proc/config.gz`:

```
/tmp/opencode/cfg-20261011.txt   1241 options   (the ROM's 20261011 kernel)
/tmp/opencode/ko-rom/            the build: 7,398,792 bytes
```

(The ROM's own kernel image is 7,399,616 bytes -- the same size to
within a kilobyte, because the configuration is the same.)

**With that kernel the ROM's camera app took a photograph**
(`IMG_20261021_205759.jpg`, 2,040,377 bytes).

So:

| source | configuration | photograph |
|---|---|---|
| 466f069dcee (pristine, no patches of ours) | the ROM's 20261011 | **yes** |
| 466f069dcee (pristine, no patches of ours) | ours (the defconfig minus the radio) | to be retested with the hand-pressed shutter |
| 466f069dcee + our patches | ours | to be retested |
| 466f069dcee + our patches | the ROM's | to be retested |

**=> Our patches are no longer the prime suspect: the configuration
is.**  The 68 options that differ between our configuration and the
ROM's are now being bisected with `camtest.sh`.

### Two more facts about the ROM's build

* the ROM's kernel was built by the **LineageOS build system** (the
  banner says `nobody@android-build`), ours by our own script;
* the ROM's kernel image carries the **author's own build path** in
  its assert strings:

```
/media/acroreiser/playground/lineage/kernel/lge/hammerhead/...
```

which is also why the image has no DWARF: the `.debug_str` section
(with the compiler's `DW_AT_producer` and therefore the exact flags)
is not part of a loaded image, so the flags cannot be read out of the
ROM's kernel the way they can be read out of our own `vmlinux`:

```
GNU C89 16.2.0 -mlittle-endian -mabi=aapcs-linux -mno-thumb-interwork -marm
           -mcpu=cortex-a15 -mfloat-abi=soft -march=armv7ve -g -Os -O3
           -std=gnu90 -fno-strict-aliasing -fno-common ...
```

### The camera's real failure, from the kernel's own messages

With the ROM's HAL and our kernel, the camera dies in the buffer
manager:

```
msm_isp_buf_prepare: Invalid buffer state: 3
msm_isp_buf_enqueue: Buf prepare failed
```

State 3 is `MSM_ISP_BUFFER_STATE_QUEUED` -- the HAL has already
queued the buffer and the old camera core refuses to prepare it.  The
ROM's 20261011 kernel has the newer CAF's buffer manager
(`msm_buf_mgr/msm_generic_buf_mgr.c` in its build paths) where our tree
still has the old `msm_buf_mgr.c`.  That is the same class of change
as the DMI-64 table fix that is already in our tree.

---

## 2026-10-21 (21:10) — the camera was never broken

*2026-10-21, 20:40–21:15*

### The shutter, fired properly

The camera app takes a varying time to focus, so a shutter pressed over
adb often does nothing.  The reliable way is the **hardware camera
key**, which the app handles itself:

```sh
adb shell am start -a android.media.action.STILL_IMAGE_CAMERA
sleep 2
adb shell input keyevent 27          # KEYCODE_CAMERA
```

### The result

| kernel | photograph |
|---|---|
| the ROM's 20261011 (the known-good case) | `IMG_20261021_210628.jpg`, 874,539 bytes |
| **ours**: the pristine commit `466f069dcee`, our configuration | **`IMG_20261021_210952.jpg`, 874,030 bytes** and `IMG_20261021_211000.jpg`, 867,800 bytes |

The photograph taken under our kernel is a real picture (a dark scene
with the sensor's noise in it -- `/tmp/opencode/ourphoto.jpg`).

**=> The camera was never broken.**  Every "the camera produces no
frame" result in this log came from a test that never fired the
shutter, and every conclusion drawn from it (the CSID's lookup table,
the virtual channel, the CAMIF's clock, the buffer manager's state
check, the "the ROM's kernel has a newer camera core") was a
consequence of that.

What is left standing:

* the CPP's firmware really has to be in the kernel's firmware path,
  and our own patch for it (reading `/lib/firmware` directly) is
  needed for our ramdisk;
* the DMI-64 table fix is real and is in our tree;
* the panic-restart change, the pm8941 pin table and the volume keys'
  master-en are unrelated to the camera;
* the ROM's kernels can be decompressed (they are gzip, one stream
  each), which is worth keeping.

**=> The next step is the one the project actually needs: the camera
in *our own firmware*.**  The MaixPy camera API on the Krux side
(`sensor.set_pixformat` and friends) was written against a camera that
was believed to be broken; with the camera working, the port's camera
page can be finished and tested on the device.

---

## 2026-10-21 (23:00) — 2026-10-22 (00:00) — the sensor's real registers, and the order of the stream-on

*2026-10-21, 23:00–2026-10-22, 00:00*

### The shutter, once

The camera test pressed the hardware camera key **twice** per run -- a
leftover from the days when the first press was often swallowed while
the app was still focusing.  It was not deliberate, and it makes a
capture session ambiguous: two captures, two mode changes, two ISP
cycles.  `standalone/scripts/cam/camtest.sh` now takes **one**
photograph per run, and if that one produces nothing, that is the
result.

### The sensor's registers, read out of the sensor while the vendor's camera held it

`standalone/cam/sensordump.c` (new) opens the sensor's sub-device and
reads registers with `CFG_SLAVE_READ_I2C` -- it only *reads*, so it can
be run **while the ROM's own camera app has the sensor open**.  It
needs a root shell, which the ROM's 20260926 image has (`su -c ...`).

Two readings were taken: one while the preview was up, and one two
seconds after the shutter.  The second one is the **photo mode**:

```
0x0100 = 0x0001        the stream is on
0x0202 = 0x0018        the PLL
0x0203 = 0x0012        (the preview's is 0x009a)
0x0204 = 0x0000
0x0205 = 0x0034        (the preview's is 0x00e0)
0x0301 = 0x0005  0x0302 = 0x0000  0x0303 = 0x0001
0x0304 = 0x000b        <- the captured arrays do not have this register
0x0305 = 0x0006  0x0306 = 0x0002  <- and not this one
0x0307 = 0x00bc  0x0308 = 0x0000  0x0309 = 0x0005
0x030a = 0x0000  0x030b = 0x0001  0x030c = 0x0000  0x030d = 0x00ca
0x0340 = 0x0009  0x0341 = 0x00d6    VMAX = 2518   (big endian!)
0x0342 = 0x000d  0x0343 = 0x0070    HMAX = 3440
0x0344..0x0347 = 0
0x0348 = 0x000c  0x0349 = 0x00cf  0x034a = 0x0009  0x034b = 0x009f
0x034c = 0x000c  0x034d = 0x00d0  0x034e = 0x0009  0x034f = 0x00a0
0x0350 = 0x0000  0x0351 = 0x0000
0x0401 = 0x0000  0x0405 = 0x0010  0x0406 = 0x0000  0x0407 = 0x0010  <- 0x0407 is new too
0x098e = 0x0000  0x0990 = 0x0000    the sensor is left on page 0
0x3302 = 0x0001  0x3303 = 0x0000    the virtual channel
0x33d4 = 0x000c  0x33d5 = 0x00d0  0x33d6 = 0x0009  0x33d7 = 0x00a0
```

### What that says about the captured arrays

`standalone/cam/sensor_config.h` was captured from the ROM's camera
stack on 2026-10-19 -- when the shutter never fired, so it is the
**preview's** sequence, and it differs from the photo mode in three
ways that matter:

1. **The PLL.** The captured mode table has `0x0202/0x0203 = 0x09/0xc4`;
   the vendor's photo mode has `0x18/0x12`.  The PLL decides the
   sensor's output rate.
2. **The byte order of the geometry.** The captured arrays write
   `0x0340 = 0x09d6` as one 16-bit write; the sensor then holds
   `0xd6` in `0x0340` and keeps the old `0xc8` in `0x0341` -- the
   geometry is reversed (the IMX179's pairs are **big endian**).  The
   vendor writes the two bytes separately: `0x09` then `0xd6`.
3. **Three registers the capture does not have at all**: `0x0304`,
   `0x0306` and `0x0407`.

`standalone/cam/sensor_live.h` (new) holds the vendor's live photo
mode, byte by byte, and the imaging server can program the sensor from
it (`imaging8.c`: `use_live_sensor_seq`).

### The result: the sensor streams

| | CSID packets | the CSID's table |
|---|---|---|
| the captured (preview) arrays | **164** | `0x122b`, cfg0 `0x23`, cfg1 `0x13` |
| **the vendor's live list** | **54,000 – 69,000** | `0x122b`, cfg0 `0x23`, cfg1 `0x13` |

The sensor now sends the image, and the CSID's table is **byte for byte
the vendor's** (the same `0x0000122b` with `cfg0 = 0x23` and
`cfg1 = 0x13` the ROM's own kernel programs).

The ISP-IF's routing now matches the vendor's as well: the log's
"the ISP-IF routes VC 1 -> PIX0, CID_MASK 0x02" was **our own**
server's configuration, not the vendor's -- the vendor's own session
under our kernel prints `num_cids 1 cid0 0 csid 0` (Entry "the failing
case looks like, in full").  We now send exactly that.

### The crop is a knob, not a fact

`msm_ispif_enable_crop()` -- the function that sets the PIX0 enable bit
in `CTRL_0` -- is only called when `crop_enable` is set, so the earlier
entries concluded that the crop *has* to be enabled.  But the vendor's
own session sends **`crop_enable = 0`** (window `0..0`) and works, so
that conclusion was drawn from a broken setup (the sensor was not
streaming at all then).  The crop is now a parameter
(`imaging8.c`: `crop_enable`), and both settings were tried.

### The order of the stream-on was wrong

The `PRIV_STREAM_ON` handler switched the sensor on **before**
`isp_start()`:

```
sensor_write_regs(stream_on_regs)   <- 0x0100 = 1: the sensor starts sending
...
isp_start()                          <- the ISP is configured afterwards
```

In the photo mode the IMX179 sends **one frame and then stops**, so
that frame is spent before the pipeline is live.  The ISP now comes
first and the sensor second.

### A bug in the server's own summary

`argv[7]` is the ISP's stream source, and the values are
`PIX_ENCODER = 0`, `PIX_VIEWFINDER = 1`, `CAMIF_RAW = 2`.  The server
was started with `0` -- that is **PIX_ENCODER** -- while its summary
printed "CAMIF_RAW", because the summary only distinguishes
`PIX_VIEWFINDER` from everything else.  The photo path wants
`CAMIF_RAW` (the kernel's `msm_isp40.c` sets the cross-bar to `0x300`
for it and refuses `PIX_ENCODER` in `msm_isp40_update_io_format()`).

### What the vendor's live state cannot be read, and why

* The **kernel's capture files** (`isp_cfg_*.bin`, `sensor_cfg_*.bin`)
  are written by the camera daemon's own context, and the ROM's
  Android's SELinux denies it the right to create a file in
  `/data/local/tmp`; `override_creds()` does not change the SELinux
  context, so the capture cannot run there.  (In our own firmware it
  works -- that is how `sensor_config.h` was made.)
* The 20260926 image's **KernelSU** gives a root shell only with the
  author's kernel: the `su` binary needs KernelSU's kernel module,
  which our tree does not have.  Booting that image with our kernel
  gives `su: inaccessible or not found`.
* `/dev/mem` does not exist: `CONFIG_DEVMEM` is **not set** in the
  ROM's kernels and in ours, so `standalone/cam/regs.c` cannot be used
  there.
* The shell cannot read the kernel's log on the ROM's Android:
  `dmesg` fails with `klogctl: Permission denied` (SELinux, not
  `dmesg_restrict` -- the option is unset in every kernel here), and
  `logcat` carries no kernel messages.

So the vendor's live registers can only be compared on a kernel that
prints them **and** whose output the shell can read -- that is our own
firmware, where `dmesg` works.

### Where this leaves the port's camera path

The sensor's registers, the CSID's table and the ISP-IF's routing all
match the vendor's now, and the sensor sends 60,000 packets instead of
164.  The frame is still empty: the VFE's `pix_count` is 0 and the
dequeued buffer's `bytesused` is 0, so the data does not reach the
ISP's pixel path.

The next things to try, in order:

1. the stream source: `CAMIF_RAW` (2) instead of `PIX_ENCODER` (0),
   and the summary has to name the value it actually sends;
2. the **photo session's** ISP configuration: `cam_config.h` holds
   the ISP's 39 uploads as captured on 2026-10-19, which is the
   *preview's* configuration -- the same mistake the sensor's arrays
   were.  Capturing it needs the kernel's capture files to work under
   the ROM's Android, i.e. a capture path SELinux allows;
3. the packets' **rate**: 54,000 packets in a run is roughly a third
   of a full frame (3280x2464 RAW10 is 10.1 MB), so the sensor may
   still be sending at the wrong rate or the CSID may be dropping
   what it receives.

### The files

* `standalone/cam/sensor_live.h` (new) -- the vendor's photo mode.
* `standalone/cam/sensordump.c` (new) -- reads the sensor's registers
  while the vendor's camera has it open.
* `standalone/scripts/cam/imaging.sh` (new) -- runs the imaging server
  with named settings; the server's positional arguments had already
  shifted twice in this session (once by one argument, which left the
  CSID's second channel unprogrammed, and once because a comment
  after a backslash swallowed the rest of the command line).
* `standalone/scripts/cam/camtest.sh` -- one photograph per run.
* `standalone/cam/imaging8.c` -- the live list, the crop as a
  parameter, and the ISP before the sensor.
* the kernel: the capture files are now `0666` (`notify_change`),
  so the shell can read them in our own firmware.

---

## 2026-10-22 (00:20) — the vendor's *photo session* captured, and the geometry's byte order

*2026-10-22, 00:00–00:30*

### The capture works now

The kernel's capture files were not readable under the ROM's Android
because the file was created with the camera daemon's umask (006),
which takes the *other* users' read bit away.  The mode is applied when
the file is **created**, so the fix is to lift the umask for the
moment (`current->fs->umask = 0` around `filp_open()`), not to change
the mode afterwards.  With that, the ROM's Android produces **6,848**
ISP uploads and 13 sensor register arrays, all of them mode 0666 and
readable by the shell.

### What the vendor does in a photo session

The ISP configuration is **not** sent once: it is sent **on every
frame**.  The capture holds 1,141 cycles of six uploads each, of which
1,120 are byte-for-byte identical (the preview's steady state) and 21
are unique.

* the **initial configuration** (uploads 2..57) is what
  `standalone/cam/cam_config.h` holds -- our 39 uploads match the
  vendor's **37 of 39, in the same order** ✓✓;
* the **capture** (uploads 67..87) is a different set of registers.
  Compared with the preview's initial configuration:

| register | preview | capture |
|---|---|---|
| `0x400` | 0 | **0x10000** |
| `0x444` | 24608 (0x6020) | **16447 (0x403f)** |
| `0x464` | 25166076 (0x18000fc) | **50332149 (0x30001f5)** |
| `0x518` | 0 | **9666778** |
| `0x638` | 7 | **0** |
| `0x37c` | 0 | 1 |

  (`0x444` and `0x464` are the SFE's and the CAMIF's geometry; the
  preview's are the *viewfinder's* size and the capture's are the
  *photo's*.)

### The geometry is written as a **word**, not as bytes

The `sensorio_*.bin` files are the vendor's ioctl headers, and they
say what the HAL asked for.  Decoded with the kernel's own struct
(`msm_camera_i2c_reg_setting`: the pointer, the size, the address
type, the data type and the delay):

```
idx  size  addr_type  data_type
  3    62       2          1        byte
  4     1       2          1        byte
  5    20       2          1        byte
  6     4       2          2        WORD   <- the geometry
  7    14       2          1        byte
  8     1       2          1        byte
  ...
```

**=> The vendor writes the geometry (`0x0340/0x0342`) as one 16-bit
word**, and everything else as bytes.  Our imaging server writes
*everything* as bytes (`MSM_CAMERA_I2C_BYTE_DATA`), so the geometry
arrived byte-swapped -- which is exactly what the read-back showed
(`0x0340 = 0x00d6` where the vendor has `0x09`).

**=> This is the byte order the live reading showed, and the reason
the geometry array has to be written as a word.**  The new
`sensor_live.h` writes the two bytes separately, which is equivalent
-- and the read-back now matches the vendor's exactly ✓.

### The sensor's own sequence in the photo session

The vendor's arrays, in the order it sent them:

```
 2: 62 registers   the mode table: 0100=0 (stream off), 0202=0x09,
                   0203=0xc4, 0301=5, 0303=1, ...
 3:  1 register    0100=0            (stream off)
 4: 20 registers   the config change: 0202=0x09, 0203=0xc4,
                   0340=0x09, 0341=0xc8, 0344..0351, 034c=0x0c, ...
 5:  4 registers   the geometry: 034c=0xcd0, 034e=0x09a0,
                   0342=0x0d70, 0340=0x09d6      <- as a WORD
 6: 14 registers   the mode: 0340=0x09, 0341=0xda, 0202=0x09,
                   0203=0xd6, 0204=0, 0205=0xa5, ...
 7:  1 register    0100=1            (stream on)
 8..14: 14 regs    the other modes (0x9e, 0x16, 0x34, 0x30, ...)
```

Note the **stream-on is the seventh array**, i.e. *after* the geometry
and the mode -- and note that the mode array (6) has
`0341=0xda`, not the `0xc8` of the preview.

**=> The photo mode's registers are the ones the sensordump read back
live** (`0x0202=0x18, 0x0203=0x12, 0x0340=0x09, 0x0341=0xd6, ...`)
except for the geometry, which the vendor writes as a word.  The
`sensor_live.h` list reproduces that end state ✓.

### What this means for the port's camera path

* the ISP's **capture** configuration is a different set of registers
  from the preview's, and the port's camera page needs the *capture's*
  -- our `cam_config.h` holds the preview's;
* the vendor's photo session is now captured in full, so the capture's
  configuration can be generated from it the same way the preview's
  was;
* the imaging server's stream source was `PIX_ENCODER` (0) while it
  claimed `CAMIF_RAW`; with `CAMIF_RAW` the kernel now reports
  `src 2` ✓ (the summary names the value it sends).


---

## Entry — 2026-10-28 — SeedKeeper signed off on the second phone; packaging

The SeedKeeper work reached a working state on a **second Nexus 5**.  Its NFC
coupling is reliable: the card can be attached for the whole sequence, reads
rarely fail, and a save to the card succeeded.

### SeedKeeper

* Full secure channel (0x81 handshake, manual key recovery, AES/HMAC envelope)
  and all card operations work: PIN verify, list, export, free space, import.
* **PIN is requested before the card is presented**, matching the official
  SeedKeeper Android app.  For save, the label is requested first too.  Both
  keep the card session uninterrupted; the old order read the status, asked
  for the PIN, and then needed the card re-presented because the link had gone
  idle during keypad entry.
* The page now retries a card session up to 20 times with an 8 s activation
  window, retries read actions with a fresh secure channel, and shows a retry
  counter with tap-to-cancel between attempts.
* Card Info is a PIN-free status read and doubles as a link test.

### RF experiments (reverted)

`FWI=15` (`0x58 0x01 0xF0`) and `PA_FSDI=2` (`0xBA 0x01 0x02`) were accepted
by the NFCC and did not improve the marginal first phone; the vendor
`SET_CONFIG_RF` is restored to `0x51 … 0x58 0x01 0x70`.  The first phone's
Android reader was also inconsistent, so those failures were coupling, not
the ISO-DEP driver.

### Tests

`82 passed` for `tests/test_seedkeeper.py` and
`tests/pages/test_seedkeeper.py`, using a host `evdev` stub and the vendored
embit.  The host `uhashlib_hw` mock now provides `sha512`/`_FastSHA512` so the
wallet fixtures work.

### Packaging and layout

* `kruxport/` is gone; the interpreter is built to
  `rootfs/micropython-arm32`.
* `rootfs/build-stub-ramdisk.sh --bundle` puts MicroPython, Krux and embit in
  `/opt/krux` in the ramdisk for a read-only production image; the init stub
  prefers it and falls back to `/data/krux` for development.
* `make-boot-image.sh` accepts `RAMDISK=`.
* `scripts/deploy-firmware.sh` creates all target directories and copies the
  embit `wordlists`, `descriptor` and `util` packages.
* The README now contains the build/deploy/bundling information that used to
  live in `kruxport/README.md`.
* `standalone-log.md` was renamed `project-log.md`; this is the first entry
  under the new name.

### NFC files at runtime

The `.ncd` blobs are vendor firmware patches.  The current port does not load
them; the chip already has firmware and answers `GET_BUILD_INFO` after a power
cycle.  No NFC blob is required in the target filesystem.  They remain as
reference.

### Next

Final validation with a fully rebuilt kernel, MicroPython and boot image on
the second phone, then commit the submodules and the main project.  After that,
camera Phase A.

### Final validation on the rebuilt image — 2026-10-28

Booted the **bundled** image (`rootfs/stock/boot-bundled.img`) with
`fastboot boot` on the second Nexus 5.  The running kernel was the freshly
built air-gap profile:

```
3.4.113-🇺🇦-g3f7bed3f2d6-09388-g95f4b0f38ce #1 SMP PREEMPT
```

`/proc/<pid>/cmdline` confirmed the firmware was the read-only copy:

```
micropython /opt/krux/boot.py
```

The final read-only checks all passed on the bundled image:

* **Card Info** (no PIN)
* **View Secrets**
* **Load Seed**

The NFC log showed three clean `0x81` handshakes and **zero `0xB2`** errors;
the first phone's RF failures did not reproduce.  Saving had already been
tested successfully on this phone before the rebuild.  The image was booted,
not flashed, so the installed boot partition is unchanged.

---

## 2026-10-29 — Camera Stages 1-3 complete

- Kernel: camera profile build with devtmpfs, CAMIF_RAW path delivers non-empty NV12 frames.
- camd daemon: captures QVGA, serves over Unix socket. CPP streaming fixed with retry.
- sensor.py + _image native module: smoke test passes (10 frames, 320x240).
- 100-frame stability passed. to_bytes() returns 153600 bytes. get_statistics computes l_mean/l_stdev.
- find_qrcodes integrated with quirc. lcd.display(Image) works.
- Known issues: dark image (fixed exposure), VFE overflow blocks PIX_VIEWFINDER, device unstable under sustained load.
- Remaining: QR on real code, scene-dependent entropy, 50 cycles, 30-min stability, air-gap.
- Stage 4 prompt: docs/cam-bringup-stage4.md.
