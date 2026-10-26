#!/bin/bash
set -e
R=/home/user/Develop/src/android-air-gapped
W=/tmp/opencode/kcmp
cd $R

echo "=== the cmdline we use ==="
grep '^cmdline=' $W/romandroid/meta.txt | sed 's/^/  /'

echo
echo "=== add androidboot.selinux=permissive so the shell may read the log ==="
sed -i "s/^cmdline=.*/cmdline='console=ttyHSL0,115200,n8 androidboot.hardware=hammerhead user_debug=31 msm_watchdog_v2.enable=1 androidboot.bootdevice=msm_sdcc.1 androidboot.selinux=permissive loop.max_part=7'/" \
    $W/romandroid/meta.txt
grep '^cmdline=' $W/romandroid/meta.txt | sed 's/^/  /'

python3 standalone/rootfs/bootimg.py pack $W/romandroid $W/romandroid/ramdisk \
    standalone/rootfs/stock/boot-ourkern-dbg-android.img | sed 's/^/  /'

echo
echo "=== boot it ==="
timeout 30 adb reboot bootloader 2>&1 | tr -d '\r' || true
for i in $(seq 1 40); do
  if timeout 8 fastboot devices 2>/dev/null | grep -q fastboot; then
    echo "  in fastboot after ${i}s"; break
  fi
  sleep 1
done
timeout 240 fastboot boot standalone/rootfs/stock/boot-ourkern-dbg-android.img 2>&1 | tail -2 | sed 's/^/  /'
timeout 240 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices 2>&1 | sed 's/^/  /'

echo
echo "=== can the shell read the kernel log now? ==="
timeout 60 adb shell 'dmesg | wc -l' 2>&1 | tr -d '\r' | sed 's/^/  /'
