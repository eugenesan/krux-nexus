echo "=== 0. current device state ==="
echo "  boot_completed : $(getprop sys.boot_completed)"
echo "  init.rc airgap : $(grep -c airgap /system/etc/init/hw/init.rc 2>/dev/null) refs"
echo "  Telecom present: $([ -d /system/priv-app/Telecom ] && echo yes || echo REMOVED)"
echo "  BT HAL present : $([ -e /vendor/bin/hw/android.hardware.bluetooth@1.0-service ] && echo yes || echo REMOVED)"
echo "  wifi HAL       : $([ -e /vendor/bin/hw/android.hardware.wifi-service-lazy ] && echo yes || echo REMOVED)"
echo "  rild           : $(getprop init.svc.vendor.ril-daemon)"

echo
echo "=== 1. NFC: the device node the CAF driver exposes ==="
echo "  /dev/nfc* : $(ls -l /dev/nfc* 2>&1 | head -3)"
echo "  /dev/bcm* : $(ls -l /dev/bcm* 2>&1 | head -3)"
echo "  -- platform device --"
for d in /sys/devices/platform/*nfc* /sys/devices/platform/*bcm2079*; do
  [ -d "$d" ] || continue
  echo "  $(basename $d)"
  echo "     driver    : $(basename "$(readlink -f $d/driver 2>/dev/null)")"
  for f in modalias of_node/compatible name; do
    [ -r "$d/$f" ] && echo "     $f: $(tr '\0' ' ' < $d/$f)"
  done
done
echo "  -- is the NFC driver bound? --"
ls /sys/bus/platform/drivers/ 2>/dev/null | grep -iE 'nfc|bcm'

echo
echo "=== 2. which BUS is the controller on? ==="
echo "  i2c adapters : $(ls /sys/bus/i2c/devices/ 2>/dev/null | grep -c 'i2c-')"
echo "  i2c devices  : $(ls /sys/bus/i2c/devices/ 2>/dev/null | grep -v '^i2c-' | tr '\n' ' ')"
echo "  -- enumerate i2c and look for an NFC-ish chip --"
for a in /sys/bus/i2c/devices/i2c-*; do
  [ -d "$a" ] || continue
  name=$(cat "$a/name" 2>/dev/null)
  case "$name" in
    *nfc*|*BCM*|*bcm*|*2079*|*nxp*|*pn5*|*ST25*|*ST3*) echo "    $name" ;;
  esac
done
echo "  -- SPI for comparison --"
echo "    spi devices: $(ls /sys/bus/spi/devices/ 2>/dev/null | tr '\n' ' ')"

echo
echo "=== 3. NFC kernel config: built-in or module ==="
zcat /proc/config.gz 2>/dev/null | grep -iE 'NFC' | grep -vE '^#' | sed 's/^/    /'

echo
echo "=== 4. NFC firmware, as loaded ==="
ls -l /vendor/firmware/*.ncd 2>/dev/null | sed 's/^/    /'
echo "  -- what does the driver reference? --"
d=$(ls -d /sys/devices/platform/*nfc* 2>/dev/null | head -1)
[ -n "$d" ] && strings "$d/uevent" 2>/dev/null | grep -i ncd | sed 's/^/    /'

echo
echo "=== 5. NFC userspace: what Android pieces exist to reuse ==="
echo "  HAL binaries:"
for p in /vendor/bin/hw/*nfc* /system/vendor/bin/hw/*nfc* /vendor/bin/hw/*Nfc*; do
  [ -e "$p" ] && echo "    $p"
done
echo "  HAL libraries:"
ls /vendor/lib/hw/*nfc* /vendor/lib64/hw/*nfc* 2>/dev/null | sed 's/^/    /'
find /vendor /system -name 'libnfc*' 2>/dev/null | sed 's/^/    /'
echo "  packages:"
pm list packages -f 2>/dev/null | grep -iE 'nfc|tag|secure' | sed 's/^/    /'
echo "  VINTF:"
grep -iE '<name>android.hardware.nfc' /vendor/etc/vintf/manifest.xml 2>/dev/null | sed 's/^/    /'
echo "  init rc that starts it:"
grep -rlnE 'service .*nfc' /system/etc/init /vendor/etc/init 2>/dev/null | sed 's/^/    /'

echo
echo "=== 6. touchscreen controller ==="
echo "  -- input devices --"
awk '/^N:/{n=$0} /^H:/{print "   " n " | " $0}' /proc/bus/input/devices 2>/dev/null | head -10
echo "  -- touch drivers in config --"
zcat /proc/config.gz 2>/dev/null | grep -iE 'TOUCHSCREEN|FT_|SYNA|MAXTOUCH|ATSM|TOUCH' | grep -vE '^#' | sed 's/^/    /'

echo
echo "=== 7. display ==="
echo "  fb0   : $(cat /sys/class/graphics/fb0/name 2>/dev/null)"
echo "  size  : $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null)"
echo "  drm   : $(ls /sys/class/drm/ 2>/dev/null | tr '\n' ' ')"
echo "  panel : $(cat /proc/mdss_dsi_panel_node 2>/dev/null | head -1)"