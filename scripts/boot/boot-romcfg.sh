#!/bin/bash
# Pack our ROM-config kernel with the ROM's Android ramdisk, boot it, and
# watch which kernel actually comes up.
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
KERNEL=${1:-/tmp/opencode/ko-romcfg/arch/arm/boot/zImage-dtb}
OUT=${2:-$R/standalone/rootfs/stock/boot-ourkern-romcfg-android.img}

mkdir -p $W/romandroid
cp "$KERNEL" $W/romandroid/kernel
cp /tmp/opencode/romcheck/ramdisk $W/romandroid/ramdisk
cd $R
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk "$OUT"

echo
echo "=== boot and watch ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot "$OUT" 2>&1 | tail -2 | sed 's/^/  /'
for i in $(seq 1 60); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [${i}] $v"
    if echo "$v" | grep -q 'g1a2ab21fc89'; then
      echo "  >>> OUR kernel is running"
      break
    fi
  fi
  sleep 2
done
