#!/system/bin/sh
# cam-supervisor.sh -- device-side supervisor for one instrumented run.
#
# Usage: cam-supervisor.sh RUNDIR [TEST_PY|-] [FRAME_WAIT] [CAMD_BIN]
#
# Starts camd, waits (bounded) for the first frame, optionally runs a
# MicroPython test, snapshots dmesg at each landmark, then stops camd and
# writes RUNDIR/DONE.  If the kernel dies first, DONE is never written and
# the host treats the run as a crash -- which is exactly what we want.
#
# Landmarks are appended to RUNDIR/supervisor.log with the uptime, so the
# host can align them with the thermal sampler and with last_kmsg.

DIR="$1"
TEST="$2"
WAIT="${3:-30}"
HOLD="${4:-20}"
CAMD="${5:-/data/krux/camd}"
K=/data/krux

[ -n "$DIR" ] || { echo "usage: $0 RUNDIR [TEST_PY|-] [FRAME_WAIT] [HOLD_S] [CAMD_BIN]" >&2; exit 2; }
mkdir -p "$DIR" || exit 1
LOG="$DIR/supervisor.log"

mark() {
    echo "$(date +%s) up=$(cut -d' ' -f1 /proc/uptime) $*" >> "$LOG"
}

mark "start test=$TEST wait=$WAIT hold=$HOLD camd=$CAMD"
# Use pidof + kill, not pkill -f: the supervisor's own command line also
# contains $CAMD, so pkill -f would kill the supervisor itself.
for p in $(pidof camd 2>/dev/null); do
    kill "$p" 2>/dev/null
done
rm -f "$K/camd.sock"
sleep 1

cd "$K" || { mark "cd failed"; exit 1; }
"$CAMD" 1640 1232 0x40000000 rear 0 /data/krux/cpp_firmware_v1_2_0.fw 2 binned cfg nocycle 0 0x2b 0 0 300 0 0 0 0x12 0 1 0xffffffff 0 0 0 1 0 > "$DIR/camd.log" 2>&1 &
CPID=$!
mark "camd pid=$CPID"

i=0
frames=0
while [ "$i" -lt "$WAIT" ]; do
    frames=$(grep -c "GOT A FRAME" "$DIR/camd.log" 2>/dev/null)
    [ "$frames" -gt 0 ] && break
    sleep 1
    i=$((i + 1))
done
mark "frame_wait=$i frames=$frames"
dmesg > "$DIR/dmesg-frame.txt" 2>/dev/null

if [ "$TEST" != "-" ] && [ -f "$TEST" ]; then
    mark "test start $TEST"
    if command -v timeout >/dev/null 2>&1; then
        timeout 40 ./micropython "$TEST" > "$DIR/test.log" 2>&1
    else
        ./micropython "$TEST" > "$DIR/test.log" 2>&1
    fi
    mark "test rc=$?"
else
    mark "stream-only hold ${HOLD}s"
    sleep "$HOLD"
fi

dmesg > "$DIR/dmesg-end.txt" 2>/dev/null
kill "$CPID" 2>/dev/null
wait "$CPID" 2>/dev/null
mark "done"
echo done > "$DIR/DONE"
