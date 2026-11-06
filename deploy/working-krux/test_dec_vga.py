import _image
data=open('/data/krux/datum-vga.gray','rb').read()
img=_image.from_nv12(data,640,480,640,320,640,480)
r=img.find_qrcodes(True)
print('n',len(r))
for x in r: print('payload',repr(x.payload()[:40]))
