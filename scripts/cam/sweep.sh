#!/bin/bash
# Boot the ROM's TWRP with our kernel once, then try several
# configurations of the imaging server in the same boot.
#
# Usage: sweep.sh "vc dt cid csid" ...
#        (each argument set is passed to the imaging server as the
#         virtual channel, data type, cid, csid and frame count)
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

timeout 120 adb push standalone/cam/imaging12 /data/local/tmp/imaging9 >/dev/null 2>&1
timeout 60 adb shell 'chmod 755 /data/local/tmp/imaging9' >/dev/null 2>&1

for cfg in "$@"; do
	set -- $cfg
	vc=$1; dt=$2; cid=$3; csid=$4
	echo
	echo "=== vc=$vc dt=$dt cid=$cid csid=$csid ==="
	timeout 200 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 $vc $dt $cid $csid 3" 2>&1 \
		| tr -d '\r' | grep -E 'virtual channel|chromatrix|buffer queue|CFG:|no frame|GOT A FRAME|frame.nv12|violation|error|Error' | sed 's/^/  /'
done
