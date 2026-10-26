#!/bin/bash
# Does any MIPI data arrive?  Compare the interrupt counters before and
# after a run: if the CSID's IRQ never fires, nothing comes out of the
# sensor; if it fires, the data arrives and the loss is downstream.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== the camera IRQs before ==="
timeout 40 adb shell 'cat /proc/interrupts | grep -iE "csid|csiphy|isp|vfe|cam|mipi"' 2>&1 | tr -d '\r' | sed 's/^/  /'

timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
echo
echo "=== run the imaging server (rear camera) ==="
timeout 45 adb shell '/data/local/tmp/imaging8 640 480 0x40000000 rear' 2>&1 | tr -d '\r' | grep -E 'FRAME|no frame|sensor|isp' | head -8 | sed 's/^/  /'

echo
echo "=== the camera IRQs after ==="
timeout 40 adb shell 'cat /proc/interrupts | grep -iE "csid|csiphy|isp|vfe|cam|mipi"' 2>&1 | tr -d '\r' | sed 's/^/  /'
