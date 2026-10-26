#!/bin/bash
# Build the imaging server for our own kernel and run it on the device.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== build (userspace ARM cross compiler, static) ==="
arm-linux-gnueabi-gcc -O2 -static -Wall -o /tmp/opencode/imaging5 standalone/cam/imaging5.c 2>&1 | head -20
ls -la /tmp/opencode/imaging5

echo
echo "=== push ==="
timeout 60 adb push /tmp/opencode/imaging5 /data/local/tmp/imaging5 2>&1 | tail -1
timeout 30 adb shell 'chmod 755 /data/local/tmp/imaging5' >/dev/null 2>&1

echo
echo "=== run ==="
timeout 60 adb shell '/data/local/tmp/imaging5 15 /dev/video1' 2>&1 | tr -d '\r'
