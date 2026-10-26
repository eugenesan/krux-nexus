# Route B — minimal Linux + MicroPython + Krux on the Nexus 5

Goal: replace Android's userspace with a small BusyBox/MicroPython root that runs
Krux, keeping the **stock CAF kernel** for as long as possible, and eventually
compiling out the radios from the kernel.

Design principle, applied in order:

1. **No kernel changes until the userspace works.** Use the kernel already
   shipping in `boot.img`. Validate display, touch and NFC against a root you
   fully control before touching the thing underneath it.
2. **Android stays alive.** Both roots boot from the same device and the same
   `/data`; switching is a reboot, not a reflash (see §4).
3. **The kernel build is the last step, not the first.** See §6.

---

## 1. What the hardware actually offers (measured 2026-10-18)

All of this was read off the device, not assumed.

| | measured |
|---|---|
| NFC bus | **I2C bus 0, addr 0x77, name `bcm2079x`** |
| NFC node | `/dev/bcm2079x`, char 10:65, opens successfully |
| NFC driver | `CONFIG_NFC_BCM2079X=y` — **built into the kernel** |
| NFC HAL | `init.svc.vendor.nfc_hal_service` running, `nfc.initialized=true` |
| NFC firmware | `bcm2079x-b5_firmware.ncd` (10101 B)<br>`bcm2079x-b5_pre_firmware.ncd` (571 B) |
| NFC chip | BCM20793M, speaks **NCI** (standard, not Broadcom-proprietary) |
| NFC init sequence | `LPTD_CFG`, `PREINIT_DSP_CFG`, `NFA_DM_START_UP_CFG` in<br>`/system/vendor/etc/libnfc-nci-20791b05.conf` |
| Touch | `/dev/input/event1` `touch_dev`, raw evdev<br>`ABS_MT_SLOT` max 9, `X` 0–1079, `Y` 0–1919, `TRACKING_ID`, `PRESSURE` |
| Display | `/dev/graphics/fb0`, `mdssfb_90000`, **32 bpp**, 1080×1920, **stride 4352** |
| Console | `cdc_acm` present — USB gadget serial works |
| Radio topology | NFC is a **separate chip** (I2C `0x77`) from the BCM4339 Wi-Fi/BT combo (SDIO) |

Three consequences, and they are the reason this route was chosen:

- **No kernel work is needed for NFC.** The driver is already built in and
  serving a device node. The worst case — writing an I2C driver for an
  undocumented chip — does not arise.
- **The touchscreen is raw evdev with real multi-touch.** Userspace gets
  `ABS_MT_*` events directly; no kernel input translation layer.
- **The display is a plain framebuffer.** No DRM, no GPU. `mmap` and write
  pixels. Note the **stride is 4352, not 4320** — it is padded, and a framebuffer
  blit that assumes `width*4` will be subtly skewed.

Also note: **TWRP is a minimal non-Android Linux and it boots on this kernel.**
That is the existence proof for §3.

---

## 2. Phase 0 — capture the reference (no new code)

Everything above works *today* under Android. Capture it before building
anything, so the port has something to be compared against.

- [ ] Read an ISO-DEP target and log the full APDU exchange
- [ ] Copy `/data/nfc/param` — the HAL writes it at init; the port must reproduce it
- [ ] Record the HAL's ioctl sequence on `/dev/bcm2079x` (strace/ltrace on the HAL)
- [ ] Record the exact chip init byte-blobs actually sent
- [ ] Dump `fb0` and save the stride/format facts
- [ ] Capture the evdev stream for a single tap
- [ ] Record `getprop` values that matter and Android's kernel cmdline

**A real card is not needed for any of this.** See §5.

---

## 3. Rootfs, on the stock kernel

Replace only the **ramdisk** inside `boot.img`; keep kernel, DTB, cmdline.

Needs exactly three device nodes:

```
/dev/graphics/fb0    display
/dev/input/event1    touch
/dev/bcm2079x        NFC
```

Contents: BusyBox, MicroPython, Krux, a `cdev`/init, and a dev shell.
For bring-up you need a way in: `cdc_acm` is in the kernel, so a USB serial
console is the cheapest option; `dropbear` over the gadget network is the
comfortable one.

---

## 3b. The runtime stub (the firmware as TWRP's payload)

The device has no serial console, so adb is the only way in — which is
why the kernel work to make adb work under TWRP's ramdisk mattered. The
device boots TWRP's ramdisk, and the `recovery` service in it is
**our** init stub (`rootfs/init-stub.c`) rather than TWRP's own init:

* the stub waits for adb, then for the **volume up key**;
* the key **held** ⇒ TWRP, exactly as before (TWRP's binary is kept as
  `/sbin/recovery.stock`);
* the key **not held** ⇒ the firmware: TWRP's user interface is stopped
  and the firmware takes the display and the touch screen.

So the firmware is the default payload and the volume up key is the way
back to TWRP — a device that looks like a perfectly ordinary TWRP to
anyone who does not know. TWRP's init stays PID 1, so the mounts, the
USB gadget and adb all come up exactly as they always did.

The stub is built and packed by three scripts, in this order:

```sh
rootfs/build-stub-init.sh      # init-stub.c -> rootfs/stub/krux-init
rootfs/build-stub-ramdisk.sh   # TWRP's ramdisk + the stub -> rootfs/stock/ramdisk-stub
rootfs/make-boot-image.sh      # kernel + ramdisk -> rootfs/stock/boot-stub.img
```

Each step is verified: the init stub rebuilds **byte-for-byte** identical
to the one that ships, and the ramdisk rebuilds to the same *content*
(the cpio itself carries the filesystem's inode numbers and mtimes, so
two packs of one tree differ in bytes but not in what is in them).

`stock/boot-stub.img` is the image to flash, and `stock/kernel-stub` is
the kernel it pairs with.

---


Both roots live on the same device and the same `/data`.

**Preferred — one boot image, rootfs chosen at runtime.**
The ramdisk is a small init that mounts `/data`, reads a flag, and either
`switch_root`s into the Krux rootfs or hands off to Android. Switching is
`touch /data/nokrux && adb reboot`. No flashing at all, and Android's state is
never disturbed. Put the rootfs at `/data/krux/` with a marker file so nothing
in Android walks into it.

**Fallback — two boot images, fastboot swap.**
`adb reboot bootloader && fastboot flash boot <other>.img && fastboot reboot`.
Ten seconds, fully reversible, `/data` untouched. Use this if the runtime-flag
approach fights Android's FDE or `init`.

Either way: **Android is the reference implementation and the fallback.** If the
port breaks, you have a working NFC stack to compare against.

---

## 5. Test hardware: a real card is not needed for bring-up

Bring-up needs no card at all. Chip init, firmware download, NCI core reset and
the poll loop all happen with nothing in the field.

For ISO-DEP specifically, you need *any* ISO-DEP **target**:

| | usable | why |
|---|---|---|
| Cheap NTAG / Ultralight tag | **best** | accepts arbitrary APDUs and echoes; ~€1 |
| Transit / access card | good | permissive, and you own several |
| Bank card | works, poor test | no arbitrary SELECT; rate-limited; may lock |
| Purpose-built APDU test card | ideal | scripted responses |

Note the ROM already carries `AID_FOR_EMPTY_SELECT={08:A0:00:00:01:51:00:00:00}`
— the payment AID — so a contactless card works as a target out of the box.

**A generic tag validates the transport, not your card's flow.** Only the
AID-specific SeedKeeper exchange needs the real JavaCard. So: build against a
tag, finish everything else, and leave the final card test to when you have one.

---

## 6. Kernel build is the LAST step

Phase 0–5 run on the **stock shipping kernel**. Nothing above needs it changed.

When you get there, `standalone/tools/profiles/hammerhead.list` already exists and is
already correct for this device: it strips `BCMDHD`/`CFG80211`/`WEXT_*`,
`BT_RFCOMM`/`BT_BNEP`/`BT_HIDP` and `MSM_PIL_MSS_QDSP6V5`, and explicitly
**keeps `CONFIG_NFC_BCM2079X`**. `build/build-kernel.sh` drives it.

Order matters: validate userspace first, then change the layer beneath it. If
something breaks afterwards you know which layer broke, which is not true if
you do both at once.

---

## 7. Working copies

Ported code goes in copies, never in the originals:

```
~/Develop/src/krux-hammerhead/
  krux/            clone of eugenesan/krux @ unified-sighash_seedkeeper   275M
  kruxmobileapp/   clone of eugenesan/kruxmobileapp @ same branch         221M
```

Both are local clones (no network auth needed), same branch as
`~/Develop/src/krux-sighash/`, build artifacts excluded. Changes land as
commits here so they are reviewable as a diff against your fork.

`seedsigner/` is not cloned yet — we are going with Krux, and it has uncommitted
patch files in the original that should be sorted out before cloning.

---

## 8. Gaps on this machine

Nothing cross-buildable is installed. Needed before Phase 3:

| tool | why |
|---|---|
| `arm-linux-gnueabihf-gcc` or a Buildroot toolchain | the rootfs is ARM |
| `buildroot` | the whole rootfs, reproducibly |
| `dtc` | not needed for the rootfs (DTB comes from `boot.img`) |
| `cmake`, `libusb`, `libudev` | for the NFC transport port |

`git`, `make` and `python3` are present. 56 GB free.