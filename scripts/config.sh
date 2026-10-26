# config.sh -- the paths the scripts in this directory share.
#
# Sourced by the scripts here, not run on its own.
#
# Two kinds of path, and the difference matters:
#
#   * the PROJECT holds what is *ours*: the kernel tree, the port, the
#     captured data.  It is the thing that survives a reboot, and it is
#     what the scripts read.
#
#   * tmpfs (/tmp/opencode) holds what is *rebuilt*: the kernel's object
#     tree, the firmware's build tree, the scratch the captures write
#     into.  It is fast, and it is meant to be thrown away.
#
# So the scripts below default to the tmpfs paths, and every one of
# them can be pointed elsewhere by exporting the variable first.

# The project this script tree belongs to.
: "${PROJECT:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

# The kernel's build tree (ours, in the project) and its object tree
# (rebuilt, on tmpfs).  The build script is build/build-kernel.sh.
: "${KERNEL_TREE:=$PROJECT/standalone/vendor/android_kernel_lge_hammerhead}"
: "${KERNEL_OUT:=/tmp/opencode/ko-cap}"

# The firmware's build tree (rebuilt, on tmpfs) and the armel sysroot
# (rebuilt from the vendored packages).
: "${MP_BUILD:=/tmp/opencode/mp-build}"
[ ! -d /usr/arm-linux-gnueabi ] || ARMEL_SYSROOT=/usr/arm-linux-gnueabi
: "${ARMEL_SYSROOT:=/tmp/opencode/armel-sysroot}"

# Scratch for the captures: where the ROM's own blobs are unpacked and
# where the logs land.
: "${SCRATCH:=/tmp/opencode}"

# The RAMDISK_* and CHROMATRIX_* paths the capture scripts use are
# derived from the project, because the ROM's blobs are in the project:
#
#   the ROM's camera HAL libraries   standalone/cam/data/vendor/rom-libs
#   the captured vendor configs      standalone/cam/data/vendor/{seq,cyc,sensor,ispif,csid}
#
# so the captures read from the project and write to $SCRATCH.
