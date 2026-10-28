# scripts — build drivers and device probes

Two kinds of scripts live here:

* the **build drivers** — the recipes that produce something (the firmware,
  the images, the deploy);
* the **probes** — the throwaway helpers written while the camera, NFC and
  display were being brought up. They are not part of the build; each one is
  a self-contained probe or a one-off capture driver, and most talk to the
  device over adb.

The probes are kept because they are the *record* of how the findings were
made — a probe is usually the only description of the question it was asking.

| path | what is in it |
|---|---|
| `build/` | the build drivers: `build-micropython.sh` (the firmware), `build-imaging.sh` (the camera server), the kernel variants |
| `cam/` | the camera investigation: capture drivers, HAL dumps, register/routing dumps |
| `nfc/` | the NFC transport work: the transport probes and card readers |
| `display/` | the framebuffer: dumps, crop, touch and key probes |
| `rootfs/` | the userspace: device-node and mount probes, init behaviour |
| `boot/` | flashing and booting the device, and the one-shot runs that tie a capture to a boot |
| `kernel/` | the kernel tree itself: branch and upstream checks, ROM-reference comparisons, rebase and repack |
| `misc/` | the leftovers that do not fit anywhere else |
| `archive/` | dead scripts, kept by topic |

Shared drivers at the top level:

* `scripts/build/build-micropython.sh` builds the static arm32 interpreter and
  writes `kruxport/micropython-arm32`;
* `scripts/build-armel-sysroot.sh` builds the armel sysroot the firmware is
  linked against;
* `scripts/deploy-firmware.sh` pushes the interpreter, `krux/src`, embit and
  the port modules to the device and restarts the service.

Two of the probes are worth knowing about:

* `scripts/build/build-imaging.sh` builds the imaging server
  (`cam/imaging*.c`) for the device. It is the only copy of that recipe;
  `cam/` has the sources but not the build.
* `scripts/rootfs/devnodes.py` is the same file as `tools/devnodes.py` — the
  device-node list that the ramdisk builders bake in.

The probes' sources are in `probes/`.

## Paths

`config.sh` holds the paths these scripts share, and is meant to be
sourced rather than run:

```sh
. "$(dirname "$0")/../config.sh"
```

Two kinds of path, and the difference is the whole point of the split:

* the **project** holds what is ours and survives a reboot — the kernel
  tree, the port, the captured ROM data;
* **tmpfs** holds what is rebuilt — the kernel's object tree, the
  firmware's build tree, and the scratch the captures write into.

So the scripts read from the project and write to tmpfs, and every path
can be pointed elsewhere by exporting the variable first
(`KERNEL_OUT=...`, `MP_BUILD=...`, `SCRATCH=...`).
