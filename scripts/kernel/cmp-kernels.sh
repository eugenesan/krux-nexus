#!/bin/bash
# Compare ffs_epfile_io / dwc3_gadget_ep_queue between the ROM kernel (works),
# the TWRP kernel (works) and our kernel (adb offline).
cd /home/user/Develop/src/android-air-gapped || exit 1
OD=standalone/vendor/toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-objdump

echo "=== ROM kernel text base ==="
grep -E ' T _text$' /tmp/opencode/rom-kallsyms.txt

ROMTEXT=$(grep -E ' T _text$' /tmp/opencode/rom-kallsyms.txt | awk '{print $1}')
echo "ROMTEXT=$ROMTEXT"

echo
echo "########## ROM kernel: ffs_epfile_write / ffs_epfile_read ##########"
$OD -D -b binary -m arm --adjust-vma=0x$ROMTEXT \
    --start-address=0xc054e304 --stop-address=0xc054e360 /tmp/opencode/rom.raw | sed -n '7,40p'

echo
echo "########## ROM kernel: ffs_epfile_io head ##########"
$OD -D -b binary -m arm --adjust-vma=0x$ROMTEXT \
    --start-address=0xc054dcdc --stop-address=0xc054dd60 /tmp/opencode/rom.raw | sed -n '7,40p'
