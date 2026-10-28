#!/bin/bash
# Build, pack, boot our kernel, open the camera, pull the captures.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== build ==="
DEFCONFIG_NAME=lineageos_hammerhead_defconfig TOOLCHAIN=arch \
  bash build/build-kernel.sh --tree standalone/vendor/android_kernel_lge_hammerhead \
  --out /tmp/opencode/ko-cap > /tmp/opencode/kocap.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kocap.log | tail -2 | sed 's/^/  /'

echo "=== pack ==="
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb /tmp/opencode/kcmp/romandroid/kernel
python3 standalone/rootfs/bootimg.py pack /tmp/opencode/kcmp/romandroid /tmp/opencode/kcmp/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cap-nowd.img | sed 's/^/  /'

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
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [$i] $v"
    echo "$v" | grep -q '(user@nuc7)' && break
  fi
  sleep 2
done

echo "=== open the camera ==="
sleep 40
timeout 90 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -2 | sed 's/^/  /'
sleep 25

echo "=== the captures ==="
timeout 60 adb shell 'ls -la /data/local/tmp/*_cfg_*.bin 2>&1 | tail -12' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo "=== pull them ==="
rm -rf /home/user/Develop/src/android-air-gapped/standalone/cam/data; mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 40 adb shell 'ls /data/local/tmp/*_cfg_*.bin 2>/dev/null' | tr -d '\r' | tail -40); do
  b=$(basename "$f")
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" >/dev/null 2>&1 || true
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | tail -15 | sed 's/^/  /'
