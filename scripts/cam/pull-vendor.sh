#!/bin/bash
# Pull the vendor's IMX179 sensor library and look for the register tables.
cd /home/user/Develop/src/android-air-gapped || exit 1
mkdir -p /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs

for f in /vendor/lib/libmmcamera_imx179.so /vendor/lib/libmmcamera2_sensor_modules.so \
         /vendor/lib/libchromatix_imx179_common.so /vendor/lib/libchromatix_imx179_preview.so; do
  b=$(basename $f)
  timeout 120 adb shell "su -c 'cp $f /data/local/tmp/$b; chmod 644 /data/local/tmp/$b'" >/dev/null 2>&1
  timeout 180 adb pull "/data/local/tmp/$b" "/home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/$b" 2>&1 | tail -1
done

echo
ls -la /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/ | sed 's/^/  /'
echo
echo "=== symbols mentioning imx179 in each ==="
for f in /home/user/Develop/src/android-air-gapped/standalone/cam/data/vendor/rom-libs/*.so; do
  echo "--- $(basename $f)"
  arm-linux-gnueabi-nm -D --defined-only "$f" 2>/dev/null | grep -i 'imx179\|settings\|mode' | head -12 | sed 's/^/    /'
done
