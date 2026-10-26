#!/bin/bash
# Boot the genuine TWRP image and pull the TWRP kernel's symbol table.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== fastboot state ==="
timeout 15 fastboot devices 2>&1 | sed 's/^/  /'

echo "=== booting genuine TWRP ==="
timeout 240 fastboot boot images/twrp-3.7.0_9-HH.R.17-MADV_WIPEONFORK.img 2>&1 | sed 's/^/  /'

echo "=== waiting for adb ==="
timeout 150 adb wait-for-device 2>&1 | sed 's/^/  /'
timeout 20 adb devices -l 2>&1 | sed 's/^/  /'

echo "=== pulling kallsyms ==="
timeout 90 adb shell cat /proc/kallsyms > /tmp/opencode/twrp-kallsyms.txt 2>/dev/null
wc -l /tmp/opencode/twrp-kallsyms.txt

echo "=== key symbols in the TWRP kernel ==="
grep -E ' (ffs_epfile_io|ffs_epfile_read|dwc3_gadget_ep_queue|__dwc3_gadget_ep_queue|ffs_ep0_open|ffs_data_opened|ffs_epfile_write)$' /tmp/opencode/twrp-kallsyms.txt | sed 's/^/  /'

echo "=== text base ==="
grep -E ' _text$' /tmp/opencode/twrp-kallsyms.txt | sed 's/^/  /'
