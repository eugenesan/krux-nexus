#!/bin/bash
# Boot our kernel with the ROM's Android ramdisk and watch what happens,
# polling adb so we can catch our kernel's dmesg before it dies.
cd /home/user/Develop/src/android-air-gapped || exit 1
IMG=${1:-standalone/rootfs/stock/boot-ourkern-diff5-android.img}
LOG=/tmp/opencode/bootwatch.log
: > $LOG

echo "=== to the bootloader ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done

echo "=== fastboot boot $IMG ==="
timeout 240 fastboot boot "$IMG" 2>&1 | tail -2 | sed 's/^/  /'

echo "=== watching (90s) ==="
for i in $(seq 1 45); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "[$i] adb up: $v" >> $LOG
    timeout 25 adb shell 'dmesg 2>/dev/null | tail -80' 2>/dev/null | tr -d '\r' >> $LOG
    echo "---" >> $LOG
    if echo "$v" | grep -q 'g1a2ab21fc89'; then
      echo "  [${i}s] OUR kernel is up"
    fi
  else
    echo "[$i] state: ${st:-none}" >> $LOG
  fi
  sleep 2
done
echo
echo "=== the log ==="
cat $LOG | sed 's/^/  /' | head -120
