#!/bin/bash
# Boot our kernel and run the imaging server + capture client.
R=/home/user/Develop/src/android-air-gapped
cd $R || exit 1

echo "=== are we in android or already on our kernel? ==="
timeout 20 adb shell 'cat /proc/version' 2>&1 | tr -d '\r' | head -1 | sed 's/^/  /'

if ! timeout 20 adb shell 'grep -q "e0cd9d48553" /proc/version' 2>/dev/null; then
  echo "=== reboot to bootloader ==="
  timeout 30 adb reboot bootloader 2>&1 | tr -d '\r'
  for i in $(seq 1 40); do
    if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
      echo "  in fastboot after ${i}s"; break
    fi
    sleep 1
  done
  echo "=== boot our kernel ==="
  timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-clean-twrp.img 2>&1 | sed 's/^/  /'
  timeout 150 adb wait-for-device 2>&1 | sed 's/^/  /'
  timeout 20 adb devices 2>&1 | sed 's/^/  /'
fi

echo
echo "=== push the imaging server ==="
timeout 60 adb push /tmp/opencode/imaging7 /data/local/tmp/imaging7 2>&1 | tail -1
timeout 30 adb shell 'chmod 755 /data/local/tmp/imaging7' >/dev/null 2>&1

echo
echo "=== run it ==="
timeout 90 adb shell '/data/local/tmp/imaging7 640 480' 2>&1 | tr -d '\r' | head -40
