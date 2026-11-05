# QR display and the panel commit path

## Partial QR commits (2026-11-05)

The QR viewer draws with `lcd.draw_qr_code_binary()`, which is a long
pixel loop over the whole code.  It did not mark the frame as in
progress or mark it dirty at the end.  The quiet-period commit thread
could therefore commit the panel halfway through the QR; the later
flush then saw `dirty = 0` and did nothing, leaving the cut-off QR on
screen.  The About and Datum QR screens both use this path.

The function now does the same `lcd.dirty = 0; lcd.draw_count++` guard
as `lcd_display()` and calls `lcd_commit()` when the QR is finished, so
the panel only ever shows the completed code.

## BBQR compression

The MicroPython unix port's default ROM level enables deflate
**decompression** only.  `scripts/build/build-micropython.sh` now builds
with `-DMICROPY_PY_DEFLATE=1 -DMICROPY_PY_DEFLATE_COMPRESS=1`, which is
what lets `encode_bbqr()` produce zlib-wrapped deflate instead of
raising "Error compressing BBQR".
