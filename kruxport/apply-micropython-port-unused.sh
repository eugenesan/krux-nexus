#!/bin/bash
# Apply the port's own files to a MicroPython tree.
#
# The unix port is not a bare port: the Nexus 5's I2C bus is the
# kernel's, reached through /dev/i2c-N and the i2c-dev ioctls, and
# MaixPy's `machine.I2C` API (which Krux's firmware uses) differs from
# the generic one in extmod/machine_i2c.c.  So the port brings its own
# I2C class and adds it to the `machine` module.
#
# Usage: apply-port.sh <path-to-micropython-tree>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TREE="${1:?usage: apply-port.sh <micropython-tree>}"
[ -d "$TREE/ports/unix" ] || { echo "  not a micropython tree: $TREE" >&2; exit 1; }

# 1. the I2C class, compiled as part of the unix port
cp "$HERE/machine_i2c_linux.c" "$TREE/ports/unix/machine_i2c_linux.c"

# 1b. the NFC chip's device node (the low-level half of the `nfc` module)
cp "$HERE/nfc_bcm2079x.c" "$TREE/ports/unix/nfc_bcm2079x.c"

# 1b. the rest of the machine module (the timer, the PWM and the watchdog)
cp "$HERE/machine_hw_linux.c" "$TREE/ports/unix/machine_hw_linux.c"

# 1c. the display: the framebuffer and the font
cp "$HERE/lcd_bcm.c" "$TREE/ports/unix/lcd_bcm.c"
cp "$HERE/lcd_font_8x16.h" "$TREE/ports/unix/lcd_font_8x16.h"

# 1d. the input devices (the touch screen and the keys)
cp "$HERE/evdev_bcm.c" "$TREE/ports/unix/evdev_bcm.c"

# 1e. the QR code generator: MaixPy's own qrcode module (the
#     firmware was written against it), with Nayuki's qrcodegen
cp "$HERE/qrcode_maixpy.c" "$TREE/ports/unix/qrcode_maixpy.c"
cp "$HERE/qrcodegen.c" "$TREE/ports/unix/qrcodegen.c"
cp "$HERE/qrcodegen.h" "$TREE/ports/unix/qrcodegen.h"

# 2. ports/unix/Makefile: compile them
python3 - "$TREE/ports/unix/Makefile" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
if "machine_i2c_linux.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tmachine_i2c_linux.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the I2C class added to the sources")
else:
    print("  ports/unix/Makefile: the I2C class is already there")
if "nfc_bcm2079x.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tnfc_bcm2079x.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the NFC device module added to the sources")
else:
    print("  ports/unix/Makefile: the NFC device module is already there")
if "machine_hw_linux.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tmachine_hw_linux.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the machine module added to the sources")
else:
    print("  ports/unix/Makefile: the machine module is already there")
if "lcd_bcm.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tlcd_bcm.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the display module added to the sources")
else:
    print("  ports/unix/Makefile: the display module is already there")
if "qrcode_maixpy.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tqrcode_maixpy.c \\\n\tqrcodegen.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the QR code generator added to the sources")
else:
    print("  ports/unix/Makefile: the QR code generator is already there")
if "evdev_bcm.c" not in s:
    marker = "\tmodtermios.c \\\n"
    assert marker in s, "the unix Makefile's source list has moved"
    s = s.replace(marker, marker + "\tevdev_bcm.c \\\n", 1)
    open(p, "w").write(s)
    print("  ports/unix/Makefile: the input device module added to the sources")
else:
    print("  ports/unix/Makefile: the input device module is already there")
PY


# 2b. ports/unix/main.c: the C stack limit.
#    The unix port allows only 40 KB of C stack, and the firmware's
#    import chain (krux.key -> embit.bip32 -> embit.ec -> embit.base58
#    -> embit.hashes -> embit.util -> ... -> embit.util.key) is deeper
#    than that, so the import dies with "maximum recursion depth
#    exceeded".  The main thread has 8 MB, so a bigger limit is safe.
python3 - "$TREE/ports/unix/main.c" <<'PY2'
import sys
p = sys.argv[1]
s = open(p).read()
old = "mp_uint_t stack_size = 40000 * UNIX_STACK_MULTIPLIER;"
new = "mp_uint_t stack_size = 500000 * UNIX_STACK_MULTIPLIER;"
if new in s:
    print("  ports/unix/main.c: the stack limit is already raised")
elif old in s:
    s = s.replace(old, new, 1)
    open(p, "w").write(s)
    print("  ports/unix/main.c: the stack limit is 500 KB now")
else:
    print("  ports/unix/main.c: the stack limit line has moved!")
PY2


# 2c. extmod/modcryptolib.c: the cipher mode constants.
#    The firmware's settings name the cipher modes (ucryptolib.MODE_ECB
#    and friends); MicroPython has the constants behind a config flag,
#    and no GCM at all.  The names are what the settings store, so the
#    missing one is added as well.
python3 - "$TREE/extmod/modcryptolib.c" <<'PY2'
import sys
p = sys.argv[1]
s = open(p).read()
old = """    #if MICROPY_PY_CRYPTOLIB_CTR
    { MP_ROM_QSTR(MP_QSTR_MODE_CTR), MP_ROM_INT(UCRYPTOLIB_MODE_CTR) },
    #endif"""
new = """    #if MICROPY_PY_CRYPTOLIB_CTR
    { MP_ROM_QSTR(MP_QSTR_MODE_CTR), MP_ROM_INT(UCRYPTOLIB_MODE_CTR) },
    #endif
    // The port: the firmware names AES-GCM among the modes (the
    // setting stores the name); MicroPython has no GCM.
    { MP_ROM_QSTR(MP_QSTR_MODE_GCM), MP_ROM_INT(4) },"""
if "MP_QSTR_MODE_GCM" in s:
    print("  extmod/modcryptolib.c: the GCM mode is already there")
elif old in s:
    s = s.replace(old, new, 1)
    open(p, "w").write(s)
    print("  extmod/modcryptolib.c: the GCM mode is added")
else:
    print("  extmod/modcryptolib.c: the constants block has moved!")
PY2


# 2d. ports/unix/mpconfigport.h: the cipher mode constants.
#    The firmware's settings name the cipher modes, so the constants
#    have to exist.  (The command line does not reach the compiler in
#    this port, so they go in the port's own configuration.)
python3 - "$TREE/ports/unix/mpconfigport.h" <<'PY2'
import sys
p = sys.argv[1]
s = open(p).read()
if "MICROPY_PY_CRYPTOLIB_CONSTS" in s:
    print("  ports/unix/mpconfigport.h: the cipher modes are already on")
else:
    marker = "#define MICROPY_PY_CRYPTOLIB (1)"
    if marker in s:
        s = s.replace(marker, marker + "\n"
                      "#define MICROPY_PY_CRYPTOLIB_CONSTS (1)\n"
                      "#define MICROPY_PY_CRYPTOLIB_CTR (1)", 1)
    else:
        # the port does not define it: put it at the end
        s = s.rstrip("\n") + "\n\n" \
            "// The port: the firmware names the cipher modes (ucryptolib.MODE_ECB\n" \
            "// and friends), so the constants have to exist.\n" \
            "#ifndef MICROPY_PY_CRYPTOLIB_CONSTS\n" \
            "#define MICROPY_PY_CRYPTOLIB_CONSTS (1)\n" \
            "#endif\n" \
            "#ifndef MICROPY_PY_CRYPTOLIB_CTR\n" \
            "#define MICROPY_PY_CRYPTOLIB_CTR (1)\n" \
            "#endif\n"
    open(p, "w").write(s)
    print("  ports/unix/mpconfigport.h: the cipher modes are on")
PY2


# 2e. extmod/modmachine.h: the declarations of the port's own types.
#    The globals table (in ports/unix/modmachine.c) refers to them, so
#    they have to be declared where that table is compiled.
python3 - "$TREE/extmod/modmachine.h" <<'PY2'
import sys
p = sys.argv[1]
s = open(p).read()
marker = "extern const mp_obj_type_t machine_i2c_type;"
# the port's own types, one by one: the header may already declare some
# of them (the extmod ones have the same names)
for name in ("machine_timer_type", "machine_pwm_type", "machine_wdt_type",
             "machine_sdcard_type"):
    if name in s:
        continue
    if marker in s:
        s = s.replace(marker, "extern const mp_obj_type_t %s;\n" % name + marker, 1)
        marker = "extern const mp_obj_type_t %s;" % name
    else:
        print("  extmod/modmachine.h: the declaration block has moved!")
        break
# The port's machine.reset(): the firmware's shutdown path calls it, and
# the unix port has none (MICROPY_PY_MACHINE_RESET is 0 there).
reset_decl = "MP_DECLARE_CONST_FUN_OBJ_0(machine_reset_obj);"
if reset_decl not in s:
    if marker in s:
        s = s.replace(marker, reset_decl + "\n" + marker, 1)
    else:
        print("  extmod/modmachine.h: the declaration block has moved!")
open(p, "w").write(s)
print("  extmod/modmachine.h: the port's types are declared")
PY2


# 3. ports/unix/modmachine.c: add the class to the machine module.
#    That file is included from extmod/modmachine.c (via
#    MICROPY_PY_MACHINE_INCLUDEFILE) before the module's globals table,
#    so this is where the port's extra globals go.
python3 - "$TREE/ports/unix/modmachine.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
# the globals the port adds to the machine module: the I2C bus, and
# the timer, the PWM and the watchdog.  The patch is idempotent: the
# block is replaced whatever is there.
globals_lines = [
    "#define MICROPY_PY_MACHINE_EXTRA_GLOBALS",
    "    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&machine_i2c_type) },",
    "    { MP_ROM_QSTR(MP_QSTR_Timer), MP_ROM_PTR(&machine_timer_type) },",
    "    { MP_ROM_QSTR(MP_QSTR_PWM), MP_ROM_PTR(&machine_pwm_type) },",
    "    { MP_ROM_QSTR(MP_QSTR_WDT), MP_ROM_PTR(&machine_wdt_type) },",
    "    { MP_ROM_QSTR(MP_QSTR_SDCard), MP_ROM_PTR(&machine_sdcard_type) },",
    "    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&machine_reset_obj) },",
]
globals_block = "".join(
    l + chr(92) + "\n" for l in globals_lines[:-1]) + globals_lines[-1] + "\n"

marker = "static void mp_machine_idle(void) {"
if "#define MICROPY_PY_MACHINE_EXTRA_GLOBALS" in s:
    # the port's own block from a previous run: replace it
    a = s.index("#define MICROPY_PY_MACHINE_EXTRA_GLOBALS")
    b = s.index(marker, a)
    s = s[:a] + globals_block + "\n" + s[b:]
    open(p, "w").write(s)
    print("  ports/unix/modmachine.c: the machine globals are up to date")
else:
    m = "\n" + marker
    assert m in s, "the unix modmachine.c has moved"
    glue = """
// The port's own classes for the machine module: the I2C bus, and
// the timer, the PWM and the watchdog (machine_hw_linux.c).
""" + globals_block + "\n"
    s = s.replace(m, glue + m, 1)
    open(p, "w").write(s)
    print("  ports/unix/modmachine.c: the machine globals are added")
PY
