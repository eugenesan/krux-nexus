#!/bin/bash
# Sweep the MIPI data type: the CSID's LUT only maps a packet whose
# data type matches, and we have been guessing 0x15.
#
# One boot, many runs: each run is a few seconds.
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

echo "=== pack and boot (TWRP ramdisk) ==="
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

# the data types to try: the CSID's LUT accepts 0x12..0x37
DTS="0x12 0x13 0x14 0x15 0x16 0x18 0x19 0x1a 0x1b 0x1c 0x1d 0x1e 0x1f 0x20 0x21 0x22 0x28 0x29 0x2a 0x2b 0x2c 0x2d 0x2e 0x2f 0x30 0x31 0x32 0x36 0x37"

for dt in $DTS; do
  timeout 120 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 0 $dt 0 0 1 2>&1 | grep -E 'GOT A FRAME' | head -1" 2>/dev/null | tr -d '\r'
  frames=$(timeout 60 adb shell 'dmesg | grep -c "GOT A FRAME"' 2>/dev/null | tr -d '\r')
  viol=$(timeout 60 adb shell 'dmesg | grep -c "camif violation status"' 2>/dev/null | tr -d '\r')
  ovf=$(timeout 60 adb shell 'dmesg | grep -c "pix0 overflow"' 2>/dev/null | tr -d '\r')
  bad=$(timeout 60 adb shell 'dmesg | grep -c "unsupported data type"' 2>/dev/null | tr -d '\r')
  csid=$(timeout 60 adb shell 'dmesg | grep "DEBUG: CSID" | tail -1' 2>/dev/null | tr -d '\r' | sed 's/.*irq status/irq/')
  echo "  dt $dt: frames=$frames violations=$viol overflow=$ovf $csid"
  if [ "${frames:-0}" -gt 0 ]; then
    echo "  >>> dt $dt produced frames"
    break
  fi
done
