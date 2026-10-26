echo "=== A. what is /dev/bcm2079x backed by? ==="
echo "  -- the only SPI device --"
for f in modalias compatible of_node/compatible; do
  [ -r /sys/bus/spi/devices/spi7.0/$f ] && echo "  spi7.0/$f: $(tr '\0' ' ' < /sys/bus/spi/devices/spi7.0/$f)"
done
echo "  driver bound to spi7.0: $(basename "$(readlink -f /sys/bus/spi/devices/spi7.0/driver 2>/dev/null)")"
echo "  spi masters: $(ls /sys/class/spi_master/ 2>/dev/null | tr '\n' ' ')"
echo "  -- i2c devices with full names --"
for a in /sys/bus/i2c/devices/*-00*; do
  [ -d "$a" ] || continue
  printf '    %-12s name=%-24s class=%s\n' "$(basename $a)" "$(cat $a/name 2>/dev/null)" "$(cat $a/class 2>/dev/null)"
done
echo
echo "  -- /proc/bus/nfc? any driver registration --"
ls /proc/bus/ 2>/dev/null | tr '\n' ' '; echo
echo "  modules holding bcm2079x:"
grep -l bcm2079x /proc/modules 2>/dev/null || echo "    (not a module -> built into the kernel)"

echo
echo "=== B. what does Android's NFC HAL config say? ==="
for c in /system/vendor/etc/libnfc-nci-20791b05.conf /system/etc/libnfc-nci.conf; do
  [ -r "$c" ] && { echo "  -- $c --"; grep -vE '^\s*#|^\s*$' "$c" | sed 's/^/    /'; }
done

echo
echo "=== C. NFC HAL service: what it links and opens ==="
ls -l /vendor/lib/hw/nfc_nci.bcm2079x.default.so /vendor/lib/hw/android.hardware.nfc@1.0-impl-bcm.so 2>/dev/null | sed 's/^/  /'
echo "  -- strings: device node + transport it expects --"
strings /vendor/lib/hw/nfc_nci.bcm2079x.default.so 2>/dev/null | grep -iE '/dev/|bcm2079|spi|i2c|fw_download' | sort -u | head -15 | sed 's/^/    /'
echo "  -- impl shim --"
strings /vendor/lib/hw/android.hardware.nfc@1.0-impl-bcm.so 2>/dev/null | grep -iE '/dev/|\.ncd|libnfc' | sort -u | head -10 | sed 's/^/    /'

echo
echo "=== D. the NFC init service ==="
cat /vendor/etc/init/android.hardware.nfc@1.0-service.rc 2>/dev/null | grep -vE '^\s*$' | sed 's/^/    /'

echo
echo "=== E. touchscreen: which chip, which bus ==="
for a in /sys/class/input/event1/device/; do
  [ -e "$a" ] && echo "  event1 -> $(basename "$(readlink -f $a)")"
done
ln -sf /sys/class/input/event1 /tmp/x 2>/dev/null
p=$(readlink -f /sys/class/input/event1/device 2>/dev/null)
echo "  device path : $p"
[ -n "$p" ] && echo "  driver      : $(basename "$(readlink -f $p/driver 2>/dev/null)")"
[ -n "$p" ] && for f in modalias of_node/compatible; do [ -r "$p/$f" ] && echo "  $f: $(tr '\0' ' ' < $p/$f)"; done
echo "  -- parent bus --"
[ -n "$p" ] && q=$(readlink -f "$p/..") && echo "  parent: $q" && [ -e "$q/name" ] && echo "  parent name: $(cat $q/name)"