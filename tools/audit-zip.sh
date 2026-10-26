#!/usr/bin/env bash
# audit-zip.sh — offline audit of a LineageOS OTA zip.
#
#     ./audit-zip.sh <lineage-*.zip>
#
# Answers, without flashing anything:
#   1. Is this a full OTA or an incremental delta? (is there a full system img?)
#   2. Which radio firmware blobs does the payload reference?
#   3. Which init rc files reference radios, and which services do they start?
#
# Limitation you must understand: a non-A/B LineageOS OTA ships
# system.new.dat.br + system.transfer.list, NOT a listable filesystem. To audit
# file *contents* you must reconstruct the image with tools/sdat2img.py first.
# This script does the cheap, reliable part (metadata + boot.img + payload
# structure) and tells you exactly what to reconstruct if you need more.

set -euo pipefail
zip=${1:?usage: audit-zip.sh <lineage-*.zip>}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "===================================================================="
echo "AUDIT: $(basename "$zip")"
echo "===================================================================="
echo
echo "== 1. payload layout =="
unzip -l "$zip" | sed -n '4,$p' | awk '{printf "   %12s  %s\n", $1, $4}'
echo
if unzip -l "$zip" | grep -q 'system.new.dat.br'; then
  has_patch=$(unzip -l "$zip" | grep -c 'system.patch.dat' || true)
  echo "   -> non-A/B block OTA (system.new.dat.br + transfer.list)"
  if [ "$has_patch" != "0" ]; then
    sz=$(unzip -l "$zip" | awk '/system.patch.dat/{print $1}')
    if [ "${sz:-0}" -gt 0 ]; then
      echo "   -> WARNING: non-empty system.patch.dat. This is an INCREMENTAL"
      echo "      update. It cannot be audited alone: it needs the currently"
      echo "      installed system.img as the base. Use tools/sdat2img.py with"
      echo "      both dat files, or audit the full zip instead."
    else
      echo "   -> 0-byte patch: this IS a full payload, safe to reconstruct."
    fi
  fi
else
  echo "   -> full images present; extract and audit directly."
fi
echo

echo "== 2. boot.img header =="
unzip -o -q "$zip" 'boot.img' -d "$work" 2>/dev/null || true
if [ -f "$work/boot.img" ]; then
python3 - "$work/boot.img" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(4096)
if d[:8] != b'ANDROID!':
    print("   not an Android boot image (magic=%r)" % d[:8]); raise SystemExit
f = struct.unpack('<10I', d[8:48])
names = ['kernel_size','kernel_addr','ramdisk_size','ramdisk_addr','second_size',
         'second_addr','tags_addr','page_size','dt_size','unused']
for n, v in zip(names, f):
    print("   %-14s %d (0x%x)" % (n, v, v))
PY
  # The kernel is page-aligned right after the header. Strings in it confirm
  # which drivers were compiled in, independently of /proc/config.gz.
  ksize=$(python3 -c "
import struct;d=open('$work/boot.img','rb').read(48);print(struct.unpack('<10I',d[8:48])[0])")
  psz=$(python3 -c "
import struct;d=open('$work/boot.img','rb').read(48);print(struct.unpack('<10I',d[8:48])[7])")
  dd if="$work/boot.img" of="$work/kernel.bin" bs=2048 skip=1 count=$((ksize/2048+1)) 2>/dev/null || true
  echo "   kernel image extracted ($ksize bytes)"
  if python3 "$(dirname "$0")/../standalone/tools/extract-vmlinux.py" "$work/boot.img" "$work/vmlinux.bin" 2>&1 \
       | sed 's/^/   | /' | grep -q "payload at offset"; then
    echo "   radio driver strings found in the DECOMPRESSED kernel:"
    kstr=$(strings -a "$work/vmlinux.bin" 2>/dev/null || true)
    for s in cfg80211 mac80211 bcmdhd bcmdhd_wlan bcm4339 wlan0 wiphy \
             bcm2079x bnep rfkill msm-pil pil_modem rmnet smd_qmi qcom,smd hci_uart; do
      if printf '%s' "$kstr" | grep -qi -- "$s"; then
        printf "      %-18s PRESENT\n" "$s"
      fi
    done
  else
    echo "   kernel is compressed; see extract-vmlinux.py output for the exact"
    echo "   fallback. Simplest exact source, if you have any device:"
    echo "       adb shell cat /proc/config.gz | gzip -d"
  fi
else
  echo "   no boot.img in this zip"
fi
echo

echo "== 3. payload: which radio firmware is inside? =="
# A .br payload is compressed, so a direct string search is unreliable. Instead
# record what to look for once reconstructed, and note the sizes.
echo "   (cannot string-scan a brotli payload; reconstruct with tools/sdat2img.py)"
cat <<'EOF'
   After reconstruction, check for these on the running device or image:
     fw_bcmdhd.bin, fw_bcmdhd_apsta.bin      Broadcom Wi-Fi firmware
     bcmdhd.cal                              factory NVRAM (MAC address, cal)
     bcm4335c0.hcd                          Broadcom Bluetooth firmware
     bcm2079x-b5_firmware.ncd                NFC firmware
     libril-qc-qmi-1.so, libqmi*, qmuxd      CAF radio stack
     libbt-vendor, bluetooth_loader         Bluetooth transport
EOF
echo

echo "== 4. quickest regression gate for CI =="
cat <<'EOF'
   After you build your air-gapped ROM, run this against the ZIP:

     unzip -l out/target/product/<dev>/*userdebug.zip \
       | grep -iE 'fw_bcmdhd|bcmdhd\.cal|bcm4335c0\.hcd|bcm2079x|wpa_supplicant|hostapd|wificond'

   For a non-A/B LineageOS zip this shows only boot/recovery, so instead audit
   the build outputs directly:

     ls out/target/product/<dev>/vendor/etc/init/ | grep -iE 'wlan|wifi|bluetooth|radio|nfc'

   And for the kernel, the definitive check:

     python3 standalone/tools/apply-defconfig.py \
       kernel/<vendor>/<board>/arch/arm64/configs/<defconfig> \
       standalone/tools/profiles/<board>.list -i --check --kernel-root kernel/<vendor>/<board>
EOF
echo
echo "===================================================================="
echo "To audit file contents offline, reconstruct the image first:"
echo "  unzip -o $zip 'system.new.dat.br' 'system.transfer.list' -d work/ota"
echo "  python3 -c \"import brotli;d=brotli.Decompressor();\\"
echo "    f=open('work/ota/system.new.dat.br','rb');o=open('work/ota/system.new.dat','wb');\\"
echo "    [o.write(d.process(c)) for c in iter(lambda: f.read(1<<22), b'')]\""
echo "  python3 tools/sdat2img.py work/system.transfer.list \\"
echo "    work/ota/system.new.dat work/system.img"
echo "  debugfs -R 'ls -l /system/vendor/firmware' work/system.img"
echo "===================================================================="