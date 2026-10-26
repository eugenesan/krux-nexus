#!/bin/bash
cd /home/user/Develop/src/android-air-gapped || exit 1
mkdir -p standalone/cam/fw

echo "=== pull the three CPP firmware files ==="
for f in cpp_firmware_v1_1_1.fw cpp_firmware_v1_1_6.fw cpp_firmware_v1_2_0.fw; do
  timeout 60 adb shell "su -c 'cp /vendor/firmware/$f /data/local/tmp/ && chmod 644 /data/local/tmp/$f'" >/dev/null 2>&1
  timeout 120 adb pull "/data/local/tmp/$f" "standalone/cam/fw/$f" 2>&1 | tail -1 | sed 's/^/  /'
done
ls -la standalone/cam/fw/ | sed 's/^/  /'
sha256sum standalone/cam/fw/* | sed 's/^/  /'

echo
echo "=== the ioctl and its struct ==="
grep -rn -A3 'VIDIOC_MSM_CPP_LOAD_FIRMWARE' standalone/vendor/android_kernel_lge_hammerhead/include/media/ | sed 's/^/  /'
grep -rn -A6 'struct msm_camera_ioctl_ptr {' standalone/vendor/android_kernel_lge_hammerhead/include/media/ | sed 's/^/  /'
