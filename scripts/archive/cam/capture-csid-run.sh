#!/bin/bash
# Boot our kernel with the ROM's Android, open the camera, and pull the
# captured CSID configurations.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== pack ==="
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb /tmp/opencode/kcmp/romandroid/kernel
python3 standalone/rootfs/bootimg.py pack /tmp/opencode/kcmp/romandroid /tmp/opencode/kcmp/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cap-nowd.img >/dev/null
echo "  ok"

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
sleep 50
for try in 1 2 3; do
  timeout 90 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'
  sleep 20
  cnt=$(timeout 60 adb shell 'ls /data/local/tmp/csid_cfg_*.bin 2>/dev/null | wc -l' 2>/dev/null | tr -d '\r')
  echo "  try $try: $cnt CSID captures"
  [ "${cnt:-0}" -gt 0 ] && break
done

echo "=== reboot into the ROM's kernel (for root) ==="
timeout 60 adb reboot 2>&1 | tr -d '\r' || true
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && { echo "  adb up after ${i}s"; break; }
  sleep 2
done

echo "=== pull ==="
rm -rf /tmp/opencode/csid; mkdir -p /tmp/opencode/csid
timeout 120 adb shell 'su -c "ls /data/local/tmp/csid_cfg_*.bin"' 2>/dev/null | tr -d '\r' > /tmp/opencode/csid/all.txt
echo "  listed: $(wc -l < /tmp/opencode/csid/all.txt)"
for f in $(cat /tmp/opencode/csid/all.txt); do
  b=$(basename "$f")
  timeout 60 adb shell "su -c 'cat $f'" > "/tmp/opencode/csid/$b" 2>/dev/null
done
ls -la /tmp/opencode/csid/ | sed 's/^/  /'
