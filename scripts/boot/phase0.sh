#!/system/bin/sh
# Phase 0 -- capture the Android reference implementation.
# Read-only. Everything here is a recording, nothing is changed.
OUT=/data/local/tmp/phase0
mkdir -p "$OUT"
say(){ echo "$*"; echo "$*" >> "$OUT/phase0.log"; }

say "=== A. identity: what exactly is running ==="
{
  echo "date            : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "build.fingerprint: $(getprop ro.build.fingerprint)"
  echo "kernel           : $(uname -a)"
  echo "cmdline          : $(cat /proc/cmdline)"
} | tee "$OUT/00-identity.txt"

say ""
say "=== B. NFC: the reference stack (hashes matter -- this is what we port) ==="
{
  for f in /vendor/bin/hw/android.hardware.nfc@1.0-service \
           /vendor/lib/hw/android.hardware.nfc@1.0-impl-bcm.so \
           /vendor/lib/hw/nfc_nci.bcm2079x.default.so \
           /vendor/etc/init/android.hardware.nfc@1.0-service.rc \
           /system/vendor/etc/libnfc-nci-20791b05.conf \
           /system/etc/libnfc-nci.conf \
           /vendor/firmware/bcm2079x-b5_firmware.ncd \
           /vendor/firmware/bcm2079x-b5_pre_firmware.ncd ; do
    if [ -e "$f" ]; then
      echo "$f"
      echo "    size  : $(stat -c %s "$f")"
      echo "    sha256: $(sha256sum "$f" | cut -d' ' -f1)"
    else
      echo "$f   MISSING"
    fi
  done
} | tee "$OUT/01-nfc-artifacts.txt"

say ""
say "=== C. NFC runtime state written by the HAL at init ==="
{
  echo "nfc.initialized      : $(getprop nfc.initialized)"
  echo "hal service          : $(getprop init.svc.vendor.nfc_hal_service)"
  echo "hal pid              : $(getprop init.svc_debug_pid.vendor.nfc_hal_service)"
  echo "device node          : $(ls -l /dev/bcm2079x 2>&1)"
  echo "i2c binding          : $(readlink -f /sys/bus/i2c/devices/0-0077 2>/dev/null)"
  echo "--- /data/nfc (written by the HAL; the port must reproduce this) ---"
  ls -laR /data/nfc 2>&1
  echo "--- /data/nfc/param ---"
  xxd /data/nfc/param 2>/dev/null | head -40 || od -A x -t x1z /data/nfc/param 2>/dev/null | head -40
} | tee "$OUT/02-nfc-state.txt"

say ""
say "=== D. NFC HAL strings: the ioctl + protocol surface we must reimplement ==="
for s in /vendor/lib/hw/nfc_nci.bcm2079x.default.so \
         /vendor/lib/hw/android.hardware.nfc@1.0-impl-bcm.so \
         /vendor/bin/hw/android.hardware.nfc@1.0-service ; do
  [ -e "$s" ] || continue
  echo "-- $s --" | tee -a "$OUT/03-nfc-strings.txt"
  strings "$s" 2>/dev/null | grep -iE 'BCMI2CNFC|/dev/|ncd|patch|reset|power|download|i2c|spi|fd =' \
      | sort -u | head -60 | tee -a "$OUT/03-nfc-strings.txt"
done

say ""
say "=== E. does the HAL binary carry symbols we can read? ==="
{
  echo "-- dynsym count --"
  readelf -sW /vendor/lib/hw/nfc_nci.bcm2079x.default.so 2>/dev/null | wc -l
  echo "-- any bcm/i2c symbol names --"
  readelf -sW /vendor/lib/hw/nfc_nci.bcm2079x.default.so 2>/dev/null \
      | grep -iE 'phDal|bcm|I2c' | head -40
  echo "-- needed shared libs --"
  readelf -dW /vendor/lib/hw/nfc_nci.bcm2079x.default.so 2>/dev/null | grep NEEDED
} | tee "$OUT/04-nfc-symbols.txt"

say ""
say "=== F. display facts + a reference framebuffer dump ==="
{
  echo "fb0        : $(ls -l /dev/graphics/fb0 2>&1)"
  echo "name       : $(cat /sys/class/graphics/fb0/name 2>/dev/null)"
  echo "stride     : $(cat /sys/class/graphics/fb0/stride 2>/dev/null)"
  echo "bpp        : $(cat /sys/class/graphics/fb0/bits_per_pixel 2>/dev/null)"
  echo "virtual_size: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null)"
  echo "NOTE: stride 4352 != 1920*4. The framebuffer is padded; a blit that"
  echo "      assumes width*4 will be skewed. The port must honour stride."
} | tee "$OUT/05-display.txt"
dd if=/dev/graphics/fb0 of="$OUT/fb0-top.bin" bs=4352 count=64 2>/dev/null \
  && echo "fb0-top.bin: $(stat -c %s "$OUT/fb0-top.bin") bytes (one 64-row band)" \
       | tee -a "$OUT/05-display.txt"

say ""
say "=== G. touch: evdev capabilities (what a userspace reader gets) ==="
getevent -pi /dev/input/event1 2>/dev/null | tee "$OUT/06-touch-caps.txt"

say ""
say "=== H. what a minimal rootfs will need, and nothing else ==="
{
  for n in /dev/graphics/fb0 /dev/input/event1 /dev/bcm2079x \
           /dev/ttyHSL0 /dev/ttyHS99 /dev/ion /dev/kgsl-3d0 ; do
    printf '  %-24s %s\n' "$n" "$(ls -l $n 2>&1 | awk '{print $1, $3, $4, $5, $6, $NF}')"
  done
} | tee "$OUT/07-devnodes.txt"

say ""
echo "captured into $OUT -- $(ls -1 $OUT | wc -l) files"