echo "=== adapters ==="
for a in /sys/bus/i2c/adapters/*; do
  printf "  %s: " "$a"
  cat "$a/name" 2>/dev/null
done
echo "=== i2c clients ==="
for c in /sys/bus/i2c/devices/*; do
  printf "  %s name=%s addr=" "$c" "$(cat $c/name 2>/dev/null)"
  cat "$c/uevent" 2>/dev/null | grep -o 'DEVNAME=.*'
done
echo "=== nfc / pn532 / bcm in dmesg ==="
dmesg | grep -i -E "nfc|pn532|bcm2079|20793|nxp" | head -20
echo "=== which android service owns the nfc ==="
ls -l /dev/nfc* 2>/dev/null
