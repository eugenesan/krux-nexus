# DIRECTORIES.md -- what is in this repository, and why

The project has two halves that grew apart, and this file says which
is which:

* the **air-gapping** of the ROM's Android (the top level: `build/`,
  `recovery/`, `runtime/`, `tools/`, `work/`), and
* the **standalone device** -- the hammerhead running the Krux
  firmware on our own kernel, with the camera (`standalone/`).

The two share the kernel, the toolchain and the log, and that is why
`standalone/` holds the kernel tree and the toolchain even though the
Android half uses them too.

```
.
├── README.md              the air-gapping half: what it does and how
├── STANDALONE-LOG.md      the session log (this is the record)
├── STANDALONE-FINDINGS.md the measurements, in one place
├── COMPACTION-LOG.md      the earlier log, kept for the history
├── build/                 the kernel build
├── recovery/              the air-gapped recovery zip
├── runtime/               the runtime part of the air-gapping
├── tools/                 the tools for the OTA zip (the Android half)
├── work/                  scratch: the OTA, extracted
└── standalone/            the standalone device: the firmware, the camera
```

## The top level (the Android air-gapping half)

### `build/`

The **kernel build**: `build-kernel.sh` builds the kernel with the
air-gap profile applied (the radio off, and the profile verified
afterwards, so nothing can switch it back on), and `patches/` holds
the two patches the kernel needs for a working adb.
`shiba.md` is the same build for the Pixel 8 (Tensor G3), which is
the other device this project covers.

The script is the one place that knows how to build a kernel for
this project; the toolchain is chosen with `TOOLCHAIN=` (see
`standalone/TOOLCHAIN.md`).

### `recovery/`

The **air-gapped recovery zip**: a TWRP-based recovery that has no
cell, no Wi-Fi and no Bluetooth.  `paths-*.list` are the lists of
files that are removed from the stock recovery to make it so, and
`build-zip.sh` builds the zip from the ROM's recovery with those
lists applied.  `zip/` holds what has been built.

### `runtime/`

The **runtime** part of the air-gapping: `airgap.sh` is the script
the device runs at boot (it stops the radio's services and checks
that they stayed stopped), and `run.sh` starts it.

### `tools/`

The tools that work on the **ROM's OTA zip** without a device:
`audit-zip.sh` (what is in the zip, does it reference radio
firmware, which init services start radios) and `sdat2img.py`
(turn the OTA's data blocks into a mountable image so the audit can
run on it).

The tools that belong to the kernel or the camera live with them:
`standalone/tools/` and `standalone/cam/tools/`.

### `work/`

**Scratch**: the OTA zip, extracted, and the images taken out of it
(including the ROM's `boot.img` and the shipped `/proc/config.gz`).
It is gitignored and can be deleted and re-extracted; the two things
worth keeping are copied out of it (the `kconfig.gz` in the top
level, and `standalone/rootfs/stock/boot.img`).

## `standalone/` -- the standalone device

The hammerhead, running the **Krux firmware** on **our own kernel**,
with the camera for the entropy.  See `standalone/README.md` for the
design and `standalone/BUILD-GUIDE.md` for how to build it.

| directory | what it is |
|---|---|
| `kruxport/` | **the port**: the firmware's own modules for this device -- the display, the input devices, the NFC, the I2C bus, the board description -- and the C modules MicroPython is built with (`lcd_bcm.c`, `evdev_bcm.c`, ...) |
| `cam/` | **the camera**: our own imaging server (the thing the ROM's `mm-qcamera-daemon` normally is), the data captured from the vendor's camera, and the C programs that probe the hardware.  `STATE.md` says where the camera stands |
| `rootfs/` | the **init stub** and the **stub ramdisk**: the device boots TWRP, but the `recovery` service is our own program, which hands the display to the firmware (or to TWRP, if the user holds the volume-up key) |
| `scripts/` | the scripts that drive the device: the deploy, the boot, the camera probes, the kernel's helper scripts |
| `tools/` | the kernel's tools: the Kconfig stripper, the config profiles, the toolchain fetcher, the sysroot builder |
| `patches/` | the kernel patches (the two that make adb work) |
| `vendor/` | the upstream trees, as git checkouts: the kernel, the firmware's source, MicroPython, and the toolchain packages.  **Not tracked**: they are large and have their own history |
| `notes/`, `logs/`, `probes/`, `phase0/`, `nfc/`, `kernelcaps/`, `buildroot/` | the working notes and the measurements of the earlier phases (see `standalone/README.md`) |

### The trees under `standalone/vendor/`

| tree | what it is |
|---|---|
| `android_kernel_lge_hammerhead` | the kernel (a CAF 3.4 tree).  The port's changes are on top of it: the device tree, the camera's debug instrumentation, and the two USB patches |
| `krux` | the firmware's own source (the Krux project).  The port replaces some of its modules |
| `micropython` | MicroPython, built at the tag the port needs |
| `toolchain` | the AOSP prebuilt (the softfloat toolchain) and Arch's `arm-none-eabi-gcc` as two compressed packages |

## The two things that are not in the repository

* **the toolchain's unpacked tree** (2 GB) -- it is unpacked from the
  compressed packages that *are* here, and it is better kept outside
  the repository (on the disk, not in `/tmp`, which is a tmpfs here).
* **the build trees** (the kernel's 700 MB, the firmware's 170 MB) --
  they are regenerable and are described in the build guide.
