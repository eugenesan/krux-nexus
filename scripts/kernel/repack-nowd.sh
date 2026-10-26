#!/bin/bash
# Repack the current kernel with msm_watchdog_v2.enable=0 (so a dead init
# does NOT get the SoC reset into the installed ROM kernel) and boot it with
# the ROM's Android ramdisk.
#
#   $1 = kernel image (zImage-dtb)
#   $2 = output boot.img
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
KERNEL=${1:-/tmp/opencode/ko-cap/arch/arm/boot/zImage-dtb}
OUT=${2:-$R/standalone/rootfs/stock/boot-ourkern-cap-nowd.img}

mkdir -p $W/romandroid
cp "$KERNEL" $W/romandroid/kernel
cp /tmp/opencode/romcheck/ramdisk $W/romandroid/ramdisk
python3 - "$W/romandroid/meta.txt" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p).read()
s = s.replace('msm_watchdog_v2.enable=1', 'msm_watchdog_v2.enable=0')
if 'androidboot.selinux=permissive' not in s:
    s = s.replace('loop.max_part=7', 'androidboot.selinux=permissive loop.max_part=7')
open(p,'w').write(s)
print("  cmdline:", re.search(r"cmdline='(.*)'", s).group(1))
PY

cd $R
python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk "$OUT" | sed 's/^/  /'

echo
echo "=== boot and watch ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  timeout 8 fastboot devices 2>/dev/null | grep -q fastboot && { echo "  fastboot after ${i}s"; break; }
  sleep 1
done
timeout 240 fastboot boot "$OUT" 2>&1 | tail -2 | sed 's/^/  /'
for i in $(seq 1 45); do
  st=$(timeout 6 adb get-state 2>/dev/null | tr -d '\r')
  if [ "$st" = "device" ]; then
    v=$(timeout 15 adb shell 'cat /proc/version' 2>/dev/null | tr -d '\r' | head -1)
    echo "  [${i}] $v"
    break
  fi
  sleep 2
done
