#!/bin/bash
# Air-gap verification: check interfaces, camd, sockets
set -e
ADB=${ADB:-adb}
echo "=== interfaces ==="
$ADB shell 'cat /proc/net/dev'
echo "=== camd running? ==="
$ADB shell 'ps | grep camd | grep -v grep || echo "camd not running (good)"'
echo "=== listening sockets ==="
$ADB shell 'cat /proc/net/tcp'
echo "=== modules ==="
$ADB shell 'lsmod 2>/dev/null | head'
echo "=== done ==="