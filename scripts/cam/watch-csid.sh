#!/bin/bash
# Run the imaging server and watch the CSID's interrupt counter: if it
# climbs, the sensor's data is arriving and the problem is downstream
# (the ISP); if it stays at zero, the sensor is not streaming.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== the CSID/CSIPHY interrupts before ==="
timeout 30 adb shell 'grep -iE "csid|csi|mipi" /proc/interrupts' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== run the imaging server in the background ==="
timeout 60 adb shell 'cd /data/local/tmp && (./imaging9 1632 1224 0x44000000 rear 0 /data/local/tmp/cpp_firmware_v1_2_0.fw 0 binned > /data/local/tmp/imaging9.log 2>&1 &)' 2>&1 | tr -d '\r' | sed 's/^/  /'
for t in 5 10 15 20; do
  sleep $t
  echo "=== after $((t*2))s: the CSID interrupts ==="
  timeout 30 adb shell 'grep -iE "csid|csi" /proc/interrupts | head -4' 2>&1 | tr -d '\r' | sed 's/^/  /'
done

echo
echo "=== the imaging server's log ==="
timeout 60 adb shell 'tail -25 /data/local/tmp/imaging9.log' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== the kernel's camera messages ==="
timeout 60 adb shell 'dmesg | grep -iE "msm_isp|msm_csid|csiphy|msm_sensor|overflow|buffer|vfe" | tail -25' 2>&1 | tr -d '\r' | sed 's/^/  k: /'
