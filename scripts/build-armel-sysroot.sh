#!/bin/bash
# build-armel-sysroot.sh -- assemble the armel cross sysroot from the
# vendored Debian packages.
#
# Not neede if using host toolchain
#
# The sysroot is the headers and the static libc that the armel cross
# toolchain compiles against.  It is not a build output in the sense of
# being ours: it is the .deb packages in vendor/toolchain.ubuntu, unpacked
# into one directory.  So it is rebuilt from them rather than carried
# around -- the packages are 21 MB, the unpacked tree is 71 MB, and the
# tree is what changes.
#
# The result is byte-for-byte the tree that the build expects; see
# TOOLCHAIN.md.
#
# Usage:
#   ./build-armel-sysroot.sh [DEST]     (default: /tmp/opencode/armel-sysroot)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST=${1:-/tmp/opencode/armel-sysroot}
DEBS="$HERE/../vendor/toolchain.ubuntu"

[ -d "$DEBS" ] || { echo "no packages in $DEBS" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "no dpkg-deb" >&2; exit 1; }

rm -rf "$DEST"
mkdir -p "$DEST"
for deb in "$DEBS"/*.deb; do
	dpkg-deb -x "$deb" "$DEST"
done
echo "  -> $DEST ($(du -sh "$DEST" | cut -f1))"
