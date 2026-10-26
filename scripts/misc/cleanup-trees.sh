#!/bin/bash
# Cleanup of the two kernel trees.
#
#   android_kernel_lge_hammerhead      the May base (a squashed vendor import
#                                      of upstream 299b2e18) + 3 of our commits
#   android_kernel_lge_hammerhead.oct  a clone of acroreiser's public repo,
#                                      shallow + blobless, at upstream's tip
#                                      466f069d + our commit
#
# The keeper is the .oct one: it has the public upstream history, so the
# fixes can be published as-is.  master becomes "upstream tip + the two
# fixes, and nothing else"; everything else is moved onto side branches.
set -e
R=/home/user/Develop/src/android-air-gapped
V=$R/standalone/vendor
KEEP=$V/android_kernel_lge_hammerhead.oct
MAY=$V/android_kernel_lge_hammerhead

cd $KEEP

echo "=== 1. preserve the May tree's work as a branch ==="
git fetch "$MAY" master:legacy/may-2026-05 2>&1 | tail -2
git log --oneline legacy/may-2026-05 | sed 's/^/  /'

echo
echo "=== 2. preserve the current master (fixes + the vendored TWRP defconfig) ==="
git branch -f airgap/wip-twrp-defconfig master
git branch -v | sed 's/^/  /'

echo
echo "=== 3. rebuild master as upstream tip + the two fixes only ==="
git reset --soft 466f069d
git reset                       # unstage everything, keep the working tree

git add drivers/usb/gadget/android.c
git -c user.email=dev@local -c user.name=dev commit -q -F - <<'MSG'
usb: gadget: default the functionfs alias to "adb"

The name written to /sys/class/android_usb/android0/f_ffs/aliases decides
whether the name "adb" in a USB configuration string selects the
userspace functionfs instance (what every modern adbd uses) or the legacy
in-kernel f_adb function (drivers/usb/gadget/f_adb.c).

That attribute only exists once the gadget has bound to a UDC, so
userspace that writes it early races with the UDC coming up.  If the write
is lost, the composite binds the in-kernel adb function: the host still
enumerates an "ADB Interface", but no userspace adbd is attached to it,
and adb sits at "offline" forever.

Defaulting the alias removes the race -- the write, when it happens, just
sets the same value again.
MSG

git add drivers/usb/gadget/f_fs.c
git -c user.email=dev@local -c user.name=dev commit -q -F - <<'MSG'
usb: f_fs: queue a full-size request for reads

The DWC3 controller cannot complete a short OUT transfer: a request
shorter than the endpoint's wMaxPacketSize never finishes, so the data
the host sent never reaches userspace.

adbd reads the 24-byte ADB header with a single read(), so the transport
stalls and adb never gets past "offline": the host sees the ADB interface
enumerate and then nothing else.

Queue the rounded-up buffer length (round_up(len, 1024), always a
multiple of the 512-byte max packet size) for reads.  The copy back to
userspace is still limited to the length the caller asked for.
MSG

echo "  master is now:"
git log --oneline -3 | sed 's/^/    /'

echo
echo "=== 4. the vendored TWRP defconfig leaves master (it is on the side branch) ==="
git checkout -- arch/arm/configs/twrp_hammerhead_defconfig 2>/dev/null || \
  git rm -q --cached arch/arm/configs/twrp_hammerhead_defconfig
rm -f arch/arm/configs/twrp_hammerhead_defconfig
git status --short | sed 's/^/  /'
echo "  (tree clean: $([ -z "$(git status --porcelain)" ] && echo yes || echo NO))"

echo
echo "=== 5. what master now is ==="
git diff --stat 466f069d master | sed 's/^/  /'
echo
git log --oneline --graph --all | head -14 | sed 's/^/  /'
