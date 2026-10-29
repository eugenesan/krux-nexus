#!/bin/bash
# build.sh -- Phase 2 rootfs for Nexus 5 / hammerhead.
#
# Produces an initramfs cpio that boots on the STOCK kernel via
#   fastboot boot <img>
# The stock kernel comes from the device's own boot.img; Buildroot's kernel is
# never used and `target-final` is called instead of `make` so it is never
# built at all.
#
# Usage:
#   ./build.sh              config + build
#   ./build.sh config       just resolve the config
#   ./build.sh build        just run the build
#   ./build.sh clean

set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# The Buildroot source tree: it lives next to this directory
# (standalone/buildroot), not inside it.
BR="$here/../buildroot"
[ -d "$BR" ] || BR="$here/../../rootfs/buildroot"
OUT="$here/build"
FRAG="$here/buildroot.fragment"
LOGS="$here/logs"
mkdir -p "$LOGS"

die() { echo ""; echo "!! $*" >&2; echo "" >&2; exit 1; }
info() { printf '   %s\n' "$*"; }

[ -d "$BR" ] || die "buildroot not found at $BR"
[ -f "$FRAG" ] || die "fragment not found at $FRAG"

cmd="${1:-all}"

# ---------------------------------------------------------------- config
resolve_config() {
    echo "=== 1. base defconfig + our fragment ==="
    mkdir -p "$OUT"
    # allnoconfig, NOT a board defconfig. qemu_arm_vexpress_defconfig is the
    # closest CPU match (Cortex-A15 quad) but it is a *board* config: it drags in
    # BR2_GLOBAL_PATCH_DIR=board/qemu/patches, a qemu post-image script and
    # host-qemu, which is a 300 MB download of something we will never use.
    # Layering a fragment onto a board config imports the whole board.
    make -C "$BR" O="$OUT" allnoconfig >"$LOGS/config.log" 2>&1 \
        || { tail -20 "$LOGS/config.log"; die "allnoconfig failed"; }

    # NOT merge_config.sh: it calls `make alldefconfig` internally, does not
    # check whether that succeeded, and exits 0 having changed nothing. It was
    # silently applying nothing here. apply-fragment.py writes the symbols
    # directly and then reads every one of them back.
    python3 "$here/apply-fragment.py" "$BR" "$OUT" "$FRAG" 2>&1 | tee "$LOGS/merge.log" \
        || die "some symbols did not survive -- see above. Fix the fragment, do not ignore."

    echo "   arch/cpu:"
    grep -E '^BR2_(arm|cortex_a15|ARM_EABIHF|ARM_FPU_VFPV4|ARM_CPU_HAS_)=' "$OUT/.config" | sed 's/^/     /'
    echo "   rootfs:"
    grep -E '^BR2_TARGET_ROOTFS_(CPIO|CPIO_GZIP|EXT2|SQUASHFS)=' "$OUT/.config" | sed 's/^/     /'
    echo "   toolchain:"
    grep -E '^BR2_TOOLCHAIN_BUILDROOT_(GLIBC|MUSL|UCLIBC)=' "$OUT/.config" | sed 's/^/     /'
    echo "   verify, do not assume:"
    grep -q '^BR2_TARGET_ROOTFS_CPIO=y' "$OUT/.config" || die "cpio rootfs not selected"
    grep -q '^BR2_cortex_a15=y'          "$OUT/.config" || die "cortex-a15 not selected (apply-fragment would have said so)"
    grep -qE '^BR2_ARM_CPU_HAS_(NEON|VFPV4)=y' "$OUT/.config" \
        || die "neither NEON nor VFPv4 is enabled -- the Broadcom HAL needs both"
    echo "     ok"
}

# ---------------------------------------------------------------- post-build
post_build() {
    echo
    echo "=== 3. post-build: firmware blobs ==="
    tgt="$OUT/target"
    [ -d "$tgt" ] || die "no target dir at $tgt"

    mkdir -p "$tgt/opt/nfc/firmware"
    cp -v "$here/blobs/"*.ncd "$tgt/opt/nfc/firmware/" 2>/dev/null \
        || info "NOTE: firmware blobs missing, skipping (Phase 3 will need them)"

    # The ROM's NFC config, parsed at runtime by the port. Without it there is
    # no LPTD_CFG / PREINIT_DSP_CFG / NFA_DM_START_UP_CFG to replay.
    if [ -f "$here/../phase0/libnfc-nci-20791b05.conf" ]; then
        cp -v "$here/../phase0/libnfc-nci-20791b05.conf" "$tgt/opt/nfc/" \
            && echo "     (this is the ROM's own config -- its FW_PATCH paths"
        echo "      still point at /vendor/firmware, so the port must remap"
        echo "      them to /opt/nfc/firmware)"
    fi

    # The port's `nfc` module: the firmware does `import nfc` and calls
    # `nfc.transport()`.  It goes next to the firmware's own files, and
    # the init script puts that directory on the path.
    mkdir -p "$tgt/opt/krux"
    if [ -f "$here/../krux/src/nfc.py" ]; then
        cp -v "$here/../krux/src/nfc.py" "$tgt/opt/krux/nfc.py" \
            && echo "     (the port's nfc module -- the SeedKeeper transport)"
    else
        echo "NOTE: the port's nfc.py is missing, skipping"
    fi

    # The port's `board` module: what the firmware is running on.
    if [ -f "$here/../krux/src/board.py" ]; then
        cp -v "$here/../krux/src/board.py" "$tgt/opt/krux/board.py" \
            && echo "     (the port's board module -- the hammerhead's config)"
    else
        echo "NOTE: the port's board.py is missing, skipping"
    fi

    # MicroPython itself: buildroot cannot build it (it insists on a
    # dynamic link), so the hand-built static binary is copied in.
    if [ -f "$here/micropython-arm32" ]; then
        mkdir -p "$tgt/usr/bin"
        cp -v "$here/micropython-arm32" "$tgt/usr/bin/micropython" \
            && echo "     (the static arm32 MicroPython -- the firmware's runtime)"
    else
        echo "NOTE: micropython-arm32 is missing, skipping"
    fi

    # The Krux firmware.  The ported package is the source of truth; it
    # already contains the Linux touch and button modules.
    KRUX_SRC=${KRUX_SRC:-$here/../krux/src}
    if [ -d "$KRUX_SRC/krux" ]; then
        rm -rf "$tgt/opt/krux/krux"
        cp -r "$KRUX_SRC/krux" "$tgt/opt/krux/krux"
        cp "$KRUX_SRC"/*.py "$tgt/opt/krux/"
        [ -d "$KRUX_SRC/flash" ] && cp -r "$KRUX_SRC/flash" "$tgt/opt/krux/"
        EMBIT_SRC="$here/../krux/vendor/embit/src/embit"
        [ -d "$EMBIT_SRC" ] && cp -r "$EMBIT_SRC" "$tgt/opt/krux/embit"
        echo "     (the firmware's entry point and the port's modules)"
    else
        echo "NOTE: the Krux firmware is not at $KRUX_SRC, skipping"
    fi

    echo
    echo "=== 4. post-build: verification ==="
    echo "   kernel  : $(grep -c . "$OUT/.config" >/dev/null && echo configured) (stock; Buildroot's is unused)"
    cpio=$(ls "$OUT"/images/rootfs.cpio* 2>/dev/null | head -1)
    if [ -z "$cpio" ]; then
        echo "   cpio    : MISSING -- build.sh would have died earlier if this was 'all'"
    else
        echo "   cpio    : $(basename "$cpio") ($(stat -c %s "$cpio") bytes)"
    fi
    echo "   size    : $(du -sh "$tgt" 2>/dev/null | cut -f1) target"
    # -L first: busybox installs /bin/sh as a symlink, and a naive
    # `ls -l | awk '{print $NF}'` prints "busybox" for both the symlink and the
    # binary, which reads as present either way and tells you nothing.
    for probe in sbin/init bin/busybox bin/sh bin/ls; do
        if   [ -L "$tgt/$probe" ]; then v="symlink -> $(readlink "$tgt/$probe")"
        elif [ -e "$tgt/$probe" ]; then v="file $(stat -c %s "$tgt/$probe") B"
        else v="MISSING"; fi
        printf '   %-9s %s\n' "$(basename $probe)" "$v"
    done
    n=$(ls -l "$tgt/bin" 2>/dev/null | grep -c '^l')
    echo "   busybox  $n applet symlinks in bin/"
    echo "   devtmpfs: $( [ -d "$tgt/dev" ] && echo present || echo 'absent (BR2 creates it at boot)')"
    echo
    echo "   IMPORTANT: this is a bare rootfs. It has no idea that:"
    echo "     - /dev/bcm2079x must be made (devtmpfs does this)"
    echo "     - the display is /dev/graphics/fb0 with a PADDED stride"
    echo "     - the panel needs lge.kcal applied for correct colours"
    echo "   Those are Phase 2's remaining work, not done by this script."
}

# ---------------------------------------------------------------- build
do_build() {
    echo
    echo "=== 2. build (target-final only -- no kernel, no bootloader) ==="
    echo "   this takes 10-30 min; log: $LOGS/build.log"
    # PATH shim: this host has no working IPv6 (curl -6 hangs entirely; IPv4 is
    # ~3x faster). wget picks IPv6 first and burns a 30 s timeout per tarball.
    # ~/bin/wget wraps the real wget with --inet4-only.
    if make -C "$BR" O="$OUT" rootfs-cpio -j"$(nproc)" \
         PATH="$HOME/bin:$PATH" >"$LOGS/build.log" 2>&1; then
        # `target-final` and `target-finalize` both succeed WITHOUT producing a
        # rootfs image. The real target is `rootfs-cpio`. The first version of
        # this script reported "build OK" and printed a globbed path for a file
        # that did not exist -- the ninth silent-success in this project, and
        # this time it was our own verification doing the lying.
        # Buildroot writes images to $OUT/images (BR2_IMAGE_DIR), not $OUT/target.
        cpio=$(ls "$OUT"/images/rootfs.cpio* 2>/dev/null | head -1)
        [ -z "$cpio" ] && cpio=$(ls "$OUT"/target/rootfs.cpio* 2>/dev/null | head -1)
        if [ -z "$cpio" ]; then
            echo "   !! make SUCCEEDED but produced no rootfs.cpio* in $OUT/target"
            echo "      looked in $OUT/images and $OUT/target"
            ls "$OUT"/images 2>/dev/null | sed 's/^/        images: /'
            ls "$OUT"/target 2>/dev/null | tail -5 | sed 's/^/        target: /'
            die "build reported success without an image"
        fi
        echo "   build OK -> $(basename "$cpio") ($(stat -c %s "$cpio") bytes)"
    else
        echo "   !! build FAILED -- last 30 lines:"
        tail -30 "$LOGS/build.log"
        die "see $LOGS/build.log"
    fi
}

case "$cmd" in
    clean)  rm -rf "$OUT"; echo "removed $OUT" ;;
    config) resolve_config ;;
    build)  do_build; post_build ;;
    all)    resolve_config; do_build; post_build ;;
    *)      die "unknown command: $cmd (all|config|build|clean)" ;;
esac
