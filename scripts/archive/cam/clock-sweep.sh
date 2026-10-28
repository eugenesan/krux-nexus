#!/bin/bash
# Sweep the CAMIF's pixel clock: the clock divider snaps to discrete
# rates, and the camera front end rejects a frame whose timing does not
# match its clock.  The right clock is the one the sensor actually
# runs at.
set +e
R=/home/user/Develop/src/android-air-gapped
cd $R
timeout 120 adb push standalone/cam/imaging9 /data/local/tmp/imaging9 >/dev/null 2>&1

CLK="121000000 166000000 200000000 242500000 260000000 266666666 320000000 400000000 480000000 600000000 720000000 960000000"

for c in $CLK; do
  before=$(timeout 60 adb shell 'dmesg | grep -c "camif violation status"' 2>/dev/null | tr -d '\r')
  timeout 120 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 0 0x12 0 0 1 0 0 $c 2>&1 | grep -E 'GOT A FRAME' | head -1" 2>/dev/null | tr -d '\r'
  after=$(timeout 60 adb shell 'dmesg | grep -c "camif violation status"' 2>/dev/null | tr -d '\r')
  got=$(timeout 60 adb shell 'dmesg | grep -c "GOT A FRAME"' 2>/dev/null | tr -d '\r')
  actual=$(timeout 60 adb shell 'dmesg | grep "camif configured: clock" | tail -1' 2>/dev/null | tr -d '\r' | sed -e 's/.*clock \([0-9]*\).*/\1/')
  viol=$((after - before))
  echo "  clock $c -> actual $actual, violations $viol, frames $got"
  if [ "${got:-0}" -gt 0 ]; then echo "  >>> FRAMES with a $c pixel clock"; break; fi
  if [ "$viol" -eq 0 ]; then echo "  >>> NO violation with a $c pixel clock"; fi
done
