#!/system/bin/sh
# capture-kernel.sh -- record everything we need to compare two kernels on the
# same device. Run under whichever recovery/Android build is booted.
#
# WHY THIS IS A SCRIPT AND NOT A PILL OF adb shell COMMANDS
# The comparison that matters is TWRP's kernel vs LineageOS's kernel, and the
# notes in this project have twice conflated the two (once claiming the
# shipped-ROM config described the running kernel; once treating "the kernel"
# as a single thing). A single capture, written to a file with the build's own
# identity in the filename, removes the chance of that happening again.
#
# Outputs a flat text report on stdout. The caller pulls it and also pulls
# /proc/config.gz separately.

echo "=== identity ==="
echo "build.prop ro.build.id      : $(getprop ro.build.id 2>/dev/null)"
echo "build.prop ro.build.display : $(getprop ro.build.display.id 2>/dev/null)"
echo "build.prop ro.build.type    : $(getprop ro.build.type 2>/dev/null)"
echo "build.prop ro.bootmode      : $(getprop ro.bootmode 2>/dev/null)"
echo "ro.product.device           : $(getprop ro.product.device 2>/dev/null)"

echo
echo "=== kernel identity ==="
echo "uname -a  : $(uname -a 2>/dev/null)"
echo "uname -r  : $(uname -r 2>/dev/null)"
echo "proc/version:"
cat /proc/version 2>/dev/null

echo
echo "=== which cross-compiler built this ==="
# /proc/version's "(gcc version ...)" field is the ground truth. Print it alone
# so a diff between the two builds is one line, not a whole banner.
sed -n 's/.*(\(gcc version[^)]*\)).*/\1/p' /proc/version 2>/dev/null
sed -n 's/.*(\(clang version[^)]*\)).*/\1/p' /proc/version 2>/dev/null

echo
echo "=== config availability ==="
if [ -r /proc/config.gz ]; then
    echo "/proc/config.gz : PRESENT ($(wc -c < /proc/config.gz) bytes)"
    echo "config sha256    : $(sha256sum /proc/config.gz 2>/dev/null | cut -d' ' -f1)"
    echo "set options      : $(zcat /proc/config.gz 2>/dev/null | grep -c '^CONFIG')"
else
    echo "/proc/config.gz : ABSENT"
fi
if [ -r /proc/version ]; then
    # the embedded-config copy inside the running image, independent of procfs
    echo "IKCONFIG symbols : $(zcat /proc/config.gz 2>/dev/null | grep -cE '^CONFIG_IKCONFIG')"
fi

echo
echo "=== capability symbols (the four we need) ==="
zcat /proc/config.gz 2>/dev/null | grep -E '^CONFIG_(FB|FB_MSM|TOUCHSCREEN_LGE_SYNAPTICS|NFC_BCM2079X|MACH_MSM8974_HAMMERHEAD|BLK_DEV_INITRD|IKCONFIG_PROC|BCMDHD|BT=|MSM_SMD|MSM_IPC_ROUTER)=|^# CONFIG_(BCMDHD|BT|MSM_SMD|MSM_IPC_ROUTER) is not set'

echo
echo "=== device nodes we depend on ==="
for n in /dev/graphics/fb0 /dev/input/event1 /dev/bcm2079x; do
    printf '  %-22s ' "$n"
    if [ -e "$n" ]; then ls -l "$n" 2>/dev/null | sed 's/.*[0-9] //'; else echo "ABSENT"; fi
done

echo
echo "=== framebuffer geometry actually reported ==="
FB=/sys/class/graphics/fb0
for f in stride virtual_size bits_per_pixel blank; do
    [ -r "$FB/$f" ] && echo "  $f = $(cat $FB/$f 2>/dev/null)"
done

echo
echo "=== poweroff path present? ==="
echo "  /sys/devices/platform entries matching restart/power:"
ls /sys/devices/platform/ 2>/dev/null | grep -iE 'restart|power' | sed 's/^/    /'
echo "  pm_power_off is a kernel symbol only; see dmesg for the pmic path:"
dmesg 2>/dev/null | grep -iE 'restart|power.?off|qpnp_pon|pon_' | tail -5 | sed 's/^/    /'

echo
echo "=== done ==="