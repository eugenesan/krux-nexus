#!/bin/bash
# Boot our kernel, open the camera LATE (so the capture lands inside the
# window /proc/last_kmsg keeps), then reboot into the ROM's kernel and
# read our kernel's log from there.
set -e
R=/home/user/Develop/src/android-air-gapped
cd $R

echo "=== boot our kernel ==="
timeout 60 adb shell 'rm -f /data/local/tmp/*_cfg_*.bin' 2>/dev/null || true
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-cap-nowd.img 2>&1 | tail -1 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [$i] $v"
    echo "$v" | grep -q 'g856576f0623\|g[0-9a-f]\{10\} (user@nuc7)' && break
  fi
  sleep 2
done

echo
echo "=== let the log scroll (100s), then open the camera ==="
sleep 100
timeout 90 adb shell 'am start -n com.google.android.GoogleCamera/com.android.camera.CameraLauncher' 2>&1 | tr -d '\r' | head -2 | sed 's/^/  /'
sleep 30

echo
echo "=== the captures (as seen by the running system) ==="
timeout 40 adb shell 'ls -la /data/local/tmp/*_cfg_*.bin 2>&1' 2>&1 | tr -d '\r' | sed 's/^/  /'

echo
echo "=== pull the captures ==="
rm -rf /home/user/Develop/src/android-air-gapped/standalone/cam/data; mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data
for f in $(timeout 40 adb shell 'ls /data/local/tmp/*_cfg_*.bin 2>/dev/null' | tr -d '\r'); do
  b=$(basename "$f")
  timeout 120 adb pull "$f" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/$b" >/dev/null 2>&1 || echo "    (failed: $b)"
done
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/ | tail -20 | sed 's/^/  /'

echo
echo "=== reboot into the ROM's kernel and read our log ==="
timeout 60 adb reboot 2>&1 | tr -d '\r' || true
for i in $(seq 1 60); do
  timeout 6 adb get-state 2>/dev/null | grep -q device && { echo "  adb up after ${i}s"; break; }
  sleep 2
done
timeout 120 adb shell 'su -c "cat /proc/last_kmsg"' > /tmp/opencode/last_kmsg3.txt 2>/dev/null
tr -d '\r' < /tmp/opencode/last_kmsg3.txt > /tmp/opencode/last_kmsg3.clean
echo "  --- lines: $(wc -l < /tmp/opencode/last_kmsg3.clean)"
echo "  --- the window:"
head -2 /tmp/opencode/last_kmsg3.clean | sed 's/^/    /'
echo "  --- our DEBUG lines:"
grep -n 'DEBUG' /tmp/opencode/last_kmsg3.clean | sed 's/^/    /' | head -20
echo "  --- the camera's kernel messages:"
grep -inE 'msm_post_event|msm_isp|msm_cpp|CSI|csid|csiphy|ispif' /tmp/opencode/last_kmsg3.clean | tail -25 | sed 's/^/    /'
