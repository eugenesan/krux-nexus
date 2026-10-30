#!/usr/bin/env bash
# ae-thermal-test.sh -- instrumented camera / auto-exposure stability test.
#
# Purpose: answer "is the device crashing because of heat, or because our
# software faults?" by recording thermal zones and kernel logs at every
# stage:
#
#   1. after the Android boot that follows a crash (and after a cooldown)
#   2. after the camera kernel has booted
#   3. continuously (every 2 s, synced to /data) while camd runs
#   4. after the run, including /proc/last_kmsg and the reboot reason
#
# The camera kernel is booted with `fastboot boot` only (never flashed).
#
# Usage:
#   scripts/cam/ae-thermal-test.sh status
#   scripts/cam/ae-thermal-test.sh boot
#   scripts/cam/ae-thermal-test.sh cooldown [SECONDS]
#   scripts/cam/ae-thermal-test.sh run LABEL [TEST_PY|-] [HOLD_S] [CAMD_BIN]
#   scripts/cam/ae-thermal-test.sh all LABEL [TEST_PY|-] [HOLD_S] [CAMD_BIN]
#   scripts/cam/ae-thermal-test.sh idle LABEL [SECONDS]
#   scripts/cam/ae-thermal-test.sh collect LABEL
#   scripts/cam/ae-thermal-test.sh analyze LABEL
#
# `run` assumes the camera kernel has just been booted (or is still fresh);
# use `all` for an atomic boot+run cycle.  Reusing a boot for several camd
# runs can leave the CPP queue dirty
# (msm_cpp_delete_buff_queue: Queue not free), after which the next run's
# frames time out and the timeout handler floods the console.
#
# Environment:
#   BOOT_IMG       camera boot image (default /tmp/opencode/boot-ksucam.img)
#   COOLDOWN       seconds to cool after an Android boot (default 300)
#   SAMPLE_EVERY   host sample interval during cooldown (default 30)
#   HOST_EVID      host evidence directory (default /tmp/opencode/cam-evidence)
#
# Examples:
#   # Full cycle: cool 5 min, boot camera kernel, run the AE test, collect.
#   scripts/cam/ae-thermal-test.sh all ae-bright test_brightness3.py 20
#
#   # Streaming only, no sensor writes (isolates the AE patch):
#   scripts/cam/ae-thermal-test.sh all stream-only - 30
#
#   # Idle control: camera kernel, no camd at all, 10 minutes:
#   scripts/cam/ae-thermal-test.sh idle idle-10m 600
set -u

cd "$(dirname "$0")/../.." || exit 1

BOOT_IMG="${BOOT_IMG:-/tmp/opencode/boot-ksucam.img}"
COOLDOWN="${COOLDOWN:-300}"
SAMPLE_EVERY="${SAMPLE_EVERY:-30}"
HOST_EVID="${HOST_EVID:-/tmp/opencode/cam-evidence}"
DEV_EVID=/data/krux/evidence
TMP=/data/local/tmp

say() { printf '%s %s\n' "$(date '+%H:%M:%S')" "$*"; }

dev_uid() {
    adb shell id -u 2>/dev/null | tr -d '\r'
}

have_dev() {
    adb devices 2>/dev/null | awk 'NR > 1 && $2 != "" { found = 1 } END { exit !found }'
}

wait_dev() {
    local t="${1:-150}" i=0
    while [ "$i" -lt "$t" ]; do
        have_dev && return 0
        sleep 2
        i=$((i + 2))
    done
    return 1
}

is_android() {
    [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]
}

# Run a simple command as root.  In recovery adbd is already root; on
# Android we go through KernelSU's su.  The command must not contain
# single quotes.
root_run() {
    local cmd="$1"
    if [ "$(dev_uid)" = "0" ]; then
        adb shell "$cmd"
    else
        adb shell "su -c '$cmd'"
    fi
}

root_cat() {
    local path="$1"
    if [ "$(dev_uid)" = "0" ]; then
        adb exec-out cat "$path"
    else
        adb exec-out su -c "cat $path"
    fi
}

push_scripts() {
    for f in thermal-once.sh thermal-sampler.sh cam-supervisor.sh kill-samplers.sh stop-camd.sh; do
        adb push "scripts/cam/$f" "$TMP/$f" >/dev/null 2>&1 || {
            say "push failed: $f"
            return 1
        }
    done
    root_run "chmod 755 $TMP/thermal-once.sh $TMP/thermal-sampler.sh $TMP/cam-supervisor.sh $TMP/kill-samplers.sh $TMP/stop-camd.sh"
}

SCRIPTS_READY=0
ensure_scripts() {
    [ "$SCRIPTS_READY" = "1" ] && return 0
    push_scripts && SCRIPTS_READY=1
}

sample() {
    root_run "sh $TMP/thermal-once.sh"
}

# Normalise a thermal value the way tools/cam-evidence.py does.
norm_temp() {
    echo "$1" | awk '{
        v = $1 + 0
        if (v == -40) { print ""; exit }
        if (v > 1000 || v < -1000) v = v / 1000.0
        printf "%.1f\n", v
    }'
}

sample_pretty() {
    local line zone val nv max maxzone=""
    ensure_scripts || true
    line="$(sample 2>/dev/null)"
    [ -n "$line" ] || { say "no sample (device gone?)"; return 1; }
    max=""
    for kv in $line; do
        case "$kv" in
        up=* | load=* | camd=*) continue ;;
        *=*)
            zone="${kv%%=*}"
            val="${kv#*=}"
            nv="$(norm_temp "$val")"
            [ -n "$nv" ] || continue
            if [ -z "$max" ] || awk -v a="$nv" -v b="$max" 'BEGIN { exit !(a > b) }'; then
                max="$nv"
                maxzone="$zone"
            fi
            ;;
        esac
    done
    say "thermal: max=${max}C ($maxzone) | $(echo "$line" | tr ' ' '\n' | grep -E '^(tsens_tz_sensor6|pm8841_tz|pm8941_tz|up|load|camd)=' | tr '\n' ' ')"
    printf '%s\n' "$line"
}

cooldown() {
    local secs="${1:-$COOLDOWN}" n=0 out
    out="$HOST_EVID/cooldown-$(date +%Y%m%d-%H%M%S).log"
    mkdir -p "$HOST_EVID"
    say "cooldown ${secs}s (sampling every ${SAMPLE_EVERY}s)"
    while [ "$n" -le "$secs" ]; do
        sample_pretty >>"$out"
        sleep "$SAMPLE_EVERY"
        n=$((n + SAMPLE_EVERY))
    done
    say "cooldown done: $out"
}

boot_cam() {
    say "boot: $BOOT_IMG"
    adb reboot bootloader >/dev/null 2>&1 || true
    sleep 6
    if ! fastboot devices | grep -q .; then
        say "bootloader not visible"
        return 1
    fi
    fastboot boot "$BOOT_IMG" || return 1
    wait_dev 180 || { say "adb did not come back"; return 1; }
    # adbd/USB re-enumerate ~10-15 s after the ramdisk starts; a shell
    # opened in that window gets disconnected and takes its children with
    # it.  Wait for the bus to settle before trusting a shell.
    sleep 10
    wait_dev 60 || { say "adb went away while settling"; return 1; }
    say "camera kernel up: $(adb shell cat /proc/version 2>/dev/null | head -1 | tr -d '\r')"
    return 0
}

wait_android_ready() {
    local t="${1:-360}" i=0
    wait_dev "$t" || return 1
    i=0
    while [ "$i" -lt 300 ]; do
        if is_android; then
            return 0
        fi
        sleep 3
        i=$((i + 3))
    done
    return 1
}

collect() {
    local label="$1" keep_kmsg="${2:-yes}"
    local dev="$DEV_EVID/$label"
    local host="$HOST_EVID/$label"
    mkdir -p "$host"
    say "collect: $dev -> $host"
    if [ "$keep_kmsg" = "yes" ]; then
        root_cat /proc/last_kmsg >"$host/last_kmsg.txt" 2>/dev/null || true
    fi
    root_cat /proc/cmdline >"$host/cmdline.txt" 2>/dev/null || true
    root_cat /proc/uptime >"$host/uptime.txt" 2>/dev/null || true
    adb shell getprop ro.boot.bootreason 2>/dev/null | tr -d '\r' >"$host/bootreason.txt" || true
    root_run "dmesg" >"$host/dmesg-current.txt" 2>/dev/null || true
    for f in thermal.log supervisor.log camd.log test.log console.txt dmesg-frame.txt dmesg-end.txt DONE; do
        root_cat "$dev/$f" >"$host/$f" 2>/dev/null || true
    done
    sample_pretty >"$host/thermal-after.txt" 2>/dev/null || true
    say "collected: $(ls -1 "$host" | tr '\n' ' ')"
}

analyze() {
    local label="$1"
    python3 tools/cam-evidence.py "$HOST_EVID/$label"
}

run() {
    local label="$1" test="${2:--}" hold="${3:-20}" camd="${4:-/data/krux/camd}"
    local dev="$DEV_EVID/$label"
    local host="$HOST_EVID/$label"
    local frame_wait=30
    mkdir -p "$host"

    if [ "$(dev_uid)" != "0" ]; then
        say "warning: adb shell is not root; camd may not start"
    fi
    push_scripts || return 1

    say "run '$label': test=$test hold=${hold}s camd=$camd"
    root_run "sh $TMP/stop-camd.sh" >/dev/null 2>&1 || true
    root_run "sh $TMP/kill-samplers.sh" >/dev/null 2>&1 || true
    root_run "rm -rf $dev && mkdir -p $dev"
    root_run "setsid sh $TMP/thermal-sampler.sh $dev/thermal.log 2 >/dev/null 2>&1 &"
    sleep 2
    root_run "sh $TMP/thermal-once.sh" >"$host/thermal-before.txt" 2>/dev/null

    local up_before
    up_before="$(root_cat /proc/uptime 2>/dev/null | awk '{print int($1)}')"
    say "uptime before run: ${up_before}s"

    # Start the supervisor detached, then poll.  Polling is what makes the
    # crash detectable: a foreground adb shell can also die when adbd
    # re-enumerates, which says nothing about the kernel.
    root_run "setsid sh $TMP/cam-supervisor.sh $dev $test $frame_wait $hold $camd > $dev/console.txt 2>&1 &"
    sleep 2

    local deadline t=0 done_marker="" up_after=""
    if [ "$test" = "-" ]; then
        deadline=$((hold + 90))
    else
        deadline=$((frame_wait + 120))
    fi
    while [ "$t" -lt "$deadline" ]; do
        sleep 2
        t=$((t + 2))
        if ! have_dev; then
            say "device went away after ${t}s (crash)"
            break
        fi
        done_marker="$(root_run "test -f $dev/DONE && echo yes" 2>/dev/null | tr -d '\r')"
        [ "$done_marker" = "yes" ] && break
    done

    if have_dev; then
        up_after="$(root_cat /proc/uptime 2>/dev/null | awk '{print int($1)}')"
    fi

    if [ "$done_marker" = "yes" ] && [ -n "$up_after" ] && [ -n "$up_before" ] &&
        [ "$up_after" -ge "$up_before" ]; then
        say "run finished without a reboot (uptime ${up_before}s -> ${up_after}s)"
        collect "$label" no-kmsg
        echo "run completed without a reboot (uptime ${up_before}s -> ${up_after}s)" \
            >"$host/run-ok.txt"
        return 0
    fi

    if have_dev && [ -n "$up_after" ] && [ -n "$up_before" ] &&
        [ "$up_after" -ge "$up_before" ]; then
        say "run did not finish (timeout, no DONE) but the device is still up"
        root_run "sh $TMP/stop-camd.sh" >/dev/null 2>&1 || true
        collect "$label" no-kmsg
        return 3
    fi

    say "no DONE marker / uptime reset -- treating as crash (uptime ${up_before}s -> ${up_after:-gone})"
    if ! wait_android_ready 420; then
        say "device did not return to Android"
        return 1
    fi
    say "Android is back; collecting crash evidence before anything else"
    collect "$label"
    analyze "$label" || true
    return 2
}

idle() {
    local label="$1" secs="${2:-600}"
    local dev="$DEV_EVID/$label"
    local host="$HOST_EVID/$label"
    mkdir -p "$host"
    push_scripts || return 1
    if [ "$(dev_uid)" != "0" ]; then
        say "idle control needs the camera (recovery) image; boot it first"
        return 1
    fi
    say "idle control '$label': ${secs}s with no camd"
    root_run "sh $TMP/stop-camd.sh" >/dev/null 2>&1 || true
    root_run "sh $TMP/kill-samplers.sh" >/dev/null 2>&1 || true
    root_run "rm -rf $dev && mkdir -p $dev"
    root_run "setsid sh $TMP/thermal-sampler.sh $dev/thermal.log 2 >/dev/null 2>&1 &"
    local n=0 rebooted=0
    while [ "$n" -lt "$secs" ]; do
        sleep 30
        n=$((n + 30))
        sample_pretty >>"$host/idle-samples.log" 2>/dev/null || true
        if ! have_dev; then
            say "device disappeared during idle control"
            rebooted=1
            break
        fi
        local up
        up="$(root_cat /proc/uptime 2>/dev/null | awk '{print int($1)}')"
        if [ -n "$up" ] && [ "$up" -lt "$n" ]; then
            say "uptime reset during idle control (device rebooted)"
            rebooted=1
            break
        fi
    done
    collect "$label"
    if [ "$rebooted" = 0 ]; then
        # last_kmsg here belongs to the previous boot, not to this idle
        # run; drop it so the analyzer does not misattribute it.
        rm -f "$host/last_kmsg.txt" "$host/cmdline.txt" "$host/bootreason.txt"
        echo "idle control completed without a reboot after ${secs}s" \
            >"$host/idle-ok.txt"
    fi
    analyze "$label" || true
}

status() {
    say "device: $(adb devices | awk 'NR>1 && $2!=""{print $1" "$2}' | tr '\n' ' ')"
    if have_dev; then
        say "uid=$(dev_uid) boot_completed=$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')"
        root_cat /proc/cmdline 2>/dev/null | tr ' ' '\n' | grep -E 'bootreason|androidboot.bootreason' || true
        sample_pretty
    fi
}

cmd="${1:-status}"
shift || true
case "$cmd" in
status) status ;;
boot) boot_cam && sample_pretty ;;
cooldown) cooldown "${1:-$COOLDOWN}" ;;
run) run "$@" ;;
collect) collect "${1:?label}" ;;
analyze) analyze "${1:?label}" ;;
idle) idle "$@" ;;
all)
    label="${1:?label}"
    test="${2:--}"
    hold="${3:-20}"
    camd="${4:-/data/krux/camd}"
    if have_dev && is_android; then
        cooldown "$COOLDOWN"
    fi
    boot_cam || exit 1
    mkdir -p "$HOST_EVID/$label"
    sample_pretty >"$HOST_EVID/$label/thermal-boot.txt" 2>/dev/null
    run "$label" "$test" "$hold" "$camd"
    rc=$?
    if have_dev && is_android; then
        cooldown "$COOLDOWN"
    fi
    exit "$rc"
    ;;
*)
    sed -n '2,45p' "$0"
    exit 2
    ;;
esac
