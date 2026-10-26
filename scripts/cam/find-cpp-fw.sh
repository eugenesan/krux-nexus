#!/bin/bash
# Find the CPP firmware file in the ROM and the paths the kernel's
# firmware loader searches.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== reboot into the ROM ==="
timeout 30 adb reboot 2>&1 | tr -d '\r'
timeout 180 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 30 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'

echo
echo "=== candidate CPP firmware files ==="
timeout 120 adb shell 'su -c "find /vendor /system /odm /data/vendor -iname \"*cpp*\" -o -iname \"*.bin\" 2>/dev/null | head -20"' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the firmware loader's search path (device tree) ==="
timeout 60 adb shell 'su -c "find /proc/device-tree -name \"*firmware*\" 2>/dev/null | head -10"' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 60 adb shell 'su -c "cat /proc/device-tree/firmware-path 2>/dev/null; echo; cat /proc/cmdline | tr \" \" \"\\n\" | grep -i firmware"' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== /lib/firmware and friends ==="
timeout 60 adb shell 'su -c "ls /lib/firmware /vendor/firmware 2>/dev/null | head -20"' 2>&1 | tr -d '\r' | sed 's/^/  /'
