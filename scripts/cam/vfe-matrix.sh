#!/bin/bash
# Try the VFE combinations: which of the two VFE subdevices is the one
# the rear camera's CSID feeds?
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

rm -rf $W/twrp
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot-ourkern-clean-twrp.img $W/twrp >/dev/null
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/twrp/kernel
python3 standalone/rootfs/bootimg.py pack $W/twrp $W/twrp/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cfg-twrp.img >/dev/null
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cfg-twrp.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && break
  sleep 2
done
timeout 120 adb push standalone/cam/imaging9 /data/local/tmp/imaging9 >/dev/null 2>&1
timeout 60 adb shell 'chmod 755 /data/local/tmp/imaging9' >/dev/null 2>&1
echo "  booted"

for vfe in 0 1; do
  for src in 0 1; do
    echo "=== VFE $vfe, source $src ==="
    timeout 120 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 1 0x12 1 0 2 $vfe $src 2>&1 | grep -E 'ISP subdev|GOT A FRAME|no frame after|ispif\]|stream source'" 2>&1 | tr -d '\r' | sed 's/^/  /'
    fr=$(timeout 60 adb shell 'dmesg | grep -c "GOT A FRAME"' 2>/dev/null | tr -d '\r')
    ov=$(timeout 60 adb shell 'dmesg | grep -c "pix0 overflow"' 2>/dev/null | tr -d '\r')
    echo "  -> frames=$fr overflow=$ov"
    [ "${fr:-0}" -gt 0 ] && { echo "  >>> FRAMES with VFE $vfe source $src"; exit 0; }
  done
done
