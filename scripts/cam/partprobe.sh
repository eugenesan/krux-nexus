echo "=== partition sizes ==="
for d in /dev/block/platform/*/by-name/*; do
  n=$(basename "$d")
  sz=$(blockdev --getsize64 "$d" 2>/dev/null)
  if [ -n "$sz" ]; then
    echo "  $n $(( sz / 1048576 ))MB"
  fi
done

echo
echo "=== recovery partition: has content? ==="
R=$(ls /dev/block/platform/*/by-name/recovery 2>/dev/null | head -1)
echo "  device: $R"
if [ -n "$R" ]; then
  echo "  first bytes (should be a gzip/zImage header):"
  dd if="$R" bs=1 count=16 2>/dev/null | od -An -tx1 | sed 's/^/    /'
  dd if="$R" bs=2048 count=1 2>/dev/null > /tmp/rec-blk 2>/dev/null
  echo "  size: $(blockdev --getsize64 "$R") bytes"
fi

echo
echo "=== boot partition header ==="
B=$(ls /dev/block/platform/*/by-name/boot 2>/dev/null | head -1)
dd if="$B" bs=1 count=16 2>/dev/null | od -An -tx1 | sed 's/^/    /'

echo
echo "=== is the recovery image ours to replace? (we do NOT need to) ==="
echo "  current boot: adb shell getprop ro.build.version.incremental"
