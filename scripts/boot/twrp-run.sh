#!/bin/bash
# Boot the ROM's TWRP with our kernel and run a command in it.
#   twrp-run.sh "the command to run"
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp

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

echo
echo "=== the command ==="
timeout 300 adb shell "$1" 2>&1 | tr -d '\r' | sed 's/^/  /'
