#!/usr/bin/env bash
# Build camd, the camera daemon.
#
# camd owns the V4L2/ISP/CPP state machine; the MicroPython sensor module
# is only its socket client.  It is built static (the ramdisk has no
# libc of its own that is worth relying on) with the hard-float armel
# toolchain -- the same one the firmware's MicroPython is built with.
#
# The source is the only input: cam/camd.c.  The AE support (the W, E
# and V socket commands) and the AF sweep are part of the committed
# source, so there is no patch step any more.
#
# Usage:
#   bash scripts/cam/build-camd.sh [output]
set -e
OUT="${1:-cam/camd}"
cd "$(dirname "$0")/../.."

CC=${CC:-arm-linux-gnueabihf-gcc}

# -O2 with tree vectorisation and NEON: the box downscale and the
# unsharp mask are the per-frame hot spots, and the 8974 has NEON.
$CC -O2 -ftree-vectorize -mfpu=neon -mfloat-abi=hard -static -s -Wall \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -o "$OUT" cam/camd.c \
    -lpthread -lrt -lm 2>&1 | grep -E "error|warning: .*(uninitial|overflow)" || true

[ -x "$OUT" ] || { echo "!! camd was not built" >&2; exit 1; }

file "$OUT"
echo "  -> $OUT"
