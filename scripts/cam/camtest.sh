#!/bin/bash
# camtest.sh -- build a kernel with a given configuration, boot it with
# the ROM's Android, and see whether the ROM's camera takes a photograph.
#
# This is the A/B test for the camera fault: the same source, the same
# ramdisk, the same Android -- only the configuration differs.
#
#   camtest.sh <config> <label> [boot-image]
#
# The config is a kernel .config (e.g. /tmp/opencode/cfg-20261011.txt).
# The build goes into /tmp/opencode/ko-<label>.
set -u
R=/home/user/Develop/src/android-air-gapped
cd "$R" || exit 1
CFG=${1:?the config}
LABEL=${2:?the label}
BOOTIMG=${3:-/tmp/opencode/boot-cam-$LABEL.img}
O=/tmp/opencode/ko-$LABEL
W=/tmp/opencode/kcmp

echo "=== build $LABEL with $CFG"
mkdir -p "$O"
cp "$CFG" "$O/.config"
PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin:$PATH \
	make -C standalone/vendor/android_kernel_lge_hammerhead ARCH=arm \
	CROSS_COMPILE="ccache arm-none-eabi-" O="$O" olddefconfig \
	>/tmp/opencode/ko-$LABEL.cfg.log 2>&1
PATH=/home/user/Develop/toolchains/arch-16.2.0/usr/bin:$PATH \
	make -C standalone/vendor/android_kernel_lge_hammerhead ARCH=arm \
	CROSS_COMPILE="ccache arm-none-eabi-" O="$O" -j"$(nproc)" \
	>/tmp/opencode/ko-$LABEL.build.log 2>&1
if [ ! -f "$O/arch/arm/boot/zImage-dtb" ]; then
	echo "!! the build failed:"; tail -5 /tmp/opencode/ko-$LABEL.build.log
	exit 1
fi
ls -la "$O/arch/arm/boot/zImage-dtb" | awk '{print "   the kernel:", $5, "bytes"}'

echo "=== pack it with the ROM's ramdisk"
rm -rf "$W/romtest"; mkdir -p "$W/romtest"
python3 standalone/rootfs/bootimg.py unpack standalone/rootfs/stock/boot.img \
	"$W/romtest" >/dev/null 2>&1
cp "$O/arch/arm/boot/zImage-dtb" "$W/romtest/kernel"
python3 standalone/rootfs/bootimg.py pack "$W/romtest" "$W/romtest/ramdisk" \
	"$BOOTIMG" >/dev/null 2>&1
ls -la "$BOOTIMG" | awk '{print "   the boot image:", $5, "bytes"}'

echo "=== boot it"
timeout 30 adb reboot bootloader >/dev/null 2>&1
for i in $(seq 1 12); do
	sleep 2
	timeout 4 fastboot devices 2>/dev/null | grep -q fastboot && break
done
timeout 120 fastboot boot "$BOOTIMG" 2>&1 | tail -1 | awk '{print "  " $0}'
for i in $(seq 1 40); do
	sleep 3
	s=$(timeout 4 adb get-state 2>&1 | tr -d '\r')
	[ "$s" = "device" ] && break
done
timeout 20 adb shell 'cat /proc/version | head -1 | cut -c1-46' 2>&1 |
	tr -d '\r' | awk '{print "  " $0}'

echo "=== wait for the framework"
for i in $(seq 1 40); do
	sleep 5
	b=$(timeout 8 adb shell 'getprop sys.boot_completed' 2>/dev/null | tr -d '\r')
	[ "$b" = "1" ] && break
done

echo "=== take a photograph"
# The reliable way: the hardware camera key (KEYCODE_CAMERA, 27), which
# the camera app handles itself.  A tap on the shutter button over adb
# often does nothing, because the app has not finished focusing -- and
# that made a whole series of tests meaningless.
timeout 30 adb shell 'ls /sdcard/DCIM/Camera/ 2>/dev/null | wc -l' 2>/dev/null | tr -d '\r' | awk '{print "  the files before:", $1}'
timeout 60 adb shell 'am start -a android.media.action.STILL_IMAGE_CAMERA' >/dev/null 2>&1
sleep 12
# One shutter press per test.  A second press is a separate capture
# session (a second mode change, a second ISP cycle), which makes a
# measurement ambiguous -- so if this one produces nothing, that is a
# result, not something to retry inside the same run.
timeout 30 adb shell 'input keyevent 27' >/dev/null 2>&1
sleep 15
timeout 30 adb shell 'ls -la /sdcard/DCIM/Camera/ | tail -3' 2>&1 | tr -d '\r' |
	awk '{print "  " $0}'
timeout 30 adb shell 'am force-stop com.google.android.GoogleCamera' >/dev/null 2>&1
