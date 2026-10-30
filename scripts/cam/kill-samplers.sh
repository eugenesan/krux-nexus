#!/system/bin/sh
# Kill only thermal sampler processes, matching their exact command line.
# A grep for "thermal-sampler" would also match the adb shell running this
# command (its -c argument contains the string), so match from the start.
for p in /proc/[0-9]*; do
    c=$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null)
    case "$c" in
    "sh /data/local/tmp/thermal-sampler.sh"* | \
    "/system/bin/sh /data/local/tmp/thermal-sampler.sh"* | \
    "setsid sh /data/local/tmp/thermal-sampler.sh"*)
        kill "${p##*/}" 2>/dev/null
        ;;
    esac
done
