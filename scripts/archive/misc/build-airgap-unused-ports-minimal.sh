#!/bin/sh
# Build MicroPython's `minimal` port freestanding for the Nexus 5.
#
# Why the minimal port: it is the only upstream port with no OS abstraction of
# its own -- it expects the embedder to supply syscalls. That matches every
# other binary in this project and avoids libc entirely, which sidesteps the
# real toolchain question: our GCC 15.2 ships glibc 2.42, and this kernel is
# 3.4.113 from 2013. Modern glibc reaches for clone3, rseq, statx and
# getrandom, all of which postdate this kernel and rely on fallbacks.
#
# FREESTANDING IS NOT OPTIONAL HERE, IT IS CORRECT HERE:
#   AT_HWCAP_FP is clear on this device (measured), so any VFP instruction is
#   a SIGILL at best. -mfloat-abi=soft with zero VFP verified after the build.
set -e
# The system toolchain is already GCC 15.2.0 -- the same major version as the
# Arm GNU 15.2.rel1 release. Those releases only ship arm-none-linux-gnueabi
# for 32-bit Linux, which is HARD-float; this device reports AT_HWCAP_FP clear,
# so a hard-float build would SIGILL. -mfloat-abi=soft below is mandatory, not
# a preference, and the host toolchain already does it correctly.
CROSS_DIR=/usr
CC="$CROSS_DIR/bin/arm-linux-gnueabi-gcc"
SIZE="$CROSS_DIR/bin/arm-linux-gnueabi-size"
OBJCOPY="$CROSS_DIR/bin/arm-linux-gnueabi-objcopy"

CFLAGS="-O2 -std=gnu11 -mfloat-abi=soft -marm -march=armv7-a -mtune=cortex-a9 \
-ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables \
-DMICROPY_ENABLE_FINALISER=0"

# The port's Makefile hardcodes Cortex-M4 flags (-mthumb -mcpu=cortex-m4) when
# CROSS=1, because the minimal port targets a Cortex-M4 board. Override them:
#   MCU = blank  -> do not add the Cortex-M block at all
#   COPT         -> our flags, appended after $(CFLAGS) so they win
make -C . clean >/dev/null 2>&1 || true
make -C . CROSS=1 CC="$CC" MCU= COPT="$CFLAGS -Os" PYTHONOPT="$PYTHONOPT" || exit 1
echo "BUILD_RC=$?"
