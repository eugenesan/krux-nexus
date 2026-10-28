#!/usr/bin/env bash
#
# build-kernel.sh — build an air-gapped hammerhead kernel.
#
# Wi-Fi, Bluetooth and the cellular baseband compiled out, driven by the
# profile in standalone/tools/profiles/hammerhead.list.
#
# Read BUILD-KERNEL.md section B1 first. This script automates the
# mechanical part; it does not tell you whether the result boots.
#
#   ./build-kernel.sh --check                 # verify tree+toolchain, change nothing
#   ./build-kernel.sh --config                # defconfig + profile only, no compile
#   ./build-kernel.sh                         # full build
#   ./build-kernel.sh --install-to ../android_device_lge_hammerhead
#
# Deliberately refuses to guess. If it cannot find the toolchain or the
# defconfig it stops and tells you, rather than producing an Image that does
# not boot and leaving you to work out why.

set -u

here="$(cd "$(dirname "$0")" && pwd)"
PROFILE="${PROFILE:-$here/profiles/hammerhead.list}"
STRIPPER="$here/apply-defconfig.py"
TOOLCHAIN_PATH="${TOOLCHAIN_PATH:-$here/../standalone/vendor/toolchain/arch-16.2.0}"
TREE=""
PREBUILTS=""
OUT="$here/out"
JOBS=""
DO_BUILD=1
DO_CONFIG=1
INSTALL_TO=""

# ======================================================================== args
# Single source of truth for the CLI. --selftest checks the help text, the
# parser and the profile list against each other.
OPTIONS_HELP=$(cat <<'EOF'
  --tree PATH        kernel tree (android_kernel_lge_hammerhead); auto-detected if omitted
  --prebuilts PATH   AOSP prebuilts root holding gcc/; auto-detected if omitted
  --out PATH         build output directory (default: ./out)
  --jobs N           parallelism (default: nproc)
  --config           apply the profile and stop -- no compile (~1 min)
  --check            verify tree and toolchain, change nothing at all
  --install-to DIR   copy the finished Image into a device tree's kernel/ dir
  -h, --help         this text
      --selftest     verify help text and parser agree, then exit
EOF
)

OPTIONS_REAL=(
    --tree --prebuilts --out --jobs --config --check --install-to --help --selftest
)

usage() {
    # Print the leading comment block whatever length it is. This used to be
    # `sed -n '2,18p' "$0"`, a hardcoded range that silently truncated the help
    # the first time the header was edited -- the same failure mode as
    # documenting options the parser does not implement.
    awk 'NR==1 {next} /^#/ {sub(/^#[ ]?/,""); print; next} {exit}' "$0"
    echo
    echo "Options"
    echo "$OPTIONS_HELP"
}

selftest() {
    local rc=0 parsed documented table missing unlisted undocumented
    parsed=$(awk '/^ *case "\$1" in/,/^ *esac/' "$0" \
             | grep -oE '^ +[^)]*\)' | grep -oE '\-\-[a-z-]+' | sort -u)
    documented=$(printf '%s\n' "$OPTIONS_HELP" | grep -oE '(^|[ ,(])--[a-z-]+' \
                | grep -oE '\-\-[a-z-]+' | sort -u)
    table=$(printf '%s\n' "${OPTIONS_REAL[@]}" | sort -u)

    missing=$(comm -13 <(printf '%s\n' "$table")      <(printf '%s\n' "$parsed"))
    unlisted=$(comm -23 <(printf '%s\n' "$table")     <(printf '%s\n' "$parsed"))
    undocumented=$(comm -23 <(printf '%s\n' "$table") <(printf '%s\n' "$documented"))

    echo "parser  : $(printf '%s ' $parsed)"
    echo "help    : $(printf '%s ' $documented)"
    [ -n "$unlisted" ]     && { echo "FAIL: parser arm not in OPTIONS_REAL:  $(printf '%s ' $unlisted)"; rc=1; }
    [ -n "$missing" ]      && { echo "FAIL: OPTIONS_REAL, no parser arm:     $(printf '%s ' $missing)"; rc=1; }
    [ -n "$undocumented" ] && { echo "FAIL: OPTIONS_REAL, absent from --help: $(printf '%s ' $undocumented)"; rc=1; }

    # The profile the script defaults to must exist and be non-empty.
    if [ ! -f "$PROFILE" ]; then
        echo "FAIL: default profile missing: $PROFILE"
        rc=1
    elif [ "$(grep -vcE '^\s*#|^\s*$' "$PROFILE")" -eq 0 ]; then
        echo "FAIL: profile is all comments: $PROFILE"
        rc=1
    fi

    [ $rc -eq 0 ] && echo "selftest OK"
    return $rc
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tree)      TREE="$2"; shift 2 ;;
        --prebuilts) PREBUILTS="$2"; shift 2 ;;
        --out)       OUT="$2"; shift 2 ;;
        --jobs)      JOBS="$2"; shift 2 ;;
        --config)    DO_BUILD=0; shift ;;
        --check)     DO_BUILD=0; DO_CONFIG=0; shift ;;
        --install-to) INSTALL_TO="$2"; shift 2 ;;
        -h|--help)   usage; exit 0 ;;
        --selftest)  selftest; exit $? ;;
        *) echo "unknown option: $1" >&2; echo "try --help" >&2; exit 2 ;;
    esac
done

# The shipped kernel is 32-bit ARM. Verified from /proc/config.gz on the live
# device: CONFIG_ARM=y, and neither CONFIG_ARM64 nor CONFIG_64BIT appears. That
# is not obvious -- the MSM8974 is a 64-bit-capable SoC, so it is reasonable to
# assume arm64 and then silently pick the wrong toolchain.
ARCH="arm"
CROSS_COMPILE="arm-linux-"

# ---------------------------------------------------------------- toolchain
# CORRECTED 2026-10-18, twice. Read this before trusting any of it.
#
# v1 assumed `arm-linux-androideabi-gcc` because that is the well-known AOSP
# prebuilt name. Then v2 assumed Clang, because `arm-linux-androideabi-4.9`
# turned out to contain no compiler on any branch. Both were wrong.
#
# The authoritative source is the device tree, not the prebuilt repo names.
# From acroreiser/android_device_lge_hammerhead, branch lineage-23.2,
# BoardConfig.mk:
#
#     TARGET_KERNEL_CLANG_COMPILE := false
#     TARGET_KERNEL_NO_GCC         := false
#     TARGET_KERNEL_CONFIG         := lineageos_hammerhead_defconfig
#     TARGET_KERNEL_SOURCE         := kernel/lge/hammerhead
#     KERNEL_TOOLCHAIN := $(TOP_PATH)/prebuilts/gcc/$(HOST_OS)-x86/arm/arm-eabi/bin
#     TARGET_KERNEL_CROSS_COMPILE_PREFIX := arm-none-eabi-
#     TARGET_KERNEL_ADDITIONAL_FLAGS := HOSTCFLAGS="-fuse-ld=lld ..."
#
# So: GCC, not Clang. Prefixed `arm-none-eabi-`, from a prebuilt named
# `arm-eabi` -- which is a different repo from `arm-linux-androideabi-4.9`.
#
# RESOLVED 2026-10-18, third attempt. The toolchain is GCC 4.9, and it is
# fetchable:
#
#     git clone --depth 1 -b lineage-19.1 \
#       https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 \
#       prebuilts/gcc-androideabi-4.9
#
# The AOSP `platform/` copy has no compiler on any branch or any commit -- AOSP
# moved kernels to Clang and stripped it. The LineageOS mirror still carries it,
# and carries something the AOSP copy lacks entirely:
#
#     bin/arm-linux-androidkernel-gcc     <- the KERNEL-targeted build
#     bin/arm-linux-androidkernel-cpp
#
# That variant is the correct one for a kernel: the userspace compiler bakes in
# bionic assumptions that do not hold for freestanding code. LineageOS ships
# both, which is why the prebuilt is 43 bin/ entries where AOSP's is 23.
#
# The `arm-eabi` prebuilt BoardConfig.mk names genuinely does not exist on
# android.googlesource.com -- that part was right -- but it is moot now: GCC
# 4.9 in the right kernel variant is what we actually want, and the cross
# prefix only picks tool names.
#
CC_PATH=""
LLVM_BIN=""

die() { echo ""; echo "!! $*" >&2; echo "" >&2; exit 1; }
step() { echo ""; echo "=== $* ==="; }
ok()   { echo "   ok  $*"; }
info() { echo "   ..  $*"; }

[ -f "$PROFILE" ]  || die "profile not found: $PROFILE"
[ -f "$STRIPPER" ] || die "stripper not found: $STRIPPER"

# ================================================================ 1. locate
step "1. locate the kernel tree"
if [ -z "$TREE" ]; then
    for c in "$here/../linux" \
             "$here/../standalone/vendor/android_kernel_lge_hammerhead" \
             "$here/../android_kernel_lge_hammerhead" \
             "$here/../kernel" \
             "$here/../../android_kernel_lge_hammerhead" \
             "$HOME/android_kernel_lge_hammerhead"; do
        [ -d "$c/arch/arm/configs" ] && { TREE="$(cd "$c" && pwd)"; break; }
    done
fi
[ -n "$TREE" ] || die "no kernel tree. Pass --tree /path/to/android_kernel_lge_hammerhead
       (the repo is LineageOS/android_kernel_lge, branch lineage-18.1 or later)"
[ -d "$TREE/arch/$ARCH/configs" ] || die "tree has no arch/$ARCH/configs -- wrong tree?
       expected: $TREE/arch/$ARCH/configs"
ok "$TREE"
info "kernel: $(head -1 "$TREE/Makefile" 2>/dev/null)"
info "version: $(grep -m1 '^VERSION' "$TREE/Makefile" 2>/dev/null) $(grep -m1 '^SUBLEVEL' "$TREE/Makefile" 2>/dev/null) $(grep -m1 '^EXTRAVERSION' "$TREE/Makefile" 2>/dev/null)"
[ "$(sed -n 's/^VERSION *= *//p' "$TREE/Makefile" | head -1)" = "3" ] \
    || info "WARNING: this profile was derived from a 3.4 CAF kernel. Anything else may differ."

# ------------------------------------------------------------- defconfig
step "2. find the defconfig"
DEFCONFIG=""
# PREFER the LineageOS one. The old glob '*hammerhead*defconfig' matched
# hammerhead_defconfig first alphabetically, so the build silently used the
# minimal bring-up config instead of the device's real base. Measured against
# the live config of the recovery kernel: lineageos_hammerhead_defconfig is
# 113 option-lines away, hammerhead_defconfig is 203 -- and README.md:26
# already said to edit the lineageos one. An explicit list, not a glob, so the
# choice is visible and cannot drift again.
# --defconfig NAME overrides the base. twrp_hammerhead_defconfig is the
# TWRP kernel's OWN live config turned into a defconfig: it is the one
# configuration known to give a working adb with the TWRP ramdisk, so it is
# the right base while the difference between the two is being bisected.
for name in ${DEFCONFIG_NAME:-lineageos_hammerhead_defconfig} \
            lineageos_hammerhead_defconfig \
            twrp_hammerhead_defconfig \
            hammerhead_defconfig; do
    f="$TREE/arch/$ARCH/configs/$name"
    [ -f "$f" ] && { DEFCONFIG="$f"; break; }
done
if [ -z "$DEFCONFIG" ]; then
    for f in "$TREE"/arch/$ARCH/configs/*hammerhead*defconfig; do
        [ -f "$f" ] && { DEFCONFIG="$f"; break; }
    done
fi
[ -n "$DEFCONFIG" ] || die "no *hammerhead* defconfig in $TREE/arch/$ARCH/configs
       ls them and pass the right tree; the profile assumes the hammerhead one."
ok "${DEFCONFIG#$TREE/}"

# ------------------------------------------------------------- toolchain
step "3. find the toolchain"
if [ -z "$PREBUILTS" ]; then
    # The AOSP prebuilt lives with the other toolchains now.
    for c in "$here/../linux" "$here/../standalone/vendor/toolchain" "$$here/../standalone/vendor/toolchain/prebuilts" \
             "$TREE/../prebuilts" "$HOME/prebuilts" "$TREE/prebuilts" /usr; do
        [ -d "$c" ] && { PREBUILTS="$(cd "$c" && pwd)"; break; }
    done
fi

# --- the toolchain, in order of preference ---------------------------------
#
# 1. `arm-linux-androidkernel-gcc`. IMPORTANT: in this prebuilt every
#    `arm-linux-androidkernel-*` entry is a SYMLINK to the matching
#    `arm-linux-androideabi-*` name -- the wrapper resolves symlinks to find
#    `real-<target>`, so they are the same binary with the same flags. There is
#    no separate kernel-targeted build here (AOSP shipped one historically; this
#    LineageOS prebuilt does not). Both prefixes work identically.
#
#    What DOES matter: this toolchain cannot do a HOSTED link. It ships bionic
#    (`crtend_android.o`) and has no `-lc`:
#        ld: error: cannot find -lc
#        ld: error: cannot open crtend_android.o
#    That is irrelevant for a Linux kernel, which is freestanding. Verified:
#      -c  -> ELF 32-bit LSB relocatable, ARM, EABI5
#      -nostdlib -ffreestanding -o -> ELF 32-bit LSB executable, ARM, EABI5
#      -march=armv7-a -mtune=cortex-a15 -mfloat-abi=softfp  accepted
#    Binutils (as/ld 2.27.0.20170315) work normally.
#
#    Also: the wrappers have a `#!/usr/bin/python` shebang. This host has only
#    python3. The shebangs of the *real* files were rewritten to
#    `#!/usr/bin/env python3` -- symlinks must be skipped or sed -i replaces
#    them with regular files and the wrapper can no longer find `real-*`.
#
# 2. the plain `arm-linux-androideabi-gcc` from the same prebuilt. Works, but
#    was not what built the shipped kernel.
#
# 3. the prebuilt BoardConfig.mk actually names, `arm-eabi` / `arm-none-eabi-`.
#    Checked for correctness, but it does not exist on android.googlesource.com.
#
# 4. a distro cross-compiler, flagged loudly as a deviation.

# 0. Arch's arm-none-eabi-gcc, which is what the reference kernels were built
#    with. The version banner of all three working kernels says
#    "gcc version 16.2.0", and Arch's package is version 16.2.0 exactly
#    (https://archlinux.org/packages/extra/x86_64/arm-none-eabi-gcc/). It is
#    also the right SHAPE for a kernel: a bare-metal target with no libc, where
#    the AOSP prebuilt is a bionic toolchain shipped for userspace.
#    Pulled by tools/fetch-toolchain.sh.
#
#    The toolchains are NOT vendored any more: the extracted trees were 3.1 GB
#    of the project, and the host's own cross-compiler builds the kernel just
#    as well (see `host` below).  What is left in standalone/vendor/toolchain
#    is the AOSP prebuilt (gcc-androideabi-4.9, which the MicroPython build
#    needs) and Arch's two .pkg.tar.zst archives, from which the Arch
#    toolchain can be unpacked again -- without a network -- by
#
#        sh tools/fetch-toolchain.sh
#
#    (it unpacks what it has and only downloads what is missing).
# TOOLCHAIN selects which of the four we use:
#   host   the host's arm-linux-gnueabi-gcc (default) -- the same toolchain
#                                                 the firmware's MicroPython is
#                                                 built with, so ONE toolchain
#                                                 serves both, and it is the one
#                                                 that is on the machine.  The
#                                                 tree forces -msoft-float
#                                                 itself (arch/arm/Makefile), so
#                                                 a hosted compiler is fine for
#                                                 a kernel.
#   arch   Arch's arm-none-eabi-gcc 16.2.0   -- matches the installed and
#                                                 ROM-zip kernels; needs
#                                                 fetch-toolchain.sh to be run
#   armgnu Arm GNU Toolchain 15.2.Rel1        -- matches the TWRP kernel, the
#                                                 one that gives a working adb;
#                                                 fetch-toolchain.sh downloads
#                                                 it
#   aosp   the AOSP prebuilt 4.9             -- a bionic userspace toolchain,
#                                                 which is not what built any of
#                                                 the reference kernels
case "${TOOLCHAIN:-host}" in
  armgnu)
      for c in "$here/../standalone/vendor/toolchain"/arm-gnu-toolchain-*/bin; do
          [ -x "$c/arm-none-eabi-gcc" ] && { CC_PATH="$c"; CROSS_COMPILE="arm-none-eabi-"; break; }
      done
      [ -n "$CC_PATH" ] || echo "   !! TOOLCHAIN=armgnu: the Arm GNU toolchain is not in the tree."
      echo "      Run: sh tools/fetch-toolchain.sh   (it downloads it)"
      ;;
  aosp) : ;;   # fall through to the prebuilt search below
  host)
      if command -v arm-linux-gnueabi-gcc >/dev/null 2>&1; then
          CC_PATH="$(dirname "$(command -v arm-linux-gnueabi-gcc)")"
          CROSS_COMPILE="arm-linux-gnueabi-"
      fi
      echo "       Host toolchain: ${CC_PATH} :: ${CROSS_COMPILE}"
      ;;
  *)
      # TOOLCHAIN_PATH points at a directory that holds the unpacked
      # Arch toolchain (the one that unpacked from the .pkg.tar.zst
      # archives is <dir>/usr/bin).  It keeps the toolchain out of the
      # project: the archives are 270 MB, the unpacked tree is 2 GB.
      for c in "${TOOLCHAIN_PATH:+$TOOLCHAIN_PATH/usr/bin}" \
               "$here/../standalone/vendor/toolchain/usr/bin"; do
          [ -x "$c/arm-none-eabi-gcc" ] && { CC_PATH="$c"; CROSS_COMPILE="arm-none-eabi-"; break; }
      done
      [ -n "$CC_PATH" ] || echo "   !! TOOLCHAIN=arch: Arch's toolchain is not unpacked."
      echo "      Run: sh tools/fetch-toolchain.sh   (it unpacks the"
      echo "      archives that are already in standalone/vendor/toolchain),"
      echo "      or point TOOLCHAIN_PATH at an unpacked copy."
      ;;
esac

for c in "$PREBUILTS/gcc-androideabi-4.9/bin" \
         "$PREBUILTS/arm-linux-androideabi-4.9/bin" \
         "$PREBUILTS/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin"
do
    [ -n "$CC_PATH" ] && break
    [ -x "$c/arm-linux-androidkernel-gcc" ] && { CC_PATH="$c"; CROSS_COMPILE="arm-linux-androidkernel-"; break; }
    [ -x "$c/arm-linux-androideabi-gcc" ]    && { CC_PATH="$c"; CROSS_COMPILE="arm-linux-androideabi-"; break; }
done

if [ -z "$CC_PATH" ]; then
    for c in "$PREBUILTS/gcc/linux-x86/arm/arm-eabi/bin" "$PREBUILTS/arm-eabi/bin"; do
        [ -x "$c/arm-none-eabi-gcc" ] && { CC_PATH="$c"; CROSS_COMPILE="arm-none-eabi-"; break; }
    done
fi

if [ -z "$CC_PATH" ] && command -v arm-linux-gnueabi-gcc >/dev/null 2>&1; then
    CC_PATH="$(dirname "$(command -v arm-linux-gnueabi-gcc)")"
    CROSS_COMPILE="arm-linux-gnueabi-"
    echo "   !! DISTRO cross-compiler, not the 4.9 cross-compiler."
    echo "      A 2013 CAF tree on GCC $(arm-linux-gnueabi-gcc -dumpversion) may not compile."
    echo ""
fi

# ---------------------------------------------------------------------------
# ccache
#
# The kernel is ~2000 translation units and is rebuilt often, so a compiler
# cache is worth having. It is enabled by prefixing the CROSS_COMPILE prefix:
# the kernel then execs "ccache arm-linux-androidkernel-gcc", and ccache
# intercepts it.
#
# This works with the AOSP toolchain even though the driver is a Python
# wrapper (arm-linux-androideabi-gcc is a script that execs
# real-arm-linux-androideabi-gcc): ccache does not need to understand the
# compiler, it only needs to run it, and it detects the compiler from the
# preprocessor output.
#
# Set CCACHE_DISABLE=1 to turn it off for a from-scratch build.
# ---------------------------------------------------------------------------
CCACHE_BIN=""
if [ "${CCACHE_DISABLE:-0}" != "1" ] && command -v ccache >/dev/null 2>&1; then
    CCACHE_BIN="ccache "
fi

if [ -n "$CC_PATH" ]; then
    # CROSS_COMPILE carries a trailing dash (the kernel Makefile does
    # `CC = $(CROSS_COMPILE)gcc` and inserts nothing), so build the driver name
    # explicitly. Appending "-gcc" here printed
    #   arm-linux-androidkernel--gcc
    # which does not exist and briefly looked like the very bug this script was
    # being fixed for. The reported name was also cosmetic-only: the double dash
    # was never passed to make.
    CC_NAME="${CROSS_COMPILE}gcc"
    ok "${CC_PATH#$PREBUILTS/}/$CC_NAME${CCACHE_BIN:+  (via $CCACHE_BIN)}"
    info "version: $("$CC_PATH/$CC_NAME" --version 2>/dev/null | head -1)"
    case "$CROSS_COMPILE" in
      arm-linux-androidkernel-*) info "prefix: arm-linux-androidkernel-   (the kernel-targeted build -- correct)" ;;
      arm-linux-androideabi-*)    info "prefix: arm-linux-androideabi-   (userspace build; may still build a kernel)" ;;
      *)                          info "prefix: $CROSS_COMPILE   (not what built this ROM; see comments)" ;;
    esac
else
    echo "   !! no kernel toolchain found"
    echo ""
    echo "   The AOSP copy of the 4.9 prebuilt has no compiler at all: AOSP"
    echo "   moved kernels to Clang and stripped it. LineageOS still carries it:"
    echo ""
    echo "     git clone --depth 1 -b lineage-19.1 \\"
    echo "       https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 \\"
    echo "       prebuilts/gcc-androideabi-4.9"
    echo ""
    die "no toolchain"
fi

[ "${JOBS:-}" = "" ] && JOBS="$(nproc 2>/dev/null || echo 4)"
ok "$JOBS parallel jobs, output in $OUT"

# ---------------------------------------------------------------- pre-flight
# The kernel's own out-of-tree check refuses to build if the SOURCE tree
# contains build artifacts:
#   "<tree> is not clean, please run 'make mrproper'"
# That is correct behaviour and worth keeping, but the message arrives buried
# in a build log with no hint of where the dirt came from. The dirt comes from
# any `make -C <tree>` WITHOUT O=, which is exactly what the config
# re-resolution used to do before it was fixed.
#
# So: check first, and say plainly how to undo it. The tree is a git checkout,
# so `git clean -xdff` restores it exactly -- `make mrproper` would also work
# but touches more.
if [ -d "$TREE/.git" ] && command -v git >/dev/null 2>&1; then
    dirty=$(cd "$TREE" && git clean -xdff --dry-run 2>/dev/null | head -20)
    if [ -n "$dirty" ]; then
        echo ""
        echo "   !! the kernel source tree is not clean:"
        echo "$dirty" | sed 's/^/        /'
        echo ""
        echo "      Out-of-tree builds refuse to run on a dirty tree. To fix:"
        echo "        (cd \"$TREE\" && git clean -xdff)"
        echo ""
        echo "      Nothing tracked by git is touched by that command."
        die "kernel source tree is dirty"
    fi
fi

[ "$DO_CONFIG" = "0" ] && { step "check only -- nothing was changed"; exit 0; }

# ================================================================ 4. config
step "4. defconfig, then apply the air-gap profile"
# With clang, CROSS_COMPILE is the *triple*, not a prefix: the kernel wants
# arm-linux-/arm-linux-gnueabi-/... for the target tools while CC/LLVM are the
# clang binaries themselves.
if [ -n "$LLVM_BIN" ]; then
    MAKE=(make -C "$TREE" O="$OUT" ARCH="$ARCH"
          CROSS_COMPILE="$CLANG_TRIPLE"
          CC="$LLVM_BIN/clang" HOSTCC="$LLVM_BIN/clang"
          LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy
          OBJDUMP=llvm-objdump STRIP=llvm-strip READELF=llvm-readelf
          LLVM=1 LLVM_IAS=1)
else
    # CROSS_COMPILE keeps its trailing dash: the kernel Makefile does
  #   CC = $(CROSS_COMPILE)gcc
  # and inserts nothing. This line used to append a SECOND dash to a value that
  # already had one, so make resolved CC=arm-linux-androidkernel--gcc, which
  # does not exist. The defconfig target only uses HOSTCC, so `--config` passed
  # and the failure waited for the first C file compiled.
  #
  # PATH must contain the toolchain directory. CROSS_COMPILE only names the
  # PREFIX; the kernel then execs `$(CROSS_COMPILE)gcc` by bare name, so if the
  # directory is not on PATH the first real compile dies with
  #   /bin/sh: 1: arm-linux-androidkernel-gcc: not found
  # and Error 127. The toolchain lives in prebuilts/, which is not a system
  # location, so the script has to put it there itself. It was found by
  # absolute path for the version report and still failed to compile.
  MAKE=(make -C "$TREE" O="$OUT" ARCH="$ARCH"
        CROSS_COMPILE="${CCACHE_BIN}${CROSS_COMPILE}"
        PATH="$CC_PATH:$PATH")
fi

info "baseline: $(basename "$DEFCONFIG")"
"${MAKE[@]}" "$(basename "$DEFCONFIG")" >"$OUT.log" 2>&1 \
    || { tail -25 "$OUT.log"; die "defconfig failed; see $OUT.log"; }
ok "baseline generated"

# apply-defconfig.py re-runs olddefconfig and verifies nothing re-enabled the
# symbols. Do NOT skip --check: on 3.4 CAF trees `select` from unrelated
# symbols is the normal way a stripped radio comes back.
info "applying $(grep -vcE '^\s*#|^\s*$' "$PROFILE") symbols from $(basename "$PROFILE")"
# --enable reads the keep-list. A stripper can only remove, but the stripped
# kernel is unusable without adb, so CONFIG_USB_FUNCTIONFS is forced back on
# and verified to survive the config re-resolution.
ENABLE_ARGS=()
# The keep-lists state what the stripped kernel must STILL have. A stripper
# can only remove, but the stripped kernel is unusable without adb, so
# CONFIG_USB_FUNCTIONFS is forced back on; and the camera needs a block of
# options that are off in every existing kernel.
#
# Syntax per file:
#   CONFIG_FOO=y   force on      -> --enable CONFIG_FOO
#   CONFIG_FOO=n   force off     -> --disable CONFIG_FOO
#   CONFIG_FOO     force on      -> --enable CONFIG_FOO   (bare name)
#
# `|| [ -n "$sym" ]` matters: `while read` DROPS the final line when the file
# does not end in a newline, and these files are hand-edited. The keep-list
# once had no trailing newline, so its only symbol was silently skipped and
# CONFIG_USB_FUNCTIONFS never reached the config.
for KEEPLIST in "$here/profiles/hammerhead-keep.list" \
               "$here/profiles/hammerhead-camera.list"; do
    [ -f "$KEEPLIST" ] || continue
    while read -r sym || [ -n "$sym" ]; do
        case "$sym" in ''|'#'*) continue;; esac
        case "$sym" in
            *=n) ENABLE_ARGS+=(--disable "${sym%=n}") ;;
            *=y) ENABLE_ARGS+=(--enable "${sym%=y}") ;;
            *)   ENABLE_ARGS+=(--enable "$sym") ;;
        esac
    done < "$KEEPLIST"
done
python3 "$STRIPPER" "$OUT/.config" "$PROFILE" "${ENABLE_ARGS[@]}" \
    -i --check --kernel-root "$TREE" >"$OUT.profile.log" 2>&1
rc=$?
grep -E 'stripped|re-enabled|NOT FOUND|WARN|error' "$OUT.profile.log" | head -20
if [ $rc -ne 0 ]; then
    tail -30 "$OUT.profile.log"
    die "profile verification failed (exit $rc). See $OUT.profile.log"
fi
ok "profile applied and verified"

echo ""
echo "   confirm for yourself before compiling:"
echo "     grep -E 'PIL_MSS|RMNET_BAM|BCMDHD|CFG80211|^CONFIG_BT' $OUT/.config"
echo ""

[ "$DO_BUILD" = "0" ] && { step "config only (--config) -- no compile"; exit 0; }

# ================================================================ 5. build
step "5. compile"
"${MAKE[@]}" -j"$JOBS" all >"$OUT.build.log" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    tail -30 "$OUT.build.log"
    die "build failed (exit $rc). See $OUT.build.log"
fi
ok "compiled"

step "6. locate the image"
IMG=""
for f in "$OUT/arch/$ARCH/boot/zImage" "$OUT/arch/$ARCH/boot/Image" \
         "$OUT/arch/$ARCH/boot/Image-dtb"; do
    [ -f "$f" ] && { IMG="$f"; break; }
done
[ -n "$IMG" ] || { tail -20 "$OUT.build.log"; die "build reported success but found no boot image under $OUT/arch/$ARCH/boot/"; }
ok "$IMG  ($(stat -c %s "$IMG") bytes)"

# ------------------------------------------------------------- sanity
step "7. what is actually in the image's config"
for s in MSM_PIL_MSS_QDSP6V5 MSM_RMNET_BAM BCMDHD CFG80211 BT_RFCOMM \
         MSM_PIL_LPASS_QDSP6V5 MSM_PIL_VENUS NFC_BCM2079X QSEECOM; do
    v=$(grep -E "^(CONFIG_$s=|# CONFIG_$s )" "$OUT/.config" 2>/dev/null | head -1)
    printf '   %-26s %s\n' "$s" "${v:-<absent>}"
done
echo ""
echo "   LPASS_QDSP6V5 (audio) and VENUS (GPU) must be =y -- if they are not,"
echo "   something selected through CONFIG_MSM_PIL and the audio path is gone."

if [ -n "$INSTALL_TO" ]; then
    step "8. install into the device tree"
    mkdir -p "$INSTALL_TO/kernel"
    cp "$IMG" "$INSTALL_TO/kernel/Image"
    ok "$INSTALL_TO/kernel/Image"
    echo ""
    echo "   Rebuild the ROM with that device tree checked out, or repack"
    echo "   boot.img with mkbootimg. A kernel is not flashable on its own."
fi

step "done"
echo "   kernel : $IMG"
echo "   config : $OUT/.config"
echo "   logs   : $OUT.log  $OUT.profile.log  $OUT.build.log"
echo ""
echo "   NEXT: flash it and check, in this order:"
echo "     1. it boots at all"
echo "     2. /sys/bus/platform/drivers has NO pil-q6v5-mss entry"
echo "     3. ls /firmware/image/modem.* still shows the files (that is fine --"
echo "        nothing loads them) but nothing in dmesg mentions pil or baseband"
echo "     4. audio works (LPASS) and the GPU is alive (Venus)"
echo "     5. GPS and sensors still work -- they share the SMD IPC transport"
echo ""