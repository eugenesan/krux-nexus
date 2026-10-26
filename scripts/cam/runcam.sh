#!/bin/sh
#
# runcam.sh -- boot our own firmware and run the imaging server on it.
#
# The device is our own firmware (the stub ramdisk and our kernel), where
# adbd is root and dmesg can be read.  The ROM's Android is not used: the
# camera there belongs to the ROM's HAL, and the ROM's HAL crashes under
# our kernel (a null pointer in getCamInfo).
#
# Usage:
#   runcam.sh <program> [live] [crop] [src] [capture]
#
# The arguments are the imaging server's settings (see
# standalone/scripts/cam/imaging.sh):
#
#   live     1 = program the sensor from the vendor's live register list
#   crop     1 = enable the ISP-IF's crop (the vendor sends it disabled)
#   src      2 = CAMIF_RAW, 1 = PIX_VIEWFINDER
#   capture  1 = send the vendor's capture configuration as well
#
# The kernel's log is read *while* the server is running: the camera's
# close path panics (the ISP's close hangs) and the device restarts, so
# the log has to be read before that.
#
set -e

PROG=${1:-/data/local/tmp/imaging15}
LIVE=${2:-1}
CROP=${3:-1}
SRC=${4:-2}
CAP=${5:-1}

echo "=== booting our own firmware"
# The fastboot boot silently falls back to the boot partition now and
# then -- and then the device is the ROM's Android, where dmesg cannot
# be read.  So the kernel is checked, and the boot is repeated until it
# is ours.
try=0
while [ $try -lt 4 ]; do
	try=$((try + 1))
	adb reboot bootloader >/dev/null 2>&1 || true
	sleep 10
	i=0
	while [ $i -lt 25 ]; do
		if fastboot devices 2>/dev/null | grep -q fastboot; then break; fi
		i=$((i + 1))
		sleep 2
	done
	fastboot boot /tmp/opencode/boot-stub-camera.img 2>&1 | grep Booting || true

	i=0
	while [ $i -lt 50 ]; do
		sleep 3
		# the state is "recovery" on our own firmware, not "device"
		case "$(adb get-state 2>/dev/null | tr -d '\r')" in
		device | recovery) break ;;
		esac
		i=$((i + 1))
	done

	KVER=$(adb shell 'cat /proc/version | head -1 | tr -d "\r"' 2>/dev/null)
	UID=$(adb shell id -u 2>/dev/null | tr -d '\r')
	case "$KVER" in
	*gfe3a12a2289*) ;;
	*)
		echo "  attempt $try: not our kernel yet ($KVER)"
		continue
		;;
	esac
	if [ "$UID" != "0" ]; then
		echo "  attempt $try: not root yet (uid $UID)"
		continue
	fi
	break
done
case "$KVER" in
*gfe3a12a2289*) ;;
*)
	echo "!!! could not boot our own firmware ($KVER, uid $UID) -- aborting"
	exit 1
	;;
esac
if [ "$UID" != "0" ]; then
	echo "!!! not root (uid $UID) -- aborting"
	exit 1
fi

# the program is given as a path in the project (standalone/cam/...) or
# as a path on the device
if [ -f "$PROG" ]; then
	adb push "$PROG" /data/local/tmp/imaging >/dev/null
else
	adb shell "cp $PROG /data/local/tmp/imaging" >/dev/null 2>&1
fi
if ! adb shell 'test -x /data/local/tmp/imaging' 2>/dev/null; then
	echo "!!! the program $PROG is not there"
	exit 1
fi
adb push standalone/scripts/cam/imaging.sh /data/local/tmp/imaging.sh >/dev/null 2>&1
adb shell "chmod 755 /data/local/tmp/imaging /data/local/tmp/imaging.sh" >/dev/null 2>&1

echo "=== running: live=$LIVE crop=$CROP src=$SRC capture=$CAP"
adb shell "cd /data/local/tmp && (./imaging.sh ./imaging $LIVE $CROP $SRC $CAP > run.log 2>&1 &)" >/dev/null 2>&1

# the frame comes after a few seconds; read the kernel's log before the
# camera is closed (the close panics and the device restarts)
sleep 4
echo "=== the kernel's log (first):"
adb shell 'dmesg | grep -aE "CSID0 IRQ|CSID0 LUT|pix_count|camif error|violation"' 2>/dev/null | tr -d '\r' | sed 's/^/  /'
sleep 4
echo "=== the kernel's log:"
adb shell 'dmesg | grep -aE "CSID0 IRQ|CSID0 LUT|pix_count|camif error|violation"' 2>/dev/null | tr -d '\r' | sed 's/^/  /'
echo "=== the server:"
adb shell 'cat /data/local/tmp/run.log' 2>/dev/null | tr -d '\r' | grep -aE "GOT A FRAME|frame rate|crop:|stream source|capture.s conf|initial config|per-frame" | sed 's/^/  /'
