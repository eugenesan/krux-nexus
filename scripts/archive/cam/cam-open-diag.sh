#!/bin/bash
# Clear the log, try to open the sensor node, and show what the kernel said.
cd /home/user/Develop/src/android-air-gapped || exit 1

timeout 30 adb shell 'dmesg -c >/dev/null 2>&1; echo cleared' >/dev/null 2>&1
timeout 30 adb shell 'cat /data/local/tmp/camprobe >/dev/null 2>&1; /data/local/tmp/camprobe 2>&1 | head -12' >/dev/null 2>&1
echo "=== kernel messages produced by the open attempts ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit|selinux" | tail -40' 2>&1 | tr -d '\r'
echo
echo "=== try opening the nodes directly ==="
timeout 60 adb shell 'for n in /dev/video0 /dev/video1 /dev/video2; do printf "%s: " $n; dd if=$n of=/dev/null bs=4096 count=1 2>&1 | head -1; done' 2>&1 | tr -d '\r'
echo
echo "=== after that, new kernel messages ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit|selinux" | tail -25' 2>&1 | tr -d '\r'
