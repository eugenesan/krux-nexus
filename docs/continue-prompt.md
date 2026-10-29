# Continuation prompt

Paste this into a fresh session to pick up the Nexus 5 air-gapped Krux work.
Read `docs/project-log.md` (the full history) and `docs/compaction-log.md`
(the older compaction history) first; this page is the current map.

---

## Situation

We are building an **air-gapped Bitcoin signing device** from an LG Nexus 5
(`hammerhead`): no modem, Wi-Fi, Bluetooth, privileged permissions or
signature permissions.  The userspace is a minimal TWRP-based ramdisk that
runs our static MicroPython and Krux; adb is the only debug channel and every
boot is reversible (`fastboot boot`, or the TWRP volume-up escape).

The port keeps upstream Krux almost untouched.  Port code lives in:

| what | where |
|---|---|
| Krux package and port modules | `krux/src/` (submodule, branch `nexus-port`) |
| MicroPython C modules | `micropython/ports/unix/` (submodule, branch `master`) |
| kernel air-gap profile | `tools/profiles/hammerhead.list`, `linux/` submodule |
| init stub, ramdisk, boot image | `rootfs/` |
| build/deploy drivers | `scripts/` |

**A second Nexus 5 is now the test device.**  Its NFC is reliable: operations
fail rarely, the card can stay attached for the whole sequence, and saving to
the card works.  The first phone's card coupling was marginal (Android's own
reader was inconsistent there too); do not treat those RF failures as a
driver bug.

---

## Current state (2026-10-28)

### Working

* Full SeedKeeper secure channel against the real card: `0x81` handshake,
  manual ephemeral-key recovery, AES-128-CBC/HMAC-SHA1, PIN verify, list,
  export, free space, import/save.
* PIN is asked **before** the card is presented, matching the official
  SeedKeeper Android app.  Save asks for the label first, then the PIN, then
  one uninterrupted card session.
* The SeedKeeper page retries a session up to 20 times (8 s activation
  window), retries read actions with a fresh secure channel, and offers
  tap-to-cancel between attempts.
* Card Info is a PIN-free status read, useful as a link test.
* Host tests: **82 passed** for `tests/test_seedkeeper.py` +
  `tests/pages/test_seedkeeper.py` (using the host `evdev` stub and the
  vendored embit).
* `nfc/blobs/*.ncd` are **not required at runtime**; the chip already has
  firmware and `nfc.py` does not download a patch.  They are reference only.

### Just changed, needs final validation

* `rootfs/init-stub.c` prefers a firmware bundled at `/opt/krux` and falls
  back to `/data/krux` (development deploy).
* `rootfs/build-stub-ramdisk.sh --bundle` copies MicroPython + Krux + embit
  into the ramdisk; `make-boot-image.sh` accepts `RAMDISK=`.
* The static interpreter moved from `kruxport/` to
  `rootfs/micropython-arm32`; `kruxport/` is being removed.
* `scripts/deploy-firmware.sh` creates all target directories (including
  embit `wordlists`) and uses the new interpreter path.
* Docs: `project-log.md` is being renamed `project-log.md`; the README now
  carries the old `kruxport/README.md` build/deploy information.

### Next, in order

1. **Connect the new Nexus 5** and confirm `adb devices` shows it.
2. **Rebuild everything from source**: kernel, MicroPython, init stub,
   development ramdisk and bundled ramdisk/boot image.  See
   `docs/build-guide.md` §“The whole thing, in one go”.
3. **Final NFC check** with the rebuilt kernel + MicroPython + boot image:
   Card Info (no PIN), View Secrets, Load Seed, and one Save.  Use PIN
   `123456` only if testing; do not spend card PIN tries.
4. **Commit** the `krux` submodule (including the port modules and the embit
   submodule bump), then the main repository.  The port's untracked files in
   `krux/src/` must be committed or the next checkout loses them.
5. Only then start camera Phase A from `docs/plan-camera.md`.

---

## Useful commands

```sh
cd ~/Develop/src/krux-nexus/krux

# tests (host)
PYTHONPATH=/tmp/opencode/teststubs:vendor/embit/src:src \
    /tmp/opencode/venv/bin/python -m pytest \
    tests/test_seedkeeper.py tests/pages/test_seedkeeper.py -q

# build the interpreter
bash scripts/build/build-micropython.sh

# development deploy (interpreter + Python to /data/krux)
bash scripts/deploy-firmware.sh
# or without restart:
DEPLOY_RESTART=0 bash scripts/deploy-firmware.sh

# read-only bundled image
bash rootfs/build-stub-ramdisk.sh --bundle
RAMDISK=rootfs/stock/ramdisk-bundled \
    bash rootfs/make-boot-image.sh \
    artifacts/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img \
    /tmp/opencode/ko/arch/arm/boot/zImage-dtb \
    rootfs/stock/boot-bundled.img

# boot it without flashing
adb reboot bootloader
fastboot boot rootfs/stock/boot-stub.img
```

Device firmware log: `/data/krux/fw.log`.  Only one process may use
`/dev/bcm2079x`; stop the `krux` service before raw NFC probes and restart it
afterwards.  Restarting blanks the screen briefly and takes ~20 s.

---

## Constraints that still matter

* **Air gap**: no radios, no privileged/signature permissions, no network.
* **Reversibility**: prefer `fastboot boot`; do not flash unless asked.
* **Ask with a BEL (`\7`) before any physical device action.**
* **Do not modify card contents** (import/export/delete/save) without explicit
  permission.  The card holds real secrets.
* **PIN tests**: only `123456`; never guess, to avoid consuming tries.
* **Preserve the logs.**  `docs/project-log.md` and
  `docs/compaction-log.md` are append-only history.
* **Minimise upstream divergence**: port-level shims, not rewrites.
* Camera work stays deferred until SeedKeeper is signed off on the new phone.

---

## Known traps

* The Nexus 5 NFC antenna is in the **back cover around the camera**; the
  on-screen prompt names that spot.  The first phone was marginal even in
  Android; the second is reliable.
* `0xB2` is `CORE_INTERFACE_ERROR` / RF timeout; the NFCC tears the link down,
  so retry a **new activation**, not the same APDU.
* `0x9C0F` is the applet's invalid-parameter status; import must send
  `header[2:]`, not `header[4:]`.
* This MicroPython hashlib has no `hexdigest()`; use `.digest().hex()`.
* The boot image header addresses and ramdisk size come from a real image
  (`rootfs/bootimg.py unpack`), never from guessed constants.
* `/data/krux/krux/pages/disclaimer.py` is **not** in this repository; it was
  a stale file on the old device.  A fresh deploy does not create it.
