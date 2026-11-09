import sys
sys.path.append('/data/krux')
import image

y = open('/data/krux/snap.raw', 'rb').read()
print("read", len(y))
img = image.Image.from_nv12(y, 720, 960, 720, 360, 720, 960)
print("image", img.width(), img.height())
for flag in (False, True):
    res = img.find_qrcodes(flag)
    print("find_qrcodes(%s) -> %d" % (flag, len(res)))
    for r in res:
        print("   ", r.payload())
