#!/usr/bin/env bash
# Test VFE overflow: boot kernel, run imaging, capture dmesg.
# Usage: bash scripts/cam/test-vfe.sh [boot_img_path]
set -e
BOOTIMG="${1:-/tmp/opencode/boot-vfe.img}"
cd "$(dirname "$0")/../.."

echo "=== Booting kernel ==="
adb reboot bootloader
sleep 5
fastboot boot "$BOOTIMG"
sleep 15

echo "=== Verifying device ==="
adb shell 'cat /proc/version'
adb shell 'ls /dev/video* /dev/v4l-subdev* 2>/dev/null'

echo "=== Running PIX_VIEWFINDER imaging (8s) ==="
adb shell 'cd /data/local/tmp && timeout 8 ./imaging-cam 1640 1232 0x40000000 rear 0 - 1 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0' 2>&1 | tail -20 || true

echo "=== Capturing dmesg ==="
adb shell 'dmesg | grep -iE "VFE40_DBG|ISPIF_DBG|pix0 overflow|hw_version|halt timeout|NULL stream" | tail -40'

echo "=== Counting overflow ==="
OVF=$(adb shell 'dmesg | grep -c "pix0 overflow"' 2>/dev/null | tr -d '\r')
echo "pix0 overflow count: $OVF"
EOF
chmod +x scripts/cam/test-vfe.sh
echo "created"