#!/system/bin/sh
# Phase 0, part 2 -- the captures that need a human.
#
# Run this, THEN do the thing it asks for. It loops and appends, so you can
# leave it running while you tap a tag / tap the screen.
#
#   adb push phase0-live.sh /data/local/tmp/ && adb shell 'su -c "sh /data/local/tmp/phase0-live.sh"'
#
# KNOWN LIMITATION: this cannot capture an APDU exchange. There is no shell
# path to IsoDep on this ROM -- `cmd nfc` only exposes `status` (UWB), and the
# binder `nfc` service is only INfcAdapter (enable/disable). Driving a real
# transceive needs an app that calls IsoDep.connect() + transceive(), which is
# what kruxmobileapp's NfcProvider.java already does. See b/LOG.md.
OUT=/data/local/tmp/phase0
mkdir -p "$OUT"
chmod 755 /data/local/tmp 2>/dev/null

echo "=== LIVE CAPTURE -- leave this running ==="
echo
echo "  [1] Hold an NFC tag on the BACK (mid-back, the coil is there)."
echo "      Last run: 8 ISO-DEP tag detections. Tech was"
echo "        [android.nfc.tech.IsoDep, android.nfc.tech.NfcA]"
echo "      which is exactly what SeedKeeper needs. Keep the tag still for"
echo "      several seconds so the presence check has time to fire."
echo
echo "  [2] Tap the screen once, slowly, while this is still running."
echo
echo "  Stop with Ctrl-C. Results land in $OUT/live-*"
echo

# ---- touch: getevent, not a dd|od pipeline.
# The dd pipeline produced a 0-byte file on the last run. getevent is present
# at /system/bin/getevent and decodes the 16-byte structs for us. Note this
# device's driver emits NO SYN_REPORT and no ABS_MT_SLOT per touch, so the
# output is a flat event stream, not frames.
getevent -lt /dev/input/event1 > "$OUT/live-touch.txt" 2>&1 &
TPID=$!

# ---- NFC: what the framework and the HAL say, continuously ----
logcat -b all > "$OUT/live-logcat.txt" 2>&1 &
LPID=$!

# ---- the HAL's own binary log ----
i=0
while [ $i -lt 9 ]; do
    cp /data/nfc/event_log.binpb "$OUT/live-nfc-event-$i.binpb" 2>/dev/null
    i=$((i + 1))
    sleep 7
done &
NPID=$!

sleep 64
kill $TPID $LPID $NPID 2>/dev/null
chmod 644 "$OUT"/live-* 2>/dev/null

echo
echo "=== captured ==="
ls -l "$OUT"/live-* 2>/dev/null
echo
echo "--- tag detections ---"
grep -c 'dispatchTag' "$OUT/live-logcat.txt" 2>/dev/null
grep -i 'dispatchTag' "$OUT/live-logcat.txt" 2>/dev/null | tail -5
echo
echo "--- touch events ---"
wc -l < "$OUT/live-touch.txt" 2>/dev/null
head -5 "$OUT/live-touch.txt" 2>/dev/null