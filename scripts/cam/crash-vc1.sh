#!/bin/bash
# crash-vc1.sh -- reproduce the ISP-IF's single-non-zero-cid crash and read
# the trace.
#
# The crash: when the ISP-IF is given one channel that is not channel 0
# (the channel the image's packets actually arrive on), the kernel dies
# with a NULL dereference.  It is a known crasher, so it is only run
# with a way back:
#
#   * the kernel now restarts the device on a panic instead of letting
#     LGE's "kernel error" magic make the bootloader power it off, so
#     the phone comes back on its own;
#   * the panic is in the persistent ram console, so /proc/last_kmsg on
#     the next boot holds the backtrace.
#
# The configuration: the CSID's channel lookup table maps virtual
# channel 1 to the image's data type (0x2b) and virtual channel 0 to
# the 8-bit stream (0x12), and the ISP-IF is routed at channel 1 alone.
#
# Usage: crash-vc1.sh [cid] [lut_cid0] [lut_cid1]
set -u
cd /home/user/Develop/src/android-air-gapped || exit 1

CID=${1:-1}
LUT0=${2:-1}
LUT1=${3:-0}

echo "=== the state before"
timeout 30 adb shell 'dmesg -c >/dev/null 2>&1; echo cleared' >/dev/null 2>&1
timeout 30 adb shell 'ls -la /data/local/tmp/imaging9 /data/local/tmp/cpp_firmware_v1_2_0.fw' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== running the imaging server with the ISP-IF routed at cid $CID"
# the argument order (see imaging8.c's main):
#   1 w  2 h  3 ion mask  4 front/rear  5 front count  6 cpp firmware
#   7 stream source  8 binned  9 nocfg  10 nocycle
#  11 vc  12 data type  13 cid  14 csid  15 frames  16 isp vfe
#  17 ispif vfe  18 pixel clock  19 the other data type
#  20 lut cid 0  21 lut cid 1  22 poke register  23 poke value  24 two cids
timeout 120 adb shell "cd /data/local/tmp && ./imaging9 3264 2448 0x44000000 rear 0 \
  /data/local/tmp/cpp_firmware_v1_2_0.fw 0 0 0 0 0 0 0x2b $CID 0 1 0 0 0 0x12 $LUT0 $LUT1 \
  0xffffffff 0" 2>&1 | tr -d '\r' | tail -30 | sed 's/^/  /'

echo
echo "=== waiting for the device to come back (the panic restarts it)"
for i in $(seq 1 30); do
  sleep 5
  a=$(timeout 8 adb get-state 2>&1 | tr -d '\r')
  if [ "$a" = "device" ]; then echo "  adb is back after $((i * 5))s"; break; fi
done

echo
echo "=== the panic (from the persistent ram console)"
timeout 60 adb shell 'cat /proc/last_kmsg' 2>&1 | tr -d '\r' > /tmp/opencode/last_kmsg.txt
echo "  the log: $(wc -l < /tmp/opencode/last_kmsg.txt) lines"
grep -n -A 40 -iE "Unable to handle|Internal error|Oops|BUG:|Unable to handle kernel" \
  /tmp/opencode/last_kmsg.txt | head -60 | sed 's/^/  /'
echo
echo "=== the tail of the log (the last thing that happened)"
tail -30 /tmp/opencode/last_kmsg.txt | sed 's/^/  /'
