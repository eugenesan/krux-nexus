# Scripts triage (tools/ vs scripts/)

## Summary
- `tools/` holds reusable build/config helpers: `build-kernel.sh`, `apply-defconfig.py`, `fetch-toolchain.sh`, `audit-zip.sh`, `sdat2img.py`, `extract-vmlinux.py`, profiles.
- `scripts/` also holds the real build drivers: `build-armel-sysroot.sh`, `build/build-micropython.sh`, `deploy-firmware.sh`.
- `scripts/` holds ~100 one-off probe/capture/boot helpers from the camera, NFC and display investigations.
- `scripts/README.md` already groups them by topic (boot, build, cam, display, kernel, misc, nfc, rootfs).

## Recommendation
Keep `tools/` and `scripts/` separate: `tools/` is build infrastructure, `scripts/` is device investigation. Merging them would mix reproducible build steps with throwaway probes.

## What is still reusable
- scripts/boot/: `boot-our-firmware.sh`, `boot-twrp.sh`, `final-run.sh`
- scripts/build/: `build-imaging.sh`, `build-micropython.sh`, `build-kernel.sh`-adjacent helpers
- scripts/rootfs/: `devnodes.py`
- scripts/kernel/: `cmp-kernels.sh`, `check-rom-kernel.sh`
- scripts/cam/: `camtest.sh`, `runcam.sh`, `imaging.sh`, `pull-vendor.sh`
- scripts/nfc/: `nfc-hw.sh`, `nfc-live.sh`

## Candidates for removal (historical, one-off)
Most of `scripts/cam/capture*.sh`, `scripts/cam/*diag*.sh`, `scripts/cam/sweep*.sh`,
`scripts/boot/fix-capture*.sh`, `scripts/misc/cleanup-trees*.sh`, `scripts/nfc/nfc_bi*.py`,
`scripts/nfc/nfc_step.py`. These encode questions that have already been answered
and are recorded in docs/project-log.md. Keep a few representative ones for
reference; the rest can be archived.

## Action
The dead ones were moved to `scripts/archive/` (by topic).  The stale
`scripts/build/build-mpy.sh` (an older MicroPython build that predates the
submodule layout) was removed in favour of `scripts/build/build-micropython.sh`.
