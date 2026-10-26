#!/bin/bash
# The definitive run with the measured data type, and a dump of what the
# ISP-IF programmed (its own registers) so we can compare with the ISP.
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

echo "=== the run ==="
timeout 180 adb shell 'cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 1 0x12 1 0 3 2>&1 | tail -25' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the kernel ==="
timeout 60 adb shell 'dmesg | grep -E "DEBUG: (CSID|CSIPHY|ISP-IF|ISPIF|camif|VFE camif)|violation"' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
