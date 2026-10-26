#!/bin/bash
# Build (air-gap profile, the ROM's compiler), pack with the ROM's Android
# ramdisk, boot it, open the camera, and collect what the HAL uploaded.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead
W=/tmp/opencode/kcmp
cd $T

echo "=== build ==="
DEFCONFIG_NAME=lineageos_hammerhead_defconfig TOOLCHAIN=arch \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-cap \
  > /tmp/opencode/kocap.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/kocap.log | tail -3 | sed 's/^/  /'

echo
echo "=== pack (watchdog off, permissive selinux) ==="
mkdir -p $W/romandroid
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/romandroid/kernel
cp /tmp/opencode/romcheck/ramdisk $W/romandroid/ramdisk
python3 - "$W/romandroid/meta.txt" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
s = s.replace('msm_watchdog_v2.enable=1', 'msm_watchdog_v2.enable=0')
if 'androidboot.selinux=permissive' not in s:
    s = s.replace('loop.max_part=7', 'androidboot.selinux=permissive loop.max_part=7')
open(p,'w').write(s)
PY
cd $R
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cap-nowd.img | sed 's/^/  /'

echo
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
    echo "  [${i}] $v"
    echo "$v" | grep -q 'g1a2ab21fc89\|g a63ab7a62c1' && break
    echo "$v" | grep -q 'g[a-z0-9]\{10\}' && break
  fi
  sleep 2
done

echo
echo "=== wait for the camera daemon, then open the camera ==="
sleep 25
timeout 90 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -2 | sed 's/^/  /'
sleep 25

echo
echo "=== the captures ==="
timeout 60 adb shell 'ls -la /data/local/tmp/*_cfg_*.bin 2>/dev/null' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the camera's state ==="
timeout 40 adb shell 'dumpsys media.camera | grep -A3 "Active Camera Clients"' 2>&1 | tr -d '\r' | head -4 | sed 's/^/  /'
timeout 60 adb shell 'logcat -d | grep -iE "CameraDevice 0 error|CAM_FatalError|CAM_ERR|camera error"' 2>&1 | tr -d '\r' | tail -8 | sed 's/^/  l: /'

echo
echo "=== the kernel's DEBUG lines ==="
timeout 60 adb shell 'dmesg | grep -i "DEBUG: captured"' 2>&1 | tr -d '\r' | tail -12 | sed 's/^/  k: /'

echo
echo "=== pull the captures ==="
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 40 adb shell 'ls /data/local/tmp/*_cfg_*.bin 2>/dev/null' | tr -d '\r'); do
  b=$(basename "$f")
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" 2>&1 | tail -1 | sed 's/^/  /'
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | sed 's/^/  /'
