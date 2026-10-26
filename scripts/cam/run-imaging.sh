#!/bin/bash
# Boot the ROM's TWRP with our kernel, run the imaging server, and
# capture both its output and the kernel's own log before the device
# reboots (the failure path reboots it).
#
# Usage: run-imaging.sh [args-to-imaging9...]
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
OUT=/tmp/opencode/imaging-out.txt

if [ $# -eq 0 ]; then
	set -- 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 1 0x12 1 0 3
fi

cd $R
rm -rf $W/twrp
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot-ourkern-clean-twrp.img $W/twrp >/dev/null
cp /tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb $W/twrp/kernel
python3 standalone/rootfs/bootimg.py pack $W/twrp $W/twrp/ramdisk \
	standalone/rootfs/stock/boot-ourkern-cfg-twrp.img >/dev/null

timeout 60 adb reboot bootloader >/dev/null 2>&1 || true
for i in $(seq 1 60); do
	timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break
	sleep 1
done
timeout 300 fastboot boot standalone/rootfs/stock/boot-ourkern-cfg-twrp.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 90); do
	timeout 6 adb get-state 2>/dev/null | grep -q device && break
	sleep 2
done
timeout 30 adb shell 'uname -a' 2>&1 | tr -d '\r' | sed 's/^/  k: /'

timeout 120 adb push standalone/cam/imaging9 /data/local/tmp/imaging9 >/dev/null 2>&1
timeout 60 adb shell 'chmod 755 /data/local/tmp/imaging9' >/dev/null 2>&1

# the kernel log first, so it is there even if the run reboots the device
timeout 60 adb shell 'dmesg' > /tmp/opencode/imaging-dmesg.txt 2>&1 || true

echo "=== the run ==="
timeout 240 adb shell "cd /data/local/tmp && ./imaging9 $*" > $OUT 2>&1 || true
tr -d '\r' < $OUT | sed 's/^/  /'

# and the kernel log after
echo
echo "=== the kernel ==="
timeout 60 adb shell 'dmesg' > /tmp/opencode/imaging-dmesg2.txt 2>&1 || true
if [ -s /tmp/opencode/imaging-dmesg2.txt ]; then
	diff /tmp/opencode/imaging-dmesg.txt /tmp/opencode/imaging-dmesg2.txt 2>/dev/null | grep '^>' | tr -d '\r' | sed 's/^/  k: /'
else
	tr -d '\r' < /tmp/opencode/imaging-dmesg.txt | grep -E "cpp|CPP|chromatrix|buffer queue|msm_cpp|DEBUG" | tail -40 | sed 's/^/  k: /'
fi
