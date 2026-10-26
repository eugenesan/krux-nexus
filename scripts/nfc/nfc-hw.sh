echo "=== NFC controller: what is it, and on which bus? ==="
echo "  -- NFC config --"
zcat /proc/config.gz 2>/dev/null | grep -iE 'NFC' | grep -vE '=m$'
echo
echo "  -- NFC device node --"
ls -l /dev/nfc* /dev/bcm* /dev/pn* 2>/dev/null
echo
echo "  -- platform device --"
ls -d /sys/devices/platform/*nfc* /sys/devices/platform/*bcm* 2>/dev/null
echo
echo "  -- which bus? (i2c adapters, and whether nfc sits on one) ==="
echo "  i2c adapters: $(ls /sys/bus/i2c/devices/ 2>/dev/null | tr '\n' ' ')"
echo "  spi devices  : $(ls /sys/bus/spi/devices/ 2>/dev/null | tr '\n' ' ')"
echo
echo "  -- is the NFC driver bound? --"
ls /sys/bus/platform/drivers/ 2>/dev/null | grep -iE 'nfc|bcm' 
for d in /sys/devices/platform/*nfc*; do
  [ -d "$d" ] || continue
  echo "    $(basename $d): driver=$(basename "$(readlink -f $d/driver 2>/dev/null)" 2>/dev/null)"
done
echo
echo "  -- firmware blobs --"
ls -l /vendor/firmware/ 2>/dev/null | grep -iE 'ncd|nfc|bcm2079'
echo
echo "=== touchscreen: which controller? ==="
echo "  -- input devices --"
cat /proc/bus/input/devices 2>/dev/null | grep -E "^N:|^H:" | paste - - 2>/dev/null | sed 's/^/    /' | head -12
echo
echo "  -- touch IC in kernel config --"
zcat /proc/config.gz 2>/dev/null | grep -iE 'TOUCHSCREEN|FT_|SYNA_|MAXTOUCH|ATSM' | grep -vE '=m$' | sed 's/^/    /'
echo
echo "=== display ==="
echo "  panel: $(cat /sys/class/graphics/fb0/name 2>/dev/null || echo unknown)"
echo "  drm connectors: $(ls /sys/class/drm/ 2>/dev/null | grep -c connector)"
echo "  framebuffer size: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null)"