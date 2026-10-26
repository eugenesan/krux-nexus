#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
timeout 30 adb shell 'dd if=/dev/video1 of=/dev/null bs=4096 count=1 2>&1 | head -2' >/dev/null 2>&1
echo "=== ALL kernel messages from one open attempt (in order) ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit\(|selinux"' 2>&1 | tr -d '\r'
