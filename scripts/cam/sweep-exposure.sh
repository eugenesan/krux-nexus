#!/usr/bin/env bash
# Sweep exposure values and capture brightness for each.
# Usage: bash scripts/cam/sweep-exposure.sh
set -e
cd "$(dirname "$0")/../.."

echo "=== Starting camd ==="
adb shell 'pkill camd' 2>/dev/null || true
sleep 1
adb shell 'cd /data/krux && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12

echo "=== Exposure sweep (gain fixed at 0x00e0) ==="
adb shell "cd /data/krux && ./micropython -c '
import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.set_gain(0x00e0)
for exp in [100, 200, 400, 600, 800, 1000, 1200]:
    sensor.set_exposure(exp)
    time.sleep(0.5)
    img = sensor.snapshot()
    s = img.get_statistics()
    print(\"exp=%d  Y_mean=%d  Y_stdev=%d\" % (exp, s.l_mean(), s.l_stdev()))
sensor.run(0)
print(\"done\")
'"

echo ""
echo "=== Gain sweep (exposure fixed at 400) ==="
adb shell "cd /data/krux && ./micropython -c '
import sensor, time
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.set_exposure(400)
for gain in [0x0020, 0x0040, 0x0080, 0x00e0, 0x0100, 0x0180, 0x01ff]:
    sensor.set_gain(gain)
    time.sleep(0.5)
    img = sensor.snapshot()
    s = img.get_statistics()
    print(\"gain=0x%04x  Y_mean=%d  Y_stdev=%d\" % (gain, s.l_mean(), s.l_stdev()))
sensor.run(0)
print(\"done\")
'"
EOF