#!/system/bin/sh
# thermal-sampler.sh -- persistent thermal/CPU sampler for crash analysis.
#
# Usage: thermal-sampler.sh OUT [INTERVAL]
#
# Writes a header naming the thermal zones, then one CSV-like line every
# INTERVAL seconds (default 2):
#
#   t_unix up_s <zone values in header order> load1 camd_count
#
# Every sample is followed by sync(1): a watchdog reset does not flush
# the page cache, so without it the last samples (the interesting ones)
# would be lost.  The sampler is meant to be started with setsid so it
# survives the adb shell that launched it.

OUT="$1"
INTERVAL="${2:-2}"
[ -n "$OUT" ] || { echo "usage: $0 OUT [interval]" >&2; exit 2; }

mkdir -p "$(dirname "$OUT")" || exit 1

{
    printf 't_unix up_s'
    for z in /sys/class/thermal/thermal_zone*; do
        printf ' %s' "$(cat "$z/type" 2>/dev/null)"
    done
    printf ' load1 camd\n'
} > "$OUT"

while :; do
    {
        t=$(date +%s)
        up=$(cut -d' ' -f1 /proc/uptime)
        printf '%s %s' "$t" "$up"
        for z in /sys/class/thermal/thermal_zone*; do
            printf ' %s' "$(cat "$z/temp" 2>/dev/null)"
        done
        printf ' %s %s\n' \
            "$(cut -d' ' -f1 /proc/loadavg 2>/dev/null)" \
            "$(pidof camd 2>/dev/null | wc -w)"
    } >> "$OUT"
    sync
    sleep "$INTERVAL"
done
