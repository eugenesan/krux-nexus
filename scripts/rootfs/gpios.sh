echo "=== the vendor NFC device ==="
ls -l /dev/bcm2079x 2>&1
echo "=== the nfc gpios: sda=83 scl=84 wake=92 ven=94 ==="
for g in 83 84 92 94; do
  printf "  gpio%-3s direction=" "$g"
  cat "/sys/class/gpio/gpio$g/direction" 2>&1 | tr -d '\n'
  printf " value="
  cat "/sys/class/gpio/gpio$g/value" 2>&1
done
echo "=== the i2c adapter for the nfc chip (f9967000) ==="
ls /sys/bus/i2c/devices/ 2>&1
echo "=== android's nfc service ==="
ps -A 2>/dev/null | grep -i nfc
echo "=== can we still talk to the chip? (the driver owns it) ==="
ls -l /dev/i2c-0 2>&1
