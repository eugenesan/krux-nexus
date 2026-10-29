#!/usr/bin/env bash
# Test auto-exposure algorithm convergence.
# Usage: bash scripts/cam/test-ae.sh
set -e
cd "$(dirname "$0")/../.."

echo "=== Starting camd ==="
adb shell 'pkill camd' 2>/dev/null || true
sleep 1
adb shell 'cd /data/krux && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12

echo "=== Auto-exposure convergence test ==="
adb shell "cd /data/krux && ./micropython -c '
import sensor, time

TARGET = 80
MIN_EXP = 100
MAX_EXP = 1200
MIN_GAIN = 0x0020
MAX_GAIN = 0x01ff
EXP_STEP = 100
GAIN_STEP = 0x0020
DEADBAND = 0.2

exposure = 1248
gain = 0x00e0

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
sensor.set_exposure_gain(exposure, gain)

for frame in range(10):
    time.sleep(0.3)
    img = sensor.snapshot()
    s = img.get_statistics()
    mean = s.l_mean()
    print(\"frame %d: Y_mean=%d  exp=%d  gain=0x%04x\" % (frame, mean, exposure, gain))

    if mean < TARGET * (1 - DEADBAND):
        if exposure < MAX_EXP:
            exposure = min(exposure + EXP_STEP, MAX_EXP)
            sensor.set_exposure(exposure)
        elif gain < MAX_GAIN:
            gain = min(gain + GAIN_STEP, MAX_GAIN)
            sensor.set_gain(gain)
    elif mean > TARGET * (1 + DEADBAND):
        if gain > MIN_GAIN:
            gain = max(gain - GAIN_STEP, MIN_GAIN)
            sensor.set_gain(gain)
        elif exposure > MIN_EXP:
            exposure = max(exposure - EXP_STEP, MIN_EXP)
            sensor.set_exposure(exposure)

sensor.run(0)
print(\"done\")
'"
EOF