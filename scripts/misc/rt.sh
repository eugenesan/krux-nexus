chmod 755 /data/local/tmp/fbmmap
echo "--- mode 0: write() FULL buffer 0 ---"
timeout 20 /data/local/tmp/fbmmap 0
echo "  exit=$?  (124 means it blocked)"
echo "--- mode 1: mmap fill all 3 buffers ---"
timeout 20 /data/local/tmp/fbmmap 1
echo "  exit=$?  (124 means it blocked)"
