#!/bin/bash
# QR code scan test: 20 frames, print any decoded QR codes
# Hold a QR code 15-25 cm from the rear camera while this runs.
set -e
ADB=${ADB:-adb}
$ADB shell 'pkill camd' 2>/dev/null || true
sleep 1
$ADB shell 'cd /data/krux && (./camd 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 >/tmp/camd.log 2>&1 &)'
sleep 12
$ADB shell "cd /data/krux && ./micropython -c '
import sensor
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.run(1)
for i in range(20):
    img = sensor.snapshot()
    qrs = img.find_qrcodes()
    if qrs:
        print(\"FOUND:\", [q.payload() for q in qrs])
sensor.run(0)
print(\"QR test done\")
'"