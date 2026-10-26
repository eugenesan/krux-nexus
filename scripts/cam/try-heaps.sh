#!/bin/bash
# Try each ION heap mask and see which one the ISP can map.
cd /home/user/Develop/src/android-air-gapped || exit 1
for m in 0x40000000 0x10000000 0x08000000 0x02000000 0x00400000; do
  echo "=== heap mask $m ==="
  timeout 30 adb shell 'dmesg -c >/dev/null 2>&1' >/dev/null 2>&1
  timeout 40 adb shell "/data/local/tmp/imaging7 640 480 $m" 2>&1 | tr -d '\r' | grep -E 'ion|isp|frame' | head -8 | sed 's/^/  /'
  timeout 30 adb shell 'dmesg | grep -iE "isp|csid|csiphy" | head -6' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
done
