#!/bin/bash
# Pull the installed ROM kernel image and its symbol table for comparison.
cd /home/user/Develop/src/android-air-gapped || exit 1

echo "=== kptr_restrict ==="
timeout 30 adb shell 'su -c "echo 0 > /proc/sys/kernel/kptr_restrict"' 2>&1 | tr -d '\r'

echo "=== kallsyms ==="
timeout 90 adb shell 'su -c "cat /proc/kallsyms"' > /tmp/opencode/rom-kallsyms.txt 2>/dev/null
wc -l /tmp/opencode/rom-kallsyms.txt

echo "=== pull the /boot partition ==="
timeout 120 adb shell 'su -c "dd if=/dev/block/mmcblk0p19 of=/data/local/tmp/rom-boot.img bs=2048"' 2>&1 | tail -2 | tr -d '\r'
timeout 180 adb pull /data/local/tmp/rom-boot.img /tmp/opencode/rom-boot.img 2>&1 | tail -1

echo "=== key symbols in the ROM kernel ==="
grep -E ' (ffs_epfile_io|ffs_epfile_read|ffs_epfile_write|dwc3_gadget_ep_queue|__dwc3_gadget_ep_queue|ffs_ep0_open|_text)$' /tmp/opencode/rom-kallsyms.txt
