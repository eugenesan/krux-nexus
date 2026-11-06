import lcd
lcd.init()
print("w:", lcd.width(), "h:", lcd.height())
# Fill entire screen with red
lcd.fill_rectangle(0, 0, lcd.width(), lcd.height(), 0x0000FF)
lcd.flush()
# Read back
v = lcd.get_pixel(100, 100)
print("pixel at (100,100):", hex(v) if v is not None else None)
print("done")
