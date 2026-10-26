#!/bin/bash
# Our own imaging server with the ISP-IF configured: read the kernel's
# view of the routing (printed inside msm_ispif_config), the CSID's
# counters and the CSIPHY's status.
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

echo "=== pack with the TWRP ramdisk ==="
rm -rf $W/twrp
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot-ourkern-clean-twrp.img $W/twrp >/dev/null
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/twrp/kernel
python3 standalone/rootfs/bootimg.py pack $W/twrp $W/twrp/ramdisk \
    standalone/rootfs/stock/boot-ourkern-cfg-twrp.img >/dev/null
echo "  ok"

echo "=== boot ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastfastboot devices 2>/dev/null | grep -q fastboot && break
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cfg-twrp.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1 | sed 's/^/  /'
    break
  fi
  sleep 2
done

echo "=== run the imaging server ==="
timeout 120 adb push standalone/cam/imaging9 /data/local/tmp/imaging9 >/dev/null 2>&1
timeout 60 adb shell 'chmod 755 /data/local/tmp/imaging9' >/dev/null 2>&1
timeout 180 adb shell 'cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 2>&1 | grep -E "ispif|vendor sequence|stream started|no frame|GOT A FRAME" | head -12' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the kernel's view of the routing (msm_ispif_config) ==="
timeout 60 adb shell 'dmesg | grep -E "DEBUG: ISP-IF routing"' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
echo "=== the CSID and the CSIPHY ==="
timeout 60 adb shell 'dmesg | grep -E "DEBUG: CSID|DEBUG: CSIPHY"' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
