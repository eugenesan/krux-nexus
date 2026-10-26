#!/bin/bash
# compare-roms.sh -- what is different between two boot images?
#
# The project has three boot images that matter:
#
#   images/boot-rom-YYYYMMDD.img      the ROM's own image (the author's kernel)
#   images/boot-kernelsu-YYYYMMDD.img  the author's kernel with KernelSU
#   our own image                      our kernel (the one in the boot partition)
#
# The ROM's kernel is built from unpublished commits, so the only way to
# see what the author changed is to compare his binaries with ours.
#
# This script does the cheap, reliable part of that comparison:
#
#   * the kernel's version string (which commit, which compiler);
#   * the device tree, decompiled and diffed (the DTB is not stripped,
#     so it is the most readable difference there is);
#   * the size and the hash of the kernel itself.
#
# Usage: compare-roms.sh [A.img] [B.img]
set -u
cd "$(cd "$(dirname "$0")/../../.." && pwd)" || exit 1

A=${1:-images/boot-rom-20260926.img}
B=${2:-images/boot-rom-20261011.img}
WORK=/tmp/opencode/cmp-roms
BOOTIMG=standalone/rootfs/bootimg.py

rm -rf "$WORK"; mkdir -p "$WORK"

extract() {   # extract <image> <name>
  local img=$1
  local name=$2
  local dir="$WORK/$name"
  mkdir -p "$dir"
  python3 "$BOOTIMG" unpack "$img" "$dir" >/dev/null 2>&1 || {
    echo "  !! cannot unpack $img"; return 1; }
  # the DTB is appended to the zImage; find it by its magic
  python3 - "$dir/kernel" "$dir/kernel.dtb" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
magic = 0xd00dfeed
pos = 0
found = None
while True:
    i = d.find(struct.pack(">I", magic), pos)
    if i < 0:
        break
    # the FDT header: totalsize at offset 4
    if i + 8 <= len(d):
        size = struct.unpack(">I", d[i + 4:i + 8])[0]
        if 0 < size < len(d) - i:
            found = (i, size)
            break
    pos = i + 4
if found:
    open(sys.argv[2], "wb").write(d[found[0]:found[0] + found[1]])
    print("  the DTB at %d, %d bytes" % found)
else:
    sys.exit(1)
PY
  # the version string lives in the kernel image itself
  strings -a "$dir/kernel" 2>/dev/null | grep -m1 -E "^Linux version 3\.4" > "$dir/version.txt"
  ls -la "$dir/kernel" | awk '{print "  the kernel: " $5 " bytes"}'
  echo "  the version: $(cat "$dir/version.txt" 2>/dev/null)"
  md5sum "$dir/kernel" | awk '{print "  the kernel md5: " $1}'
}

echo "=== A: $A"
extract "$A" a
echo
echo "=== B: $B"
extract "$B" b
echo

echo "=== the device trees"
if [ -f "$WORK/a/kernel.dtb" ] && [ -f "$WORK/b/kernel.dtb" ]; then
  dtc -I dtb -O dts -o "$WORK/a.dts" "$WORK/a/kernel.dtb" 2>/dev/null
  dtc -I dtb -O dts -o "$WORK/b.dts" "$WORK/b/kernel.dtb" 2>/dev/null
  echo "  the DTBs: $(stat -c %s "$WORK/a/kernel.dtb") and $(stat -c %s "$WORK/b/kernel.dtb") bytes"
  diff -u "$WORK/a.dts" "$WORK/b.dts" > "$WORK/dtb.diff" 2>/dev/null
  echo "  the DTB differences: $(wc -l < "$WORK/dtb.diff") lines"
  head -60 "$WORK/dtb.diff" | awk '{print "    " $0}'
else
  echo "  (no DTB found in one of the images)"
fi
