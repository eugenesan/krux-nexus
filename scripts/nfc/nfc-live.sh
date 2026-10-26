echo "=== 1. is the CAF NFC driver live right now? ==="
echo "  nfc service    : $(getprop init.svc.nfc) / $(getprop init.svc.vendor.nfc-1-0-service)"
echo "  chip present   : $(ls /sys/bus/i2c/devices/0-0077 2>/dev/null >/dev/null && echo yes || echo NO)"
echo "  device node    : $(ls -l /dev/bcm2079x 2>&1 | head -1)"
echo "  can it be opened: $(dd if=/dev/bcm2079x bs=1 count=1 2>&1 | head -1)"
echo "  -- NFC log: did the chip initialise? --"
logcat -b all -d 2>/dev/null | grep -iE 'bcm2079|BCMI2CNFC|nfc_nci.*download|firmware patch' | tail -8
echo
echo "=== 2. the NFC HAL config (transport + chip id) ==="
for c in /system/vendor/etc/libnfc-nci-20791b05.conf /system/etc/libnfc-nci.conf; do
  if [ -r "$c" ]; then
    echo "  -- $c --"
    sed 's/[[:space:]]*#.*$//' "$c" | grep -E '=' | sed 's/^/    /'
  fi
done

echo
echo "=== 3. touchscreen capabilities (does it do multi-touch?) ==="
echo "  -- device nodes --"
ls -l /dev/input/ 2>/dev/null | sed 's/^/    /'
echo "  -- event1 caps --"
getevent -pi /dev/input/event1 2>/dev/null | sed -n '1,40p' | sed 's/^/    /'

echo
echo "=== 4. display: what a minimal userspace would write to ==="
ls -l /dev/graphics/fb0 /dev/fb0 2>&1 | sed 's/^/    /'
echo "  fb0 name : $(cat /sys/class/graphics/fb0/name 2>/dev/null)"
echo "  stride   : $(cat /sys/class/graphics/fb0/stride 2>/dev/null)"
echo "  bpp      : $(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null)"
echo "  var_screeninfo: $(cat /sys/class/graphics/fb0/mode 2>/dev/null)"
echo "  -- 3D/gsm devices (Krux will want plain fb0, not these) --"
ls -l /dev/kgsl-3d0 /dev/ion 2>&1 | head -3 | sed 's/^/    /'