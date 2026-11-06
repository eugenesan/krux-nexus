import lcd
import time
import _image

W, H = 480, 640
y = bytearray(W * H)
for row in range(H):
    for col in range(W):
        # vertical bars + horizontal gradient so tiling is obvious
        y[row * W + col] = ((col // 60) * 50 + row // 16) & 0xFF
uv = bytes([128]) * (W // 2 * H // 2 * 2)
img = _image.from_nv12(bytes(y) + uv, W, H, W, W // 2, W, H)

lcd.init(invert=False)
lcd.mirror(False)
lcd.bgr_to_rgb(False)
lcd.rotation(1)

lcd.clear(0x0000)
lcd.display(img, oft=((lcd.width() - W * 2) // 2, (lcd.height() - H * 2) // 2),
            roi=(0, 0, W, H), scale=2, flip_x=True, flip_y=True)
lcd.flush()
time.sleep_ms(4000)
