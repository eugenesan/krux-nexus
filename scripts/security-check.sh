#!/usr/bin/env bash
# The release gate.  Fails if a debug/storage capability is still in the
# release artifacts or the image.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:-$ROOT/artifacts/built/boot-standard-release.img}"
CAMD="$ROOT/cam/camd-release"
PORT="$ROOT/rootfs/micropython-arm32-release"
STUB="$ROOT/rootfs/stub/krux-init"
fail=0

say() { echo "security-check: $*"; }
bad() { echo "  !! $*" >&2; fail=1; }

[ -x "$CAMD" ] || bad "no release camd at $CAMD"
[ -x "$PORT" ] || bad "no release port at $PORT"

for s in scan.raw scan.meta last-qr CAMD_DUMP_RAW; do
	strings "$CAMD" 2>/dev/null | grep -q "$s" && bad "camd still has $s"
done
strings "$PORT" 2>/dev/null | grep -q "save_ppm" && bad "the port still exports save_ppm"
strings "$STUB" 2>/dev/null | grep -q "/data/krux/fw.log" && bad "the stub still logs to /data"

grep -rqE 'open\([^)]*(last-qr|scan\.raw|scan_full\.raw)' "$ROOT/krux/src" 2>/dev/null && \
	bad "the Krux source still writes a camera frame"

if [ -f "$IMG" ]; then
	for s in last-qr scan.raw scan.meta save_ppm CAMD_DUMP_RAW; do
		strings "$IMG" 2>/dev/null | grep -q "$s" && bad "the image has $s"
	done
	# the release sockets must not be world-writable in the source
	grep -q 'chmod("/data/krux/camd.sock", 0666)' "$ROOT/cam/camd.c" && \
		bad "the camd socket is world-writable"
fi

if [ "$fail" -eq 0 ]; then
	say "PASS"
else
	say "FAIL"
	exit 1
fi
