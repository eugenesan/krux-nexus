#!/bin/bash
# capture-run.sh -- record one boot of the device, for comparison.
#
# The screen is the only real oracle for "the firmware took the display", but
# a human has to be there for it.  Everything else can be recorded, so this
# script saves what the device can be asked:
#
#   * the kernel's dmesg (the display driver's messages are in it);
#   * the framebuffer, as a raw dump and as a PNG, so that what was drawn
#     can be looked at afterwards rather than only photographed;
#   * the processes, the framebuffer geometry, the backlight;
#   * the firmware's own log.
#
# Usage: capture-run.sh <label>
#
# Writes into /tmp/opencode/runs/<label>/.

set -u

LABEL="${1:-run}"
OUT="/tmp/opencode/runs/$LABEL"
HERE="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$OUT"

say() { printf '  %s\n' "$*"; }

say "collecting from the device..."

# The kernel's log.
timeout 120 adb shell 'dmesg 2>/dev/null' >"$OUT/dmesg.txt" 2>&1

# The processes.
timeout 60 adb shell 'ps -A 2>/dev/null' >"$OUT/ps.txt" 2>&1

# The framebuffer's geometry, the backlight, and the pan offset.
timeout 60 adb shell 'cat /sys/class/graphics/fb0/virtual_size 2>/dev/null;
                     cat /sys/class/graphics/fb0/pan 2>/dev/null;
                     cat /sys/class/graphics/fb0/stride 2>/dev/null;
                     cat /sys/class/backlight/lm3630/brightness 2>/dev/null' \
    >"$OUT/fb.txt" 2>&1

# The framebuffer's contents: the first plane, raw and as a PNG.
#
# The panel scans one of the three planes; the others are what the
# firmware drew but was never shown (or what the panel showed before).
# Dumping the first plane is enough to tell "what is on the panel now"
# from "what is in memory".
timeout 300 adb shell 'dd if=/dev/graphics/fb0 bs=4352 count=1920 2>/dev/null' \
    >"$OUT/plane0.raw" 2>/dev/null
python3 "$HERE/fb2png.py" "$OUT/plane0.raw" "$OUT/plane0.png" >/dev/null 2>&1

# The firmware's log, if the firmware got as far as writing one.
timeout 60 adb shell 'cat /data/krux/fw.log 2>/dev/null' >"$OUT/fw.log" 2>&1

# The display driver's own messages, which is the interesting part.
grep -iE 'mdss|dsi|panel|splash|adf|fb' "$OUT/dmesg.txt" >"$OUT/display.txt" 2>&1

say "saved in $OUT:"
say "  dmesg.txt      $(wc -l <"$OUT/dmesg.txt") lines"
say "  plane0.png     $(stat -c%s "$OUT/plane0.png" 2>/dev/null || echo 0) bytes"
say "  fw.log         $(wc -c <"$OUT/fw.log") bytes"
