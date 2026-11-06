import lcd
lcd.init()
print("w:", lcd.width(), "h:", lcd.height())
lcd.fill_rectangle(0, 0, 200, 200, 0x00FF00)
lcd.flush()
print("done")
