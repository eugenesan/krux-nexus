#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== msm / camera / sensor messages ==="
timeout 60 adb shell 'dmesg | grep -iE "msm_cam|msmb|imx179|mt9m|camera|csiphy|csiph|isp|msm_isp|actuator|oi" | head -60' 2>&1 | tr -d '\r'
echo
echo "=== video node identities (from /sys) ==="
timeout 60 adb shell 'for v in video0 video1 video2; do echo "--- $v"; cat /sys/class/video4linux/$v/name 2>/dev/null; cat /sys/class/video4linux/$v/dev 2>/dev/null; done' 2>&1 | tr -d '\r'
echo
echo "=== subdevs ==="
timeout 60 adb shell 'for s in /sys/class/video4linux/v4l-subdev*; do printf "%s " $s; cat $s/name 2>/dev/null; done' 2>&1 | tr -d '\r' | head -30
