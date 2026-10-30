#!/system/bin/sh
# Stop camd without matching this script's own command line.
for p in $(pidof camd 2>/dev/null); do
    kill "$p" 2>/dev/null
done
