#!/bin/bash
# Run the imaging server, then show what the kernel said.
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 90 adb shell '/data/local/tmp/imaging7 640 480' 2>&1 | tr -d '\r' | head -25
echo
echo "=== kernel messages ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux" | tail -35' 2>&1 | tr -d '\r' | sed 's/^/  /'
