#!/bin/bash
# sweep-vc.sh -- find the sensor register that sets the virtual channel.
#
# The image's packets arrive on virtual channel 1, but the camera HAL's
# lookup table maps VC 0 to the image's data type, so nothing is mapped
# and the frame is empty.  The sensor is told which virtual channel to
# use by one of its registers; this script writes a candidate register
# and reads back what the CSID saw in the packet header.
#
# The register is written after the vendor's own sensor sequence and
# before the stream starts (the imaging server's "poke" option), so the
# sensor is in the right mode with the register overridden.
#
# Usage: sweep-vc.sh [value] [register ...]
set -u
cd /home/user/Develop/src/android-air-gapped || exit 1

VALUE=${1:-0}
shift || true
REGS=${*:-"0x3302 0x0383 0x0387 0x4108 0x0401 0x030c 0x0102 0x0106 0x0301 0x0303 0x0305 0x0309 0x030b"}

for reg in $REGS; do
  echo "=== poking $reg = $VALUE"
  timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
  # the argument order (see imaging8.c's main):
  #   1 w  2 h  3 ion mask  4 front/rear  5 front count  6 cpp firmware
  #   7 stream source  8 binned  9 nocfg  10 nocycle
  #  11 vc  12 data type  13 cid  14 csid  15 frames  16 isp vfe
  #  17 ispif vfe  18 pixel clock  19 the other data type
  #  20 lut cid 0  21 lut cid 1  22 poke register  23 poke value  24 two cids
  timeout 90 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 \
    /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 0 0x2b 0 0 1 0 0 0 0x12 0 1 \
    $reg $VALUE" >/dev/null 2>&1
  timeout 30 adb shell 'dmesg | grep -E "CSID0 (LUT|irq)"' 2>&1 | tr -d '\r' | sed 's/^/    /'
done
