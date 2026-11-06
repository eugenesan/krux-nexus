import lcd
import time

lcd.init(invert=False)
lcd.mirror(False)
lcd.bgr_to_rgb(False)
lcd.rotation(1)

for _ in range(6):
    lcd.clear(0xF800)  # red
    lcd.flush()
    time.sleep_ms(500)
    lcd.clear(0x001F)  # blue
    lcd.flush()
    time.sleep_ms(500)
