#!/bin/bash
# Boot the ROM's Android with our kernel, get the camera to open, and
# pull the captured ISP input configuration (the vendor's CAMIF geometry
# and pixel clock).
set +e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

echo "=== pack with the ROM's Android ramdisk ==="
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/romandroid/kernel
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cap-nowd.img >/dev/null
echo "  ok"

echo "=== boot ==="
timeout 30 adb reboot bootloader >/dev/null 2>&1
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cap-nowd.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && break
  sleep 2
done
timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1 | sed 's/^/  /'

echo "=== open the camera until the HAL configures the ISP ==="
# the stale captures from an earlier run would satisfy the count test
timeout 60 adb shell 'rm -f /data/local/tmp/isp_input_*.bin' >/dev/null 2>&1 || true
sleep 40
for try in $(seq 1 10); do
  timeout 90 adb shell "am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher" >/dev/null 2>&1
  sleep 18
  n=$(timeout 60 adb shell 'ls /data/local/tmp/isp_input_*.bin 2>/dev/null | wc -l' 2>/dev/null | tr -d '\r')
  echo "  try $try: $n ISP input captures"
  [ "${n:-0}" -gt 0 ] && break
  # NB: do NOT force-stop the camera app -- that kills the very
  # session we are trying to observe.  Just relaunch it.
  sleep 6
done
timeout 60 adb shell 'ls -la /data/local/tmp/isp_input_*.bin 2>/dev/null | head -4' 2>&1 | tr -d '\r' | sed 's/^/  /'
timeout 60 adb shell 'dmesg | grep -E "DEBUG: ISP input|DEBUG:   camif|DEBUG: ISP-IF routing"' 2>&1 | tr -d '\r' | tail -8 | sed 's/^/  k: /'

echo "=== reboot into the ROM's kernel and pull ==="
timeout 60 adb reboot >/dev/null 2>&1
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && break
  sleep 2
done
rm -rf /tmp/opencode/ispcfg; mkdir -p /tmp/opencode/ispcfg
timeout 120 adb shell 'su -c "ls /data/local/tmp/isp_input_*.bin"' 2>/dev/null | tr -d '\r' > /tmp/opencode/ispcfg/all.txt
echo "  listed: $(wc -l < /tmp/opencode/ispcfg/all.txt)"
i=0
for f in $(cat /tmp/opencode/ispcfg/all.txt); do
  b=$(basename "$f")
  timeout 60 adb shell "su -c 'cat $f'" > "/tmp/opencode/ispcfg/$b" 2>/dev/null
  i=$((i+1)); [ $i -ge 6 ] && break
done
ls -la /tmp/opencode/ispcfg/ | sed 's/^/  /'
