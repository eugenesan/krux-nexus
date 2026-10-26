# standalone/scripts — the session's working scripts

These are the throwaway helpers that were written while the camera, NFC
and display were being brought up.  They are not part of the build: each
one is a self-contained probe or a one-off capture driver, and most of
them talk to the device over adb.

They are kept because they are the *record* of how the findings were
made — a probe is usually the only description of the question it was
asking.  They are grouped by what they were for:

| dir | what is in it |
|---|---|
| `cam/` | the camera investigation: the capture drivers (`capture*.sh`, `run-imaging*.sh`), the HAL dumps (`hal-*.sh`), the register and routing dumps (`dump-*.sh`, `csid*.sh`, `ispif*.sh`), and the ones that pick the capture apart (`find-cpp*.sh`, `find-sensor-data.sh`) |
| `nfc/` | the NFC transport work: the transport probes and the card readers |
| `display/` | the framebuffer: the dumps, the crop and the touch and key probes |
| `rootfs/` | the userspace: the device-node and mount probes, and the ones that read the init's behaviour |
| `boot/` | flashing and booting the device, and the one-shot runs that tie a capture to a boot |
| `kernel/` | the kernel tree itself: the branch and upstream checks, the ROM-reference comparisons, the rebase and repack |
| `build/` | the build drivers (the imaging server, the kernel, the port) |
| `misc/` | the leftovers that do not fit anywhere else |

Two of them are worth knowing about:

* `scripts/build/build-imaging.sh` builds the imaging server
  (`standalone/cam/imaging*.c`) for the device.  It is the only copy of
  that recipe; `standalone/cam/` has the sources but not the build.
* `scripts/rootfs/devnodes.py` is the same file as
  `standalone/tools/devnodes.py` — the device-node list that the
  ramdisk builders bake in.

The probes' sources are in `standalone/probes/`.

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
