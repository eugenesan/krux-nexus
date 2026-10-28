#!/bin/bash
# Deploy the MicroPython firmware to the hammerhead and restart it.
#
# The Krux submodule now carries the port (branch nexus-port): its
# src/krux/ package and src/*.py top-level modules are already the ported
# versions, so this script only has to push them.  No overlay step.
#
#   /data/krux/micropython     the interpreter
#   /data/krux/krux/*.py       the Krux package (ported)
#   /data/krux/*.py            the port's top-level modules
#   /data/krux/embit/          embit (pure python)
#   /data/krux/flash/          the flash module (package form)
#
#   DEPLOY_RESTART=0   push the files but do not restart the firmware
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
PORT="$HERE/../kruxport"
KRUXSRC="$HERE/../krux/src"
RESTART=${DEPLOY_RESTART:-1}

[ -f "$PORT/micropython-arm32" ] || {
	echo "no built interpreter at $PORT/micropython-arm32 -- build it first" >&2
	exit 1
}

echo "== the interpreter"
adb push "$PORT/micropython-arm32" /data/krux/micropython

echo "== the Krux package"
for f in "$KRUXSRC"/krux/*.py; do
	adb push "$f" /data/krux/krux/ >/dev/null
done
for d in pages printers translations touchscreens; do
	[ -d "$KRUXSRC/krux/$d" ] && adb push "$KRUXSRC/krux/$d" /data/krux/krux/ >/dev/null
done

echo "== the top-level port modules"
for f in "$KRUXSRC"/*.py; do
	adb push "$f" /data/krux/ >/dev/null
done
if [ -d "$KRUXSRC/flash" ]; then
	adb shell 'mkdir -p /data/krux/flash' >/dev/null
	for f in "$KRUXSRC"/flash/*; do
		adb push "$f" /data/krux/flash/ >/dev/null
	done
	adb shell 'chmod 600 /data/krux/flash/__init__.py' >/dev/null
fi

echo "== the embit package"
EMBIT="$HERE/../krux/vendor/embit/src/embit"
if [ -d "$EMBIT" ]; then
	adb shell 'mkdir -p /data/krux/embit /data/krux/embit/descriptor' >/dev/null
	for f in "$EMBIT"/*.py; do adb push "$f" /data/krux/embit/ >/dev/null; done
	for f in "$EMBIT"/descriptor/*.py; do adb push "$f" /data/krux/embit/descriptor/ >/dev/null; done
fi

adb shell 'rm -rf /data/krux/krux/__pycache__ /data/krux/__pycache__ /data/krux/embit/__pycache__'

if [ "$RESTART" = "1" ]; then
	echo "== restarting the firmware"
	adb shell 'setprop ctl.stop krux; sleep 4; setprop ctl.start krux; sleep 18; cat /data/krux/fw.log'
fi
