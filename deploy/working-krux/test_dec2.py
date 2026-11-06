import _image
data=open('/data/krux/qr-compact.gray','rb').read()
img=_image.from_nv12(data,320,240,320,160,320,240)
for inv in (False,True):
 r=img.find_qrcodes(inv); print('inv',inv,'n',len(r))
 for x in r: print('payload repr',repr(x.payload()))
