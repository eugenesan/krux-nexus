#!/bin/bash
# Push the known-good /data/krux working set to a device in the stub.
# WARNING: this removes /data/krux, including settings and seeds.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${SRC:-$HERE/../deploy/working-krux}
SERIAL=${1:-}
ADB="adb ${SERIAL:+-s $SERIAL}"
$ADB shell 'rm -rf /data/krux'
$ADB push "$SRC/." /data/krux/
$ADB shell 'find /data/krux -type d -exec chmod 777 {} +; find /data/krux -type f -exec chmod 666 {} +'
echo "deployed $SRC"
