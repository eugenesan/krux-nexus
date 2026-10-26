#!/bin/bash
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

echo "=== pack ==="
cp /tmp/opencode/ko-dbg/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-dbg-android.img | sed 's/^/  /'

echo
echo "=== boot ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-dbg-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'

echo
echo "=== wait for the camera HAL to open the camera ==="
sleep 25
timeout 60 adb shell 'ls -la /data/local/tmp/isp_cfg_*.bin 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 60 adb shell 'dumpsys media.camera | head -8' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== pull the captures ==="
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 30 adb shell 'ls /data/local/tmp/isp_cfg_*.bin 2>/dev/null' | tr -d '\r'); do
  b=$(basename $f)
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" 2>&1 | tail -1 | sed 's/^/  /'
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | sed 's/^/  /'
