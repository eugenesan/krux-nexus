#!/bin/bash
# Extract the ROM kernel's own config (it is embedded in the kernel image)
# and look at the camera options.  This settles whether the ROM has
# camera_v1 compiled in as well as camera_v2.
cd /tmp/opencode || exit 1
python3 - <<'EOF'
import zlib, re
d=open('/tmp/opencode/rom.raw','rb').read()
print("  searching for the embedded kernel config in the ROM kernel...")
found=0
for m in re.finditer(b'\x1f\x8b\x08', d):
    off=m.start()
    try:
        o=zlib.decompressobj(16+zlib.MAX_WBITS)
        out=o.decompress(d[off:off+400000])
    except Exception:
        continue
    if out[:40].startswith(b'# Automatically generated file') or b'CONFIG_MSM_CAMERA=' in out[:200]:
        open('/tmp/opencode/rom-kconfig','wb').write(out)
        print(f"  found the config at offset {off} ({len(out)} bytes)")
        found=1
        break
if not found:
    print("  not found that way")
EOF
if [ -f /tmp/opencode/rom-kconfig ]; then
  echo
  echo "=== the ROM kernel's camera configuration ==="
  grep -E 'CONFIG_MSM_CAMERA=|CONFIG_MSM_CAMERA_V4L2=|CONFIG_MSMB_CAMERA=|CONFIG_IMX179=|CONFIG_MT9M114B=|CONFIG_MSM_CAMERA_V4L2' /tmp/opencode/rom-kconfig | sed 's/^/  /'
  grep -E '^# CONFIG_MSM_CAMERA|^# CONFIG_MSMB_CAMERA' /tmp/opencode/rom-kconfig | sed 's/^/  /'
fi
