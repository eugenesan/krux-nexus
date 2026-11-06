import _image, sys
data=open('/data/krux/qr-test.gray','rb').read()
img=_image.from_nv12(data,320,240,320,160,320,240)
print('stats',img.get_statistics().l_mean(),img.get_statistics().l_max())
for inv in (False,True):
    r=img.find_qrcodes(inv)
    print('inv',inv,'n',len(r))
    for x in r: print('payload',x.payload())
