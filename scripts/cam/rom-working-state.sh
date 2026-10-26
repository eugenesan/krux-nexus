#!/bin/bash
# rom-working-state.sh -- the camera hardware's state in the case that WORKS.
#
# The one measurement that is still missing: what the CSID, the ISP-IF
# and the CAMIF look like while the ROM's own camera is taking a
# picture under the ROM's kernel, to be compared with the same
# registers under our kernel, where the frame comes out empty.
#
# The ROM's kernel has no debug prints of its own, and the camera's
# driver only prints our own kernel's registers, so the registers have
# to be read from the outside -- which needs a root shell in the ROM's
# Android (the ROM's kernel is booted, the ROM's Android is running).
#
# The addresses come from the device tree:
#   qcom,csid@fda08000  CSID0     qcom,csid@fda08800  CSID2
#   qcom,ispif@fda0a000 ISP-IF    qcom,vfe@fda10000    VFE0
set -u
R=/home/user/Develop/src/android-air-gapped
cd "$R" || exit 1

echo "=== the kernel we are running on ==="
timeout 20 adb shell 'cat /proc/version | head -1 | cut -c1-60' 2>&1 | tr -d '\r'
timeout 20 adb shell 'id' 2>&1 | tr -d '\r' | head -1

echo
echo "=== the register reader ==="
timeout 60 adb push standalone/cam/regs /data/local/tmp/regs 2>&1 | tail -1
timeout 30 adb shell 'chmod 755 /data/local/tmp/regs; ls -la /dev/mem 2>&1 | head -1; mknod /dev/mem c 1 1 2>&1; ls -la /dev/mem' 2>&1 | tr -d '\r'

echo
echo "=== start the camera and take a picture ==="
timeout 30 adb shell 'input keyevent 224' 2>&1
timeout 90 adb shell 'monkey -p com.google.android.GoogleCamera -c android.intent.category.LAUNCHER 1' 2>&1 | tail -1 | tr -d '\r'
sleep 10
timeout 30 adb shell 'input keyevent 115' 2>&1	# the volume key is the shutter
sleep 8

echo
echo "=== the CSID0's registers (while the camera runs) ==="
timeout 30 adb shell '/data/local/tmp/regs 0xfda08000' 2>&1 | tr -d '\r'
echo
echo "=== the CSID2's registers ==="
timeout 30 adb shell '/data/local/tmp/regs 0xfda08800' 2>&1 | tr -d '\r'

echo
echo "=== the VFE0's camera front end ==="
timeout 30 adb shell '/data/local/tmp/regs 0xfda10000 vfe' 2>&1 | tr -d '\r'

echo
echo "=== the ISP-IF's routing registers ==="
timeout 30 adb shell '/data/local/tmp/regs 0xfda0a000 ispif 0x0 0x8 0x10 0x18 0x20 0x28 0x30 0x38 0x40 0x48 0x50 0x58 0x60 0x68 0x70 0x78 0x80 0x88 0x90 0x98 0xa0 0xa8 0xb0 0xb8 0xc0 0xc8 0xd0 0xd8 0xe0 0xe8 0xf0 0xf8 0x100 0x108 0x110 0x118 0x120 0x128 0x130 0x138 0x140 0x148 0x150 0x158 0x160 0x168 0x170 0x178 0x180 0x188 0x190 0x198 0x1a0 0x1a8 0x1b0 0x1b8 0x1c0 0x1c8 0x1d0 0x1d8 0x1e0 0x1e8 0x1f0 0x1f8 0x200 0x208 0x210 0x218 0x220 0x228 0x230 0x238 0x240 0x248 0x250 0x258 0x260 0x268 0x270 0x278 0x280 0x288 0x290 0x298 0x2a0 0x2a8 0x2b0 0x2b8 0x2c0 0x2c8 0x2d0 0x2d8 0x2e0 0x2e8 0x2f0 0x2f8 0x300 0x308 0x310 0x318' 2>&1 | tr -d '\r'

echo
echo "=== the photograph ==="
timeout 30 adb shell 'ls -la /sdcard/DCIM/Camera/ | tail -3' 2>&1 | tr -d '\r'

echo
echo "=== stop the camera ==="
timeout 30 adb shell 'am force-stop com.google.android.GoogleCamera' 2>&1
