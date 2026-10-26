#!/bin/sh
# /init -- mount the pseudo filesystems, then hand over to busybox init.
#
# This must be a script, not busybox itself. /init runs before ANY filesystem is
# mounted, so /dev does not exist yet: there is no /dev/console for busybox to
# attach stdio to, and no device nodes. If init exits, the kernel panics with
# "Attempted to kill init" and the machine hangs on the bootloader logo forever
# with no error anywhere -- which is exactly the symptom we spent six boots
# failing to explain.
#
# rcS (run by busybox init as ::sysinit) then does USB first, display second.

/bin/busybox mkdir -p /proc /sys /dev /tmp /run

/bin/busybox mount -t proc     proc     /proc  2>/dev/null
/bin/busybox mount -t sysfs    sysfs    /sys   2>/dev/null
/bin/busybox mount -t devtmpfs devtmpfs /dev   2>/dev/null
/bin/busybox mount -t tmpfs    tmpfs    /tmp   2>/dev/null

echo "init: mounted /proc /sys /dev; handing over to busybox init" > /dev/kmsg 2>/dev/null

exec /bin/busybox init
