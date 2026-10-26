#!/bin/bash
# Build the imaging server for the device (arm32, static) and report what
# it needs at run time.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R/standalone/cam

CC=${CC:-arm-linux-gnueabi-gcc}
[ ! -d /usr/arm-linux-gnueabi ] || SYSROOT=/usr/arm-linux-gnueabi
SYSROOT=${SYSROOT:-/tmp/opencode/armel-sysroot}

$CC -O2 -static -march=armv7-a -mfpu=neon-vfpv3 -mfloat-abi=hard \
    -Wall -o imaging9 imaging8.c -lpthread 2>&1 | grep -v '^$' | head -20 || true

ls -la imaging9 | sed 's/^/  /'
file imaging9 | sed 's/^/  /'
