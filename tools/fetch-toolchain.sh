#!/bin/bash
# fetch-toolchain.sh -- pull Arch's ARM bare-metal cross toolchain into the
# project, so the kernel can be built with the same compiler the reference
# kernels were built with.
#
# WHY
# ---
# All three working kernels report:
#
#   Linux version 3.4.113-... (gcc version 16.2.0 (GCC) )
#
# and Arch's `arm-none-eabi-gcc` is version 16.2.0. It is also the right
# *shape* for a kernel: a bare-metal target with no libc, whereas the AOSP
# `arm-linux-androideabi-4.9` prebuilt we have been using is a bionic
# toolchain shipped for userspace.
#
# `arm-none-eabi-binutils` is pulled alongside it because the kernel build
# needs the matching `arm-none-eabi-ld`, `arm-none-eabi-objcopy` and
# `arm-none-eabi-objdump`.
#
# USAGE
#   ./fetch-toolchain.sh [destdir]        (default: ../vendor/toolchain)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-$here/../vendor/toolchain}"
mkdir -p "$DEST"

MIRRORS=(
  "https://archive.archlinux.org/packages/extra/x86_64"
  "https://geo.mirror.pkgbuild.com/extra"
  "https://mirror.accum.se/mirror/archlinux/extra/os"
)

PKGS=(
  "arm-none-eabi-gcc-16.2.0-1-x86_64.pkg.tar.zst"
  "arm-none-eabi-binutils-2.47-1-x86_64.pkg.tar.zst"
)

cd "$DEST"
for pkg in "${PKGS[@]}"; do
    if [ -f "$pkg" ]; then
        echo "have $pkg"
        continue
    fi
    for m in "${MIRRORS[@]}"; do
        url="$m/$pkg"
        echo "fetching $url"
        if curl -fL --retry 3 --connect-timeout 20 -o "$pkg.part" "$url"; then
            mv "$pkg.part" "$pkg"
            echo "got $pkg"
            break
        fi
        rm -f "$pkg.part"
        echo "  mirror failed: $m"
    done
    if [ ! -f "$pkg" ]; then
        echo "!! could not fetch $pkg" >&2
        exit 1
    fi
done

for pkg in "${PKGS[@]}"; do
    echo "extracting $pkg"
    tar --use-compress-program=zstd -xf "$pkg"
done

echo
echo "=== result ==="
ls -d "$DEST"/usr/bin 2>/dev/null && ls "$DEST"/usr/bin | head -20
"$DEST/usr/bin/arm-none-eabi-gcc" --version 2>/dev/null | head -1
"$DEST/usr/bin/arm-none-eabi-ld" --version 2>/dev/null | head -1

# ---------------------------------------------------------------------------
# Arm GNU Toolchain 15.2.Rel1
#
# The TWRP kernel -- the one that gives a WORKING adb -- is
#   3.4.113-<flag>-gda91d066bc6 (acroreiser@thinkstation)
#     (gcc version 15.2.1 20251203 (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)))
# so to reproduce that kernel exactly the compiler has to be 15.2.Rel1, not
# Arch's 16.2.0 (which matches the *September* kernels instead).
#
# It is a tarball rather than a package, so it is fetched and unpacked as one.
# ---------------------------------------------------------------------------
ARMGNU_VERSION=15.2.rel1
ARMGNU_URL="https://developer.arm.com/-/media/Files/downloads/gnu/${ARMGNU_VERSION}/binrel/arm-gnu-toolchain-${ARMGNU_VERSION}-x86_64-arm-none-eabi.tar.xz"
ARMGNU_DIR="arm-gnu-toolchain-${ARMGNU_VERSION}-x86_64-arm-none-eabi"

if [ "${FETCH_ARGNU:-1}" = "1" ] && [ ! -d "$DEST/$ARMGNU_DIR" ]; then
    echo "=== fetching Arm GNU Toolchain ${ARMGNU_VERSION} ==="
    if curl -fL --retry 3 --connect-timeout 20 -o "$DEST/armgnu.tar.xz" "$ARMGNU_URL"; then
        tar xf "$DEST/armgnu.tar.xz" -C "$DEST"
        rm -f "$DEST/armgnu.tar.xz"
        echo "=== unpacked ==="
        "$DEST/$ARMGNU_DIR/bin/arm-none-eabi-gcc" --version | head -1
    else
        echo "!! could not fetch Arm GNU Toolchain ${ARMGNU_VERSION}" >&2
    fi
fi
