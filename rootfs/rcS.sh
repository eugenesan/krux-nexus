#!/bin/sh
# rcS -- run by busybox init as ::sysinit
#
# Order is deliberate and is the lesson from five failed boots:
#   1. USB gadget FIRST. If anything later blocks or crashes, adb is already
#      up and we can see what happened instead of staring at a bootloader logo.
#   2. framebuffer SECOND, and only a SMALL region -- a full three-buffer fill
#      is what wedged the previous attempt.
#
# Measured on this device (b/phase0/):
#   fb0: 1080x1920 32bpp, STRIDE 4352 (not width*4 = 4320), triple buffered
#   buffer 2 does NOT exist: a full write to it fails with EFBIG after 16 rows
#   /dev/bcm2079x: char 10:65

say() { echo "rcS: $*"; }

/dev/graphics/fb0 >/dev/null 2>&1
/dev/bcm2079x     >/dev/null 2>&1

# ---------------------------------------------------------------- 1. USB first
say "enabling USB gadget (adb acm)"
A=/sys/class/android_usb/android0
if [ -d "$A" ]; then
    echo 0      > "$A/enable"       2>/dev/null
    echo 0x18d1 > "$A/idVendor"     2>/dev/null
    echo 0x4ee2 > "$A/idProduct"    2>/dev/null
    echo "adb acm" > "$A/functions" 2>/dev/null || echo adb > "$A/functions" 2>/dev/null
    echo 1      > "$A/enable"       2>/dev/null
    say "  functions=[$(cat $A/functions 2>/dev/null)] state=$(cat $A/state 2>/dev/null)"
    say "  ttyGS: $(ls /dev/ttyGS* 2>/dev/null || echo none)"
else
    say "  $A missing"
fi

# ---------------------------------------------------------------- 2. framebuffer
# Paint ONE band, small, into buffers 0 and 1 only. Buffer 2 is not backed by
# the device and writes to it fail.
say "painting a small band (buffers 0 and 1 only)"
dd if=/dev/zero bs=4352 count=400 2>/dev/null | tr '\000' '\377' > /tmp/band.raw
dd if=/tmp/band.raw of=/dev/graphics/fb0 bs=4352 seek=0    conv=notrunc 2>/dev/null
dd if=/tmp/band.raw of=/dev/graphics/fb0 bs=4352 seek=1920 conv=notrunc 2>/dev/null
say "  painted 400 rows x2 buffers"

# ---------------------------------------------------------------- 3. NFC
say "/dev/bcm2079x present: $([ -c /dev/bcm2079x ] && echo yes || echo no)"
if [ -x /nfc-probe ]; then
    say "nfc-probe available"
fi

say "done. adb should now be visible on the host."

# ---------------------------------------------------------------- 4. the firmware
# The Krux firmware, run by the static MicroPython the port builds.  It
# draws on the framebuffer and reads the touch screen, so it owns the
# display from here on.
if [ -x /usr/bin/micropython ] && [ -f /opt/krux/boot.py ]; then
    say "starting the firmware (MicroPython)"
    cd /opt/krux || say "  /opt/krux is missing"
    /usr/bin/micropython /opt/krux/boot.py >/tmp/krux.log 2>&1 &
    sleep 2
    say "  the firmware's log: /tmp/krux.log"
    head -5 /tmp/krux.log 2>/dev/null | while read -r line; do say "    $line"; done
else
    say "no firmware: micropython $( [ -x /usr/bin/micropython ] && echo present || echo missing ), boot.py $( [ -f /opt/krux/boot.py ] && echo present || echo missing )"
fi
