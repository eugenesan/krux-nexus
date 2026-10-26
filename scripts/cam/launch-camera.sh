#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the camera apps and their launcher activities ==="
for p in com.google.android.GoogleCamera com.google.android.apps.googlecamera.fishfood com.shenyaocn.android.usbcamera; do
  a=$(timeout 40 adb shell "cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER $p 2>/dev/null | tail -1 | tr -d '\r'" 2>/dev/null)
  echo "  $p -> $a"
done
echo
echo "=== start one ==="
for act in \
  "com.google.android.GoogleCamera/com.google.android.apps.googlecamera.fishfood.MainActivity" \
  "com.google.android.GoogleCamera/com.google.android.apps.googlecamera.MainActivity" \
  "com.shenyaocn.android.usbcamera/com.shenyaocn.android.usbcamera.MainActivity" \
  "com.shenyaocn.android.usbcamera/.MainActivity" ; do
  echo "--- $act"
  timeout 40 adb shell "am start -n $act" 2>&1 | tr -d '\r' | head -2 | sed 's/^/    /'
  sleep 6
  n=$(timeout 30 adb shell 'ls /data/local/tmp/isp_cfg_*.bin 2>/dev/null | wc -l' | tr -d '\r')
  echo "    capture files: $n"
  [ "$n" != "0" ] && break
done
echo
echo "=== the captures ==="
timeout 30 adb shell 'ls -la /data/local/tmp/isp_cfg_*.bin 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'
