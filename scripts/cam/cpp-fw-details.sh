#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
echo "=== the ROM's CPP / firmware messages ==="
timeout 60 adb shell 'su -c "dmesg | grep -iE \"cpp|firmware|blob\" | head -25"' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== the DT: a firmware path for the loader? ==="
timeout 60 adb shell 'su -c "cat /proc/device-tree/firmware-path 2>/dev/null; ls /proc/device-tree/ | head -5; find /proc/device-tree -maxdepth 2 -name \"*firmware*\" 2>/dev/null | head"' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== the cpp firmware files in detail ==="
timeout 60 adb shell 'su -c "ls -la /vendor/firmware/cpp_firmware*; md5sum /vendor/firmware/cpp_firmware*"' 2>&1 | tr -d '\r' | sed 's/^/  /'
echo
echo "=== is there a firmware_class path? ==="
timeout 60 adb shell 'su -c "ls /sys/class/firmware 2>/dev/null; cat /proc/interrupts | grep -c . ; ls -la /data/vendor/firmware 2>/dev/null | head"' 2>&1 | tr -d '\r' | sed 's/^/  /'
