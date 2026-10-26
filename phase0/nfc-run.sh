#!/system/bin/sh
# nfc-run.sh -- run the /dev/bcm2079x probe with Android's NFC HAL frozen.
#
#   adb push nfc-probe nfc-run.sh /data/local/tmp/
#   adb shell 'su -c "sh /data/local/tmp/nfc-run.sh"'
#
# WHY SIGSTOP AND NOT ctl.stop
#   Measured 2026-10-18: `setprop ctl.stop vendor.nfc_hal_service` kills the
#   process, and init restarts it within ~1 second (pid 7820 -> 7905).
#   init.svc.vendor.nfc_hal_service reads "running" before, during and after --
#   there is no window in which to grab the chip that way, and no reliable way
#   to tell from the property that the stop did nothing.
#
#   SIGSTOP freezes the process in place: State: T. init is not notified, does
#   not restart it, and the fd stays open (which is fine -- our open() already
#   succeeded alongside it). It simply stops doing I/O. SIGCONT resumes.
#   This is fully reversible and does not disturb init's supervision.
#
# HONESTY NOTE
#   The first run of this script reported "0 packets", and that result was
#   INVALID: the HAL had never stopped, it was polling the chip the whole time,
#   and it starved our reader. The script reported the failure to stop and
#   continued anyway. That is the silent-success failure mode this project has
#   hit seven times in the installer already, so this version ABORTS if it
#   cannot prove the chip is quiet.

BIN=/data/local/tmp/nfc-probe
OUT=/data/local/tmp/nfcprobe
HAL=android.hardware.nfc@1.0-service

mkdir -p "$OUT"
frozen=no
HALPID=""

[ -x "$BIN" ] || { echo "!! $BIN missing/not executable"; exit 1; }
[ "$(id -u)" = "0" ] || { echo "!! run as root: adb shell 'su -c \"sh $0\"'"; exit 1; }

unfreeze() {
    if [ "$frozen" = yes ] && [ -n "$HALPID" ]; then
        echo
        echo "== resuming $HAL (was frozen) =="
        kill -CONT "$HALPID" 2>/dev/null
        sleep 2
        st=$(grep '^State' "/proc/$HALPID/status" 2>/dev/null | awk '{print $2}')
        if [ "$st" = "T" ] || [ "$st" = "t" ]; then
            echo "   !! still stopped (state $st). NFC is frozen."
            echo "      Run:  kill -CONT $(pidof $HAL)"
        else
            echo "   $HAL is running again (state $st) -- NFC is fine."
        fi
    elif [ -n "$HALPID" ]; then
        echo
        echo "== $HAL was never frozen; left alone =="
    else
        echo
        echo "== $HAL was not running; nothing to restore =="
    fi
}
trap unfreeze 0 INT TERM

state_of() { grep '^State' "/proc/$1/status" 2>/dev/null | awk '{print $2}'; }

echo "== freezing the NFC HAL =="
HALPID=$(pidof "$HAL")
if [ -z "$HALPID" ]; then
    echo "   $HAL not running -- chip is already free, carrying on"
else
    echo "   $HAL pid=$HALPID state=$(state_of "$HALPID")"
    kill -STOP "$HALPID"
    sleep 1
    ST=$(state_of "$HALPID")
    echo "   after SIGSTOP: state=$ST"
    if [ "$ST" = "T" ] || [ "$ST" = "t" ]; then
        frozen=yes
    else
        echo "   !! SIGSTOP did not take (state=$ST)."
        echo "      Cannot get the chip to ourselves. Aborting rather than"
        echo "      producing a result that would be meaningless."
        echo "      Alternative: run the probe from TWRP, where no Android runs."
        exit 1
    fi
fi
echo

{
    echo "=== date ==="; date
    echo "=== kernel ==="; uname -a
    echo "=== nfc service prop (may say running even while frozen) ==="
    getprop init.svc.vendor.nfc_hal_service
    echo "=== HAL frozen? ==="
    echo "  pid $HALPID state $(state_of "$HALPID")"
    echo "=== does the frozen HAL still hold /dev/bcm2079x? ==="
    ls -l "/proc/$HALPID/fd" 2>/dev/null | grep -c bcm2079x
    echo "  (1 = yes, 0 = no; either is fine, our open() is independent)"
} > "$OUT/preflight.txt" 2>&1
cat "$OUT/preflight.txt"
echo
echo "==============================================================="
echo " STAGE 1: open the device, change nothing"
echo "==============================================================="
"$BIN" --info 2>&1 | tee "$OUT/1-info.txt"

echo
echo "==============================================================="
echo " STAGE 2: power/wake ioctl reachability"
echo "==============================================================="
"$BIN" --power 2>&1 | tee "$OUT/2-power.txt"

echo
echo "==============================================================="
echo " STAGE 3: listen 30s, chip powered, nothing sent by us"
echo "==============================================================="
echo "Hold an NFC tag on the BACK for part of this window."
echo
"$BIN" --listen 30 2>&1 | tee "$OUT/3-listen.txt"

echo
echo "==============================================================="
echo " STAGE 4: sanity after the HAL comes back"
echo "==============================================================="
{
    echo "=== nfc.initialized ==="; getprop nfc.initialized
    echo "=== HAL state ==="; echo "  $HALPID $(state_of "$HALPID")"
    echo "=== recent nfc log ==="
    logcat -b all -d 2>/dev/null | grep -iE 'NfcNciHal|NfcService|NfcDispatcher' | tail -25
} > "$OUT/postflight.txt" 2>&1
cat "$OUT/postflight.txt"

echo
echo "== results in $OUT =="
ls -l "$OUT" 2>/dev/null
echo
echo "Pull them (adb does NOT glob -- use a loop):"
echo '  for f in preflight.txt 1-info.txt 2-power.txt 3-listen.txt postflight.txt; do adb pull /data/local/tmp/nfcprobe/$f b/phase0/; done'
