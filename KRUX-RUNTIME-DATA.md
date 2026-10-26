# RUNTIME-DATA.md -- where the firmware keeps its settings and its secrets

The firmware is a **Krux** build: it keeps the user's settings and
the encrypted mnemonics (the "seeds") between boots, and the "Erase
User's Data" button in the firmware has to remove them.  This file
says where they are, what protects them, and what the wipe does.

## Where the data is

The firmware runs with **`/data/krux` as its working directory**
(the init stub `chdir`s there before it starts the firmware), and
the firmware's own paths are relative to it:

| path | what is in it |
|---|---|
| `/data/krux/flash/settings.json` | the settings (the theme, the brightness, the auto-shutdown timeout, ...) |
| `/data/krux/flash/seeds.json` | the encrypted mnemonics |
| `/data/krux/sd/` | the settings' copy on the SD card, if the user asked for one |
| `/data/krux/embit/` | the firmware's own working files |
| `/data/krux/micropython` | the interpreter |
| `/data/krux/krux/` | the firmware's Python package |

The upstream firmware's `krux/settings.py` uses the *relative*
paths `flash` and `sd`, and the init stub makes the same two
directories available under the root as symlinks:

```
/flash  -> /data/krux/flash
/sd     -> /data/krux/sd
```

so both the relative path the firmware uses and the absolute path a
person would type reach the same place.

**The data lives on the `data` partition**, which is the phone's
user data partition -- the same one the ROM's Android uses.  It is
not a partition of its own, and it is not encrypted by the kernel
(it is the ROM's `f2fs`, mounted by TWRP's init).

## What protects it

The directories are created by the init stub with the owner's
permissions only:

```
drwx------ root root   /data/krux/flash
-rw------- root root   /data/krux/flash/settings.json
-rw------- root root   /data/krux/flash/seeds.json
```

and the firmware is the only thing that runs as root in this
design, so the files are as private as the running system can make
them.  The mnemonics are stored **encrypted** (the firmware's own
encryption, with the key derived from the user's settings) -- the
file is a list of encrypted blobs, not the words themselves.

## The wipe

The firmware's "Erase User's Data" asks the `flash` module to erase
the SPIFFS area of the K210's SPI flash, which is where the upstream
firmware keeps the settings and the mnemonics.

**This device has no SPI flash**, and the offsets the firmware uses
(0xD00000 to 0x1000000) mean nothing here.  A module that mapped
them onto the wrong device -- the internal flash, the eMMC -- would
take the partition table or the bootloader with it, and **the phone
would not come back**.

So the port provides its own `flash` module
(`standalone/kruxport/kruxport/flash.py`), which is safe by
construction:

* it opens **no** block device and no device node at all -- it only
  ever touches the two files in the settings directory;
* it **refuses** any address outside the SPIFFS area, so the
  firmware's own loop (which walks the whole flash) cannot make it
  write anywhere else;
* `erase()` removes the settings and the mnemonics -- which is what
  the button says;
* `write()` raises: there is no firmware in a SPI flash on this
  device, so pretending to write one would be worse than failing.

**It lives in the settings directory as its `__init__.py`**, because
a directory wins over a module of the same name: the settings live
in a directory called `flash`, so `import flash` finds the package.

The check, on the device:

```
>>> import flash
>>> flash.read(flash.SPIFFS_ADDR, 32)
b'{"settings": {"appearance": {"th'
>>> flash.erase(0, 16)                      # outside the area
ValueError: flash: 0x0..0x10 is not in the SPIFFS area (0xd00000..0x1000000)
>>> flash.write(flash.SPIFFS_ADDR, b"x")
OSError: flash: this device has no flash to write to
```

## What is NOT wiped

* The **partition**: the wipe removes two files, not a partition.
  That is deliberate -- a wipe that touched the partitions could
  brick the device (and, on this device, would also take the
  firmware's own data with it).
* The **files outside the settings directory**: `/data/krux/embit`
  and the interpreter stay.
* The **ROM's Android data**: the wipe is the firmware's, and it does
  not touch the phone's user data partition as a whole.

## Open questions

* The settings live in the phone's user-data partition, so a
  "factory reset" in the ROM's Android would take them with it.
  A partition of their own would be better, but that is a
  partition-table change -- see the wipe review in the log.
* The files are not encrypted at rest: the settings are plain JSON
  and only the mnemonics are encrypted.  On a device whose data
  partition is a plain f2fs, a settings file that says "the tamper
  code is ..." is readable by anyone with the phone.
