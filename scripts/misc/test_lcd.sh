#!/bin/bash
# LCD display test: fill screen yellow, then display a camera frame
set -e
ADB=${ADB:-adb}
$ADB shell 'pkill camd' 2>/dev/null || true
sleep 1
$ADB shell 'cd /data/local/tmp && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12
$ADB shell "cd /data/krux && ./micropython -c '
import sensor, lcd
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
lcd.init()
img = sensor.snapshot()
lcd.display(img)
print(\"display OK\")
sensor.run(0)
'"