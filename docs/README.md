# Documentation index

The repository's documentation is deliberately small.  If a document is not
listed here, it is either archived (see [archive/](archive/)) or it is
history in the git log.

## Where to start

| you want to | read |
|---|---|
| know what works right now | [cam-state.md](cam-state.md) — the camera, verified |
| build the firmware, the kernel and the boot image | [build-guide.md](build-guide.md), [build-kernel.md](build-kernel.md) |
| work on packaging (the three delivery modes) | [packaging-plan.md](packaging-plan.md) |
| see what is left to do | [todo.md](todo.md) |
| understand the layout | [directories.md](directories.md) |
| secure the delivered image (logs, storage, RAM) | [security-audit.md](security-audit.md) |

## The camera

| document | what it holds |
|---|---|
| [cam-state.md](cam-state.md) | **the camera's current state**: the PIX/CPP path, the geometry, the verified QR and entropy results, the device state |
| [cam-plan.md](cam-plan.md) | how the camera is put together, from V4L2 to the sensor shim |
| [cam-integration.md](cam-integration.md) | how `camd` and the firmware talk to each other |
| [cam-frame-path.md](cam-frame-path.md) | the measured frame path (CAMIF_RAW + CPP) and its throughput |
| [cam-bringup-vfe.md](cam-bringup-vfe.md) | the VFE overflow analysis and the PIX_VIEWFINDER path |
| [cam-autofocus.md](cam-autofocus.md) | the DW9714 VCM: the vendor's sequence, the table, the bisect, the calibration |
| [cam-ae-af-plan.md](cam-ae-af-plan.md) | the auto-exposure and auto-focus plan and its measurements |
| [cam-bringup-auto-exposure.md](cam-bringup-auto-exposure.md) | the auto-exposure bring-up: the sensor registers and what they do |
| [cam-corpus-findings.md](cam-corpus-findings.md) | the capture corpus: crop, field of view, QR readability |
| [cam-stability-thermal.md](cam-stability-thermal.md) | the stability investigation: a software fault, not thermal |
| [display-qr.md](display-qr.md) | the QR display partial-commit fix |

## NFC

| document | what it holds |
|---|---|
| [nfc-isodep.md](nfc-isodep.md) | the ISO-DEP layer: architecture and the port plan |
| [nfc-transport.md](nfc-transport.md) | the BCM2079x NCI transport and its contract |

## The build and the tree

| document | what it holds |
|---|---|
| [build-guide.md](build-guide.md) | building the firmware, the kernel, the ramdisk and the boot image |
| [build-kernel.md](build-kernel.md) | the kernel's air-gap profile and the LineageOS layers |
| [toolchain.md](toolchain.md) | the host toolchains, current and historical |
| [directories.md](directories.md) | the project's layout |
| [scripts-triage.md](scripts-triage.md) | the tools/ vs scripts/ split and the archive decisions |
| [krux-runtime-data.md](krux-runtime-data.md) | where the firmware keeps its settings and seeds at run time |
| [hw-bringup.md](hw-bringup.md) | how the hardware was brought up: the methods, the tools and the lessons |
| [phase0-findings.md](phase0-findings.md) | the phase 0 measurements taken on the stock phone |
| [memory-wipe-state.md](memory-wipe-state.md), [memory-wipe-continue.md](memory-wipe-continue.md) | the paused RAM-wipe experiment: where it stands and how to pick it up |
| [todo.md](todo.md) | the live task list for every area |
| [packaging-plan.md](packaging-plan.md) | the three delivery modes, and the two Krux trees |

## Archive

[archive/](archive/) holds the superseded documents and the raw captures.
They are kept for the record and are **not** part of the current state; the
two machine-written session logs (`project-log.md`, `compaction-log.md`) are
not even tracked any more — their history is in the git log.

The raw evidence that is still worth having lives in
[archive/vfe-request-2026-11-11.txt](archive/vfe-request-2026-11-11.txt)
(the vendor's VFE stream request), [archive/media.camera-2026-10-28.txt](archive/media.camera-2026-10-28.txt)
(the stock camera service's configuration dump) and
[archive/cppmsg-still-455.bin](archive/cppmsg-still-455.bin) (the vendor's
455-word CPP still message).

## Checking the links

```sh
python3 scripts/misc/check-doc-links.py docs
```

## Running the Krux test suite

```sh
scripts/misc/run-krux-tests.sh
```

One upstream test — the datum tool's "scan a QR" abort test — allocates
without bound; it grows past 1.5 GB and keeps going, and it is a known
problem in the krux-mobile tree too.  The runner caps the address space
and leaves that test out, so a test run cannot take the machine down.
