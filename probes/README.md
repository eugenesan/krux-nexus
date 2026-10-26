# standalone/probes — the device-side probe programs

Small C programs that were compiled for the device (arm32, static) to
answer one question each.  They are kept as the record of what was
asked; the answers are in `standalone/cam/STATE.md` and
`airgap-standalone.log.md`.

| group | what they ask |
|---|---|
| `fb*.c`, `probe2.c`, `off*.c`, `pa.c`… | where does a write to the framebuffer actually land? (the panel is a multi-plane device with a padded stride) |
| `i2cscan.c`, `i2ctest.c` | what is on the I2C buses, and which bus is the NFC chip on? |
| `mknodtest.c` | why does `makedev()` fail here? (no devtmpfs) |
| `imx179*.c`, `caf_drivers_*.c` | the sensor driver, lifted out of the ROM's vendor tree |
| `ffs_*.c` | the USB gadget's file-system API |
| `sz*.c`, `varsz*.c` | the framebuffer's real size versus the one the driver reports |

The headers are the vendor's, lifted from the ROM:
`ab.h`, `aui33.h`, `c33.h`, `colour33.h`, `fm33.h`, `fm_master.h`
(the display and the FM radio), and `t_auibook.h`, `t_auipane.h`.

The NFC vendor headers are in `standalone/nfc/vendor-headers/`.
