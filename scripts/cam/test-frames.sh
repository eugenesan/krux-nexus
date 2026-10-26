#!/bin/bash
# The real test: our own kernel, the pristine TWRP ramdisk, and our own
# imaging server -- no vendor code at run time.
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

KERNEL=${1:-/tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb}
OUT=${2:-$R/standalone/rootfs/stock/boot-ourkern-cfg-twrp.img}

echo "=== unpack the TWRP image to get its ramdisk and cmdline ==="
rm -rf $W/twrp
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot-ourkern-clean-twrp.img $W/twrp | sed 's/^/  /'
grep cmdline $W/twrp/meta.txt | sed 's/^/  /'

echo "=== pack with our kernel ==="
cp "$KERNEL" $W/twrp/kernel
python3 standalone/rootfs/bootimg.py pack $W/twrp $W/twrp/ramdisk "$OUT" | sed 's/^/  /'

echo "=== boot ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot "$OUT" 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [$i] $v"
    echo "$v" | grep -q '(user@nuc7)' && break
  fi
  sleep 2
done

echo "=== push the imaging server ==="
timeout 120 adb push standalone/cam/imaging9 /data/local/tmp/imaging9 2>&1 | tail -1 | sed 's/^/  /'
timeout 60 adb shell 'chmod 755 /data/local/tmp/imaging9' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo "=== run it (binned mode, the mode Krux wants) ==="
timeout 180 adb shell 'cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 2>&1 | tail -70' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo "=== the kernel log ==="
timeout 60 adb shell 'dmesg | tail -30' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
