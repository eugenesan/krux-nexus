#!/bin/bash
# Boot our kernel with the ROM's Android and get a *complete* camera
# session (14 sensor arrays), so that the ISP-IF is configured and its
# routing printed.
set +e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== boot ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cap-nowd.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1 | sed 's/^/  /'
    break
  fi
  sleep 2
done

echo "=== open the camera until the session completes ==="
sleep 40
for try in $(seq 1 8); do
  timeout 90 adb shell "am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher" >/dev/null 2>&1 || true
  sleep 18
  n=$(timeout 60 adb shell 'ls /data/local/tmp/sensor_cfg_*.bin 2>/dev/null | wc -l' 2>/dev/null | tr -d '\r')
  echo "  try $try: $n sensor arrays"
  [ "${n:-0}" -ge 14 ] && break
  timeout 60 adb shell 'am force-stop com.google.android.GoogleCamera' >/dev/null 2>&1 || true
  sleep 4
done

echo "=== the ISP-IF's routing, from the kernel log ==="
timeout 60 adb shell 'dmesg | grep -E "DEBUG: ISP-IF"' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
