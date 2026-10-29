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
MICROPYTHON=${MICROPYTHON:-$HERE/../rootfs/micropython-arm32}
CAMD=${CAMD:-/tmp/opencode/camd}
KRUXSRC="$HERE/../krux/src"
RESTART=${DEPLOY_RESTART:-1}

[ -f "$MICROPYTHON" ] || {
	echo "no built interpreter at $MICROPYTHON -- build it first" >&2
	exit 1
}

echo "== the target directories"
adb shell 'mkdir -p /data/krux/krux /data/krux/krux/pages /data/krux/krux/printers /data/krux/krux/translations /data/krux/krux/touchscreens /data/krux/flash /data/krux/embit/descriptor /data/krux/embit/util /data/krux/embit/wordlists' >/dev/null

echo "== the interpreter"
adb push "$MICROPYTHON" /data/krux/micropython

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

if [ -f "$CAMD" ]; then
	echo "== camd daemon"
	adb push "$CAMD" /data/krux/camd >/dev/null
	adb shell 'chmod +x /data/krux/camd' >/dev/null
fi

CPPFW="$HERE/../cam/fw/cpp_firmware_v1_2_0.fw"
if [ -f "$CPPFW" ]; then
	echo "== cpp firmware"
	adb push "$CPPFW" /data/krux/cpp_firmware_v1_2_0.fw >/dev/null
fi
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
	adb shell 'mkdir -p /data/krux/embit /data/krux/embit/descriptor /data/krux/embit/util  /data/krux/embit/wordlists' >/dev/null
	for f in "$EMBIT"/*.py; do adb push "$f" /data/krux/embit/ >/dev/null; done
	for f in "$EMBIT"/descriptor/*.py; do adb push "$f" /data/krux/embit/descriptor/ >/dev/null; done
	for f in "$EMBIT"/util/*.py; do adb push "$f" /data/krux/embit/util/ >/dev/null; done
	for f in "$EMBIT"/wordlists/*.py; do adb push "$f" /data/krux/embit/wordlists/ >/dev/null; done
fi

adb shell 'rm -rf /data/krux/krux/__pycache__ /data/krux/krux/*/__pycache__ \
	/data/krux/__pycache__ /data/krux/embit/__pycache__ /data/krux/embit/descriptor/__pycache__ \
	/data/krux/embit/util/__pycache__ /data/krux/embit/wordlists/__pycache__'

if [ "$RESTART" = "1" ]; then
	echo "== restarting the firmware"
	adb shell 'setprop ctl.stop krux; sleep 4; setprop ctl.start krux; sleep 18; cat /data/krux/fw.log'
fi
