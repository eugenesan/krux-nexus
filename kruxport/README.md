# kruxport — firmware build output

This directory holds only the built firmware:

```
kruxport/micropython-arm32      # the static arm32 MicroPython interpreter (gitignored)
```

The sources are no longer here. They live in the submodules:

| what | where |
|---|---|
| the port's Python modules | `krux/src/` (and `krux/src/krux/`), branch `nexus-port` |
| the port's C modules | `micropython/ports/unix/`, branch `master` |
| the build recipe | `scripts/build/build-micropython.sh` |

Build it with:

```sh
bash scripts/build/build-micropython.sh
```

The upstream tree is the `micropython` submodule; the build takes a worktree of
it at `HEAD` in `BUILD=` (default `/tmp/opencode/mp-build`) and links the port
modules that are committed in it. The armel sysroot defaults to
`/tmp/opencode/armel-sysroot` and is rebuilt by
`scripts/build-armel-sysroot.sh` when missing.

Deploy it with `scripts/deploy-firmware.sh`; the firmware log is
`/data/krux/fw.log` on the device.

See `docs/build-guide.md` for the full build, `docs/directories.md` for the
layout and `docs/nfc-isodep.md` / `docs/nfc-transport.md` for the NFC work.
