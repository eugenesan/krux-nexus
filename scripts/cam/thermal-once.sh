#!/system/bin/sh
# thermal-once.sh -- print one thermal/CPU sample as a single line.
#
# Runs on recovery (root) and Android (root via su).  Values are printed
# raw exactly as the kernel exposes them, because the zones disagree on
# units: the tsens zones report degrees Celsius, the PMIC zones report
# millidegrees, and some sensors report -40 when absent.  The parser
# (tools/cam-evidence.py) normalises them.
#
# Output:  up=<seconds> <zone>=<temp> ... load=<loadavg1> camd=<n>

up=$(cut -d' ' -f1 /proc/uptime 2>/dev/null)
printf 'up=%s' "$up"
for z in /sys/class/thermal/thermal_zone*; do
    t=$(cat "$z/type" 2>/dev/null) || continue
    v=$(cat "$z/temp" 2>/dev/null) || continue
    printf ' %s=%s' "$t" "$v"
done
printf ' load=%s' "$(cut -d' ' -f1 /proc/loadavg 2>/dev/null)"
printf ' camd=%s' "$(pidof camd 2>/dev/null | wc -w)"
printf '\n'
