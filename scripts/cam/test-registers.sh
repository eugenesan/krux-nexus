#!/usr/bin/env bash
# Test register writes via camd socket protocol.
# Usage: bash scripts/cam/test-registers.sh
set -e
cd "$(dirname "$0")/../.."

echo "=== Starting camd (if not running) ==="
adb shell 'pkill camd' 2>/dev/null || true
sleep 1
adb shell 'cd /data/krux && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12

echo "=== Test: write exposure register ==="
adb shell "cd /data/krux && ./micropython -c '
import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
time.sleep(1)
# Read baseline
img = sensor.snapshot()
s = img.get_statistics()
print(\"baseline Y mean:\", s.l_mean())
# Increase exposure
sensor.set_exposure(2000)
time.sleep(1)
img = sensor.snapshot()
s = img.get_statistics()
print(\"after exposure=2000 Y mean:\", s.l_mean())
# Increase gain
sensor.set_gain(0x0180)
time.sleep(1)
img = sensor.snapshot()
s = img.get_statistics()
print(\"after gain=0x0180 Y mean:\", s.l_mean())
sensor.run(0)
print(\"done\")
'"

echo "=== camd log tail ==="
adb shell 'tail -5 /tmp/camd.log'
EOF