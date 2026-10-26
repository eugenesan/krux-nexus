#!/bin/bash
# Boot the ROM's Android with our kernel and open the camera with the
# ROM's own camera app, so that the kernel logs the sequence of ioctls
# the *vendor's* camera2 HAL makes on the ISP and the CPP.
#
# That sequence is the ground truth for what our imaging server has to
# do: our own sequence so far reaches the CPP (the chromatrix is
# accepted) but no frame ever arrives.
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
KERNEL=${1:-/tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb}
OUT=${2:-$R/standalone/rootfs/stock/boot-ourkern-cap-android.img}

echo "=== pack our kernel with the ROM's Android ramdisk ==="
rm -rf $W/romandroid
mkdir -p $W/romandroid
cp /tmp/opencode/romcheck/meta.txt $W/romandroid/meta.txt
cp "$KERNEL" $W/romandroid/kernel
cp /tmp/opencode/romcheck/ramdisk $W/romandroid/ramdisk
cd $R
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk "$OUT" >/dev/null
echo "  ok"

echo
echo "=== boot it ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 60); do
	timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && break
	sleep 1
done
timeout 300 fastboot boot "$OUT" 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 120); do
	st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
	if [ "$st" = "device" ]; then
		v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
		echo "  [${i}] $v"
		case "$v" in
		*ge9bf776e758*|*'-dirty'*) echo "  >>> our kernel is up"; break;;
		esac
	fi
	sleep 2
done

echo
echo "=== wait for Android to settle ==="
timeout 120 adb shell 'while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 2; done; echo booted' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== clear the log and open the camera with the ROM's app ==="
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1; echo cleared' 2>&1 | tr -d '\r' | sed 's/^/  /'
for act in android.media.action.STILL_IMAGE_CAMERA android.intent.action.STILL_IMAGE_CAMERA; do
	timeout 60 adb shell "am start -a $act" 2>&1 | tr -d '\r' | head -3 | sed 's/^/  /'
done
sleep 20

echo
echo "=== the ISP ioctl sequence the vendor's HAL made ==="
timeout 60 adb shell 'dmesg | grep -E "DEBUG: ISP ioctl|DEBUG: CPP ioctl"' 2>&1 | tr -d '\r' | sed 's/^/  k: /'

echo
echo "=== the rest of the camera's kernel messages ==="
timeout 60 adb shell 'dmesg | grep -viE "avc:|audit|selinux" | tail -40' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
