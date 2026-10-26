import lcd

lcd.init()
print("the display: %dx%d" % (lcd.width(), lcd.height()))

lcd.clear(lcd.BLACK)
lcd.draw_string(20, 40, "Krux on the Nexus 5", lcd.WHITE, lcd.BLACK)
lcd.draw_string(20, 80, "0123456789 abcdefghij", lcd.YELLOW, lcd.BLACK)
lcd.draw_string(20, 120, "lcd.draw_string works", lcd.GREEN, lcd.BLACK)
lcd.draw_outline(20, 180, 300, 60, lcd.RED)
lcd.fill_rectangle(360, 180, 120, 60, lcd.BLUE, 10)
lcd.draw_line(20, 280, 500, 360, lcd.MAGENTA)

# what is actually in the framebuffer?
print("a black spot:      %04x" % lcd.get_pixel(900, 900))
print("the first letter:  %04x" % lcd.get_pixel(22, 44))
print("inside the text:   %04x" % lcd.get_pixel(24, 46))
print("the blue box:      %04x" % lcd.get_pixel(400, 200))
print("the outline:       %04x" % lcd.get_pixel(22, 182))
print("the line:          %04x" % lcd.get_pixel(260, 320))
lcd.save_ppm("/data/local/tmp/fb.ppm")
print("wrote the picture")
