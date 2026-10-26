# kruxport — the Krux port, on the Nexus 5

Krux's firmware is **MaixPy** (a MicroPython fork) with its Python side
in `src/krux/*.py`; the hardware is reached through MaixPy's modules.
This directory is the port of that firmware to a minimal Linux (the
"route B" rootfs), running the **stock** kernel and piggybacking on
TWRP's init.

## What runs, and where

* **MicroPython 1.28.0, static arm32** — built by
  `build-micropython.sh` (buildroot cannot: `BR2_PACKAGE_MICROPYTHON`
  depends on `!BR2_STATIC_LIBS`, and TWRP's libc is bionic, so a
  dynamic binary would not run there).
  Verified on the device inside TWRP:

  ```
  $ adb shell /data/local/tmp/micropython -c "import sys; print(sys.implementation)"
  MicroPython (name='micropython', version=(1, 28, 0, ''), ...)
  ```

### Building it

```sh
./build-micropython.sh
```

The upstream source is **not** in this directory and is not downloaded:
it is the tag `v1.28.0` of the checkout in `../vendor/micropython`, and
the build takes a git worktree of it. What lands in the worktree is the
port's own files (this directory) applied on top by `apply-port.sh`.

Three directories, and only the third is disposable:

| | where | what |
|---|---|---|
| the source | `../vendor/micropython` @ `v1.28.0` | upstream, in the project |
| the build tree | `BUILD=` (default `/tmp/opencode/mp-build`) | the worktree, the objects, the linked binary |
| the sysroot | `SYSROOT=` (default `/tmp/opencode/armel-sysroot`) | the armel headers and static libc, rebuilt from `../vendor/toolchain.ubuntu` by `../tools/build-armel-sysroot.sh` |

The build tree and the sysroot are on tmpfs on purpose: they are
rebuilt many times a day, they are a few hundred megabytes of objects
that nobody wants in a backup, and putting them in the project would
mean the project's snapshots carry them.

```
BUILD=/tmp/opencode/mp-build  SRC=../vendor/micropython  SYSROOT=/tmp/opencode/armel-sysroot
```

The result is copied to `micropython-arm32` here — the binary that goes
on the device.

## The hardware, and what has to replace it

| Krux's MaixPy module | used by | the Linux replacement |
|---|---|---|
| `machine.I2C` | `i2c.py` (the NFC chip) | `/dev/i2c-0`; the NFC chip (BCM20793) is at **0x77** |
| `lcd` | `display.py` | `/dev/graphics/fb0` (1080×1920, 32 bpp, **stride 4352**) |
| `Maix.GPIO` + `fpioa_manager` | `buttons.py`, `light.py` | the GPIO sysfs; the touch is `/dev/input/event1` (raw evdev) |
| `sensor` | `camera.py` | V4L2 |
| `flash` | — | the SD card / the internal storage |
| `board` | `i2c.py`, `display.py`, `light.py` | the pin map (a table, not code) |
| `nfc` | `seedkeeper.py` | **done**: `nfc_bcm2079x.c` + `nfc.py` (below) |

The **NFC protocol is already in Python** (`seedkeeper.py` does
`import nfc` and calls `nfc.transport()`), so the phone side is an
ISO-DEP reader over the BCM20793 on I2C bus 0 at 0x77. A real card is
not needed to bring the transport up (see `standalone/README.md` §5).

## The NFC transport (done, verified against a real card)

Two files:

* **`nfc_bcm2079x.c`** -- the C module `_nfc`, compiled into the unix
  port.  It opens `/dev/bcm2079x`, drives the power and wake lines
  and reads and writes whole packets.  (Raw I2C does not work: the
  kernel's bcm2079x driver owns the chip's address and the chip
  NACKs anyone else.)
* **`kruxport/nfc.py`** -- the `nfc` module Krux imports.  It replays
  the vendor HAL's bring-up byte for byte, polls for a card,
  activates it and exchanges APDUs.

The API is exactly the one `seedkeeper.py` expects:

```python
t = nfc.transport()
t.connect(40000)                  # -> connected: True
data, sw1, sw2 = t.transmit(apdu)  # -> (b'', 0x6a, 0x82)
t.close()
```

Verified on the device, in TWRP, with no vendor HAL and no Android
framework: the port found a card, activated it and got an answer
(`6a82` -- the card is not a SeedKeeper, so its AID is not there).

Three things that had to be right, and each one is a trap:

1. **the wake line** has to be asserted *before* the configuration;
   with it deasserted the chip stays asleep and answers nothing;
2. **the routing and the protocol numbers are sent twice** -- once
   in the configuration and once after the deactivate.  The chip only
   finds a tag after the second round;
3. **the discovery must not be restarted too often** -- asking the
   chip to discover again every 300 ms interrupts the poll it is in
   the middle of.  Once per second works every time.

The bring-up's payloads are generated from the trace the kernel logs
of the ROM's own NFC framework (the kernel's bcm2079x driver logs
every packet), not typed by hand: two of them were wrong when they
were, and the chip accepted both and then never polled.

## The modules to write

MicroPython's unix port has no `fcntl` and no `machine.I2C`, so the
Linux bindings have to be a **C module** (the port's `mod*.c` +
`extmod/` pattern). The first one is the I2C bus, which everything
else sits on.

## The buildroot rootfs (the eventual runtime)

`standalone/rootfs/buildroot.fragment` carries the rootfs's settings.
What the build taught us:

* `BR2_RELRO_FULL` **selects** `BR2_PIC_PIE`, so PIE could not be
  turned off while RELRO was FULL — and a PIE `/init` hangs the
  bootloader on this kernel (an ET_EXEC `/init` is what works). The
  RELRO choice now names `BR2_RELRO_NONE`.
* `BR2_STATIC_LIBS` is only possible with uclibc or musl (not glibc),
  *and* MicroPython cannot be built with it at all — hence the separate
  hand build above.
* The buildroot path in `build.sh` was wrong, the stale `build/host`
  tools carried the old path, and the build extracts into `TMPDIR`
  (which must not be the tmpfs).
