#!/bin/bash
# Cleanup, part 2: make the keeper's "master" exactly "public upstream tip
# + the two fixes", and put everything else on side branches.
set -e
R=/home/user/Develop/src/android-air-gapped
KEEP=$R/standalone/vendor/android_kernel_lge_hammerhead.oct
cd $KEEP

echo "=== 1. preserve the current work (fixes + vendored TWRP defconfig) ==="
git branch -f airgap/wip-twrp-defconfig lineage-23.2
echo "  airgap/wip-twrp-defconfig -> $(git rev-parse --short airgap/wip-twrp-defconfig)"

echo
echo "=== 2. put the tracking branch back to pristine upstream ==="
git checkout --detach 466f069d
git branch -f lineage-23.2 origin/lineage-23.2
git branch -v | sed 's/^/  /'

echo
echo "=== 3. master = upstream tip + the two fixes ==="
git checkout -B master 466f069d
git reset --hard 466f069d
echo "  master starts at $(git rev-parse --short master) (upstream tip)"

git cherry-pick --no-commit airgap/wip-twrp-defconfig -- drivers/usb/gadget/android.c 2>/dev/null || \
  git checkout airgap/wip-twrp-defconfig -- drivers/usb/gadget/android.c
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

git checkout airgap/wip-twrp-defconfig -- drivers/usb/gadget/f_fs.c
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

echo
echo "=== 4. the vendored TWRP defconfig stays on the side branch only ==="
git checkout airgap/wip-twrp-defconfig -- arch/arm/configs/twrp_hammerhead_defconfig
git rm -q --cached arch/arm/configs/twrp_hammerhead_defconfig
rm -f arch/arm/configs/twrp_hammerhead_defconfig
echo "  tree clean: $([ -z "$(git status --porcelain)" ] && echo yes || echo NO)"

echo
echo "=== 5. final branch layout ==="
git branch -v | sed 's/^/  /'
echo
echo "  master is exactly upstream + 2 commits:"
git diff --stat 466f069d master | sed 's/^/    /'
git log --oneline master | head -4 | sed 's/^/    /'
echo
echo "  the two publishable patches:"
git format-patch -2 --stdout master | grep -E '^(From |Subject:)' | sed 's/^/    /'
