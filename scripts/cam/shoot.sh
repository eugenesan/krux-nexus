#!/bin/bash
# shoot.sh -- take a photograph with the ROM's camera app and report
# whether a file appeared.
#
# The procedure has to be validated against a kernel that is known to
# work (the ROM's own 20261011 image), otherwise "no photograph" only
# says that the shutter was not hit.
#
#   shoot.sh [label]
set -u
LABEL=${1:-run}

echo "=== the kernel we are on"
timeout 20 adb shell 'cat /proc/version | head -1 | cut -c1-60' 2>&1 |
	tr -d '\r' | awk '{print "  " $0}'

echo "=== the files before"
timeout 30 adb shell 'ls /sdcard/DCIM/Camera/ 2>/dev/null' 2>&1 | tr -d '\r' |
	awk '{print "  " $0}' | tail -3

echo "=== open the camera"
timeout 30 adb shell 'input keyevent 224' >/dev/null 2>&1
sleep 3
timeout 90 adb shell 'monkey -p com.google.android.GoogleCamera -c android.intent.category.LAUNCHER 1' \
	>/dev/null 2>&1
sleep 20

echo "=== a screenshot, to see where the shutter is"
timeout 60 adb shell 'screencap -p /sdcard/shoot.png' >/dev/null 2>&1
timeout 60 adb pull /sdcard/shoot.png /tmp/opencode/shoot-$LABEL.png >/dev/null 2>&1
ls -la /tmp/opencode/shoot-$LABEL.png 2>/dev/null | awk '{print "  ", $5, "bytes"}'

echo "=== fire the shutter: the volume key, then the on-screen button"
timeout 30 adb shell 'input keyevent 115' >/dev/null 2>&1
sleep 12
timeout 30 adb shell 'input tap 195 537' >/dev/null 2>&1
sleep 12
timeout 30 adb shell 'input keyevent 115' >/dev/null 2>&1
sleep 15

echo "=== the files after"
timeout 30 adb shell 'ls -la /sdcard/DCIM/Camera/ 2>/dev/null | tail -4' 2>&1 |
	tr -d '\r' | awk '{print "  " $0}'
timeout 30 adb shell 'am force-stop com.google.android.GoogleCamera' >/dev/null 2>&1
