#!/bin/bash
# Rebase our two adb fixes onto the newest upstream commit (466f069d) and build.
set -e
R=/home/user/Develop/src/android-air-gapped
T=$R/standalone/vendor/android_kernel_lge_hammerhead.oct
cd $T

echo "=== 1. drop the old experiment, keep the TWRP defconfig ==="
git checkout -- drivers/usb/gadget/f_fs.c
git status --short | sed 's/^/  /'

echo
echo "=== 2. does upstream's f_fs.c still need the uidgid include? ==="
grep -n 'uidgid.h' drivers/usb/gadget/f_fs.c || echo "  (no uidgid.h include -- check for the build)"

echo
echo "=== 3. apply fix 1: default the functionfs alias to \"adb\" ==="
python3 - <<'PY'
p='drivers/usb/gadget/android.c'
s=open(p).read()
old="""	android_dev->name = pdev->name;
	android_dev->disable_depth = 1;
"""
new="""	android_dev->name = pdev->name;
	android_dev->disable_depth = 1;

	/*
	 * Default the functionfs alias to "adb".
	 *
	 * The name written to /sys/class/android_usb/android0/f_ffs/aliases
	 * decides whether the name "adb" in a configuration string selects the
	 * userspace functionfs instance (what every modern adbd uses) or the
	 * legacy in-kernel f_adb function (drivers/usb/gadget/f_adb.c).
	 *
	 * The attribute only exists once the gadget has bound to a UDC, so
	 * userspace that writes it early races with the UDC coming up.  If
	 * that write is lost, the composite binds the in-kernel adb function:
	 * the host still enumerates an "ADB Interface", but no userspace adbd
	 * is attached to it, and adb sits at "offline" forever.
	 *
	 * Defaulting the alias removes the race -- the write, when it happens,
	 * just sets the same value again.
	 */
	strlcpy(android_dev->ffs_aliases, "adb", sizeof(android_dev->ffs_aliases));
"""
assert old in s
s=s.replace(old,new,1)
open(p,'w').write(s)
print("  android.c patched")
PY

echo
echo "=== 4. apply fix 2: queue a full-size request for reads ==="
python3 - <<'PY'
p='drivers/usb/gadget/f_fs.c'
lines=open(p).read().split('\n')
idxs=[i for i,l in enumerate(lines) if 'req->length   = io_data->len;' in l]
assert len(idxs)==2, idxs
for i in idxs:
    lines[i]=lines[i].replace('req->length   = io_data->len;',
                              'req->length   = io_data->read ? buffer_len : io_data->len;')
comment=[
"\t\t\t/*",
"\t\t\t * For a read, queue the whole (rounded up) buffer, not just",
"\t\t\t * the number of bytes the caller asked for.",
"\t\t\t *",
"\t\t\t * The DWC3 controller cannot complete a short OUT transfer: a",
"\t\t\t * request shorter than the endpoint's wMaxPacketSize never",
"\t\t\t * finishes, so the data the host sent never reaches",
"\t\t\t * userspace.  adbd reads the 24-byte ADB header with a single",
"\t\t\t * read(), so queueing io_data->len here stalls the transport",
"\t\t\t * and adb never gets past \"offline\": the host sees the ADB",
"\t\t\t * interface enumerate and then nothing else.",
"\t\t\t *",
"\t\t\t * buffer_len is round_up(io_data->len, 1024), so it is always a",
"\t\t\t * multiple of the 512-byte max packet size and the buffer",
"\t\t\t * allocated above is large enough.  The copy back to userspace",
"\t\t\t * is still limited to io_data->len by the min_t() below.",
"\t\t\t */",
]
i=idxs[1]
j=i
while 'req->buf' not in lines[j]:
    j-=1
lines[j:j]=comment
open(p,'w').write('\n'.join(lines))
print("  f_fs.c patched")
PY

git add -A
git -c user.email=dev@local -c user.name=dev commit -q -m "usb: make the adb transport work on this device

Two bugs kept adb at \"offline\" on our own kernel with the TWRP
userspace: the host enumerated an ADB interface and then nothing
answered it.

1. /sys/class/android_usb/android0/f_ffs/aliases decides whether the
   name \"adb\" in a configuration string selects the userspace
   functionfs instance or the legacy in-kernel f_adb function.  The
   attribute only exists once the gadget has bound to a UDC, so
   userspace that writes it early races with the UDC.  Lose the write
   and the composite binds the in-kernel function, which no userspace
   adbd is attached to.  Default the alias to \"adb\" so the race cannot
   happen.

2. The DWC3 controller cannot complete a short OUT transfer: a request
   shorter than the endpoint's wMaxPacketSize never finishes.  adbd
   reads the 24-byte ADB header with a single read(), so the transport
   stalled.  Queue the rounded-up buffer length for reads.

Also add the TWRP defconfig, which matches the config of the known-good
TWRP kernel for this device byte for byte."
git log --oneline -2 | sed 's/^/  /'

echo
echo "=== 5. build ==="
DEFCONFIG_NAME=twrp_hammerhead_defconfig TOOLCHAIN=armgnu \
  bash $R/build/build-kernel.sh --tree $T --out /tmp/opencode/ko-rebase \
  > /tmp/opencode/korebase.log 2>&1
grep -E '^\s+(ok|!!)' /tmp/opencode/korebase.log | tail -4 | sed 's/^/  /'
ls -la /tmp/opencode/ko-rebase/arch/arm/boot/zImage-dtb
