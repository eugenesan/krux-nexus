#!/bin/bash
# Boot our kernel (with the capture patches), open the camera, and pull
# the whole initial configuration plus the first per-frame cycle.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== boot our kernel ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cap-nowd.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [$i] $v"
    echo "$v" | grep -q '(user@nuc7)' && break
  fi
  sleep 2
done

echo "=== open the camera (retry until the HAL really configures) ==="
sleep 60
for try in 1 2 3 4; do
  timeout 90 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
  sleep 20
  cnt=$(timeout 60 adb shell 'ls /data/local/tmp/*_cfg_*.bin 2>/dev/null | wc -l' 2>/dev/null | tr -d '\r')
  echo "  try $try: $cnt capture files"
  [ "${cnt:-0}" -gt 100 ] && break
  timeout 60 adb shell 'am force-stop com.google.android.GoogleCamera' 2>/dev/null || true
  sleep 5
done

echo "=== the files ==="
timeout 60 adb shell 'ls /data/local/tmp/*_cfg_*.bin 2>/dev/null | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  count: /'

echo "=== pull them all (as root, through the ROM's kernel later) ==="
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq
timeout 120 adb shell 'su -c "ls /data/local/tmp/*_cfg_*.bin"' 2>/dev/null | tr -d '\r' > /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq/all.txt
wc -l < /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq/all.txt | sed 's/^/  listed: /'
n=0
for f in $(cat /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq/all.txt); do
  b=$(basename "$f")
  case "$b" in
    cpp_*) ;;                       # the chromatrix: one is enough
    *) idx=$(echo "$b" | sed -E 's/isp_cfg_([0-9]+)_.*/\1/')
       [ "$idx" -ge 70 ] && continue ;;
  esac
  timeout 60 adb shell "su -c 'cat $f'" > "/home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq/$b" 2>/dev/null
  n=$((n+1))
done
echo "  pulled $n files"
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/seq/ | head -4 | sed 's/^/  /'

echo "=== reboot into the ROM's kernel ==="
timeout 60 adb reboot 2>&1 | tr -d '\r' || true
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && { echo "  adb up after ${i}s"; break; }
  sleep 2
done
