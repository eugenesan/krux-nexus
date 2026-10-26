#!/bin/bash
# Deploy the MicroPython firmware to the hammerhead and restart it.
#
# WHAT GOES WHERE
# ---------------
#
#   /data/krux/micropython     the interpreter (built by
#                               kruxport/build-micropython.sh)
#   /data/krux/krux/*.py       the Krux package: the upstream tree
#                               (vendor/krux/src/krux) with the port's own
#                               modules put in its place
#   /data/krux/*.py            the port's modules that are not part of
#                               the Krux package: board, hmac, nfc, pmu,
#                               secp256k1, sensor, uhashlib_hw
#
# A port module replaces an upstream module when the upstream package
# has a file of the same name; otherwise it is one of the extra
# modules and goes to the top level.  That rule is applied here, so
# the device always gets what the port means.
#
#   DEPLOY_RESTART=0   push the files but do not restart the firmware
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="$HERE/../kruxport"
KRUX="$HERE/../vendor/krux/src/krux"
RESTART=${DEPLOY_RESTART:-1}

[ -f "$PORT/micropython-arm32" ] || {
	echo "no built interpreter at $PORT/micropython-arm32 -- build it first" >&2
	exit 1
}

echo "== the interpreter"
adb push "$PORT/micropython-arm32" /data/krux/micropython

echo "== the Krux package"
for f in "$KRUX"/*.py; do
	adb push "$f" /data/krux/krux/ >/dev/null
done
for d in pages printers translations touchscreens; do
	[ -d "$KRUX/$d" ] && adb push "$KRUX/$d" /data/krux/krux/ >/dev/null
done

echo "== the port's own modules"
for f in "$PORT/kruxport"/*.py; do
	name=$(basename "$f")
	[ "$name" = "__init__.py" ] && continue
	if [ "$name" = "flash.py" ]; then
		# The firmware imports "flash" (the K210's SPI flash, which
		# the wipe and the flash tools use).  The settings live in a
		# DIRECTORY called "flash", and a directory wins over a module
		# of the same name -- so the port's module has to be the
		# package's __init__.py, inside it.
		adb shell 'mkdir -p /data/krux/flash'
		adb push "$f" "/data/krux/flash/__init__.py" >/dev/null
		# the settings directory holds the secrets, so the module in
		# it is not world writable
		adb shell 'chmod 600 /data/krux/flash/__init__.py' >/dev/null
		echo "   $name -> /data/krux/flash/__init__.py"
	elif [ -f "$KRUX/$name" ]; then
		adb push "$f" "/data/krux/krux/$name" >/dev/null
	else
		adb push "$f" "/data/krux/$name" >/dev/null
	fi
	echo "   $name"
done

adb shell 'rm -rf /data/krux/krux/__pycache__ /data/krux/__pycache__'

if [ "$RESTART" = "1" ]; then
	echo "== restarting the firmware"
	adb shell 'setprop ctl.stop krux; sleep 4; setprop ctl.start krux; sleep 18; cat /data/krux/fw.log'
fi
