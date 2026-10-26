#!/bin/bash
# Boot the installed ROM (KernelSU) kernel and check whether the working
# kernel also logs the "unaligned request" warning from dwc3.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== rebooting into the installed kernel (/boot) ==="
timeout 30 adb reboot 2>&1 | sed 's/^/  /'

echo "=== waiting for adb (up to 180s) ==="
timeout 180 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices -l 2>&1 | sed 's/^/  /'

echo "=== kernel version ==="
timeout 30 adb shell cat /proc/version 2>&1 | tr -d '\r' | sed 's/^/  /'

echo "=== does the WORKING kernel log the unaligned warning? ==="
timeout 40 adb shell 'dmesg | grep -c "unaligned request"' 2>&1 | tr -d '\r' | sed 's/^/  count: /'
timeout 40 adb shell 'dmesg | grep "unaligned request" | head -5' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo "=== for reference: ffs/functionfs lines ==="
timeout 40 adb shell 'dmesg | grep -iE "functionfs|ffs:" | head -10' 2>&1 | tr -d '\r' | sed 's/^/  /'
