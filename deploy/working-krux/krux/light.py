"""
light -- the port's version.

The K210 boards have a white LED on a GPIO (the firmware's light.py
drives it through MaixPy's GPIO and the pin muxer).  The hammerhead has
no such LED: its camera's flash is not a light the firmware can switch,
and the display's backlight is the panel's, not a GPIO.

So the port's light does nothing: the firmware's Light singleton is
here so that the pages that use it (the address confirmation, the
mnemonic generation) keep working, and the "light" they turn on is
just the screen.
"""

import board
from .kboard import kboard

TORCH = "/sys/class/leds/led:flash_torch/brightness"
TORCH_MAX = "/sys/class/leds/led:flash_torch/max_brightness"


def _write(path, value):
    try:
        with open(path, "w") as f:
            f.write(str(value))
    except Exception:
        pass


class IOCircuit:
    """The protocol for a multifunctional IO"""

    def __init__(self, board_io_pin=None, kendryte_gpio=None, maixpy_gpio=None):
        self.circuit = None
        self.turn_off()

    def turn_on(self):
        """Turns on the circuit: there is none, so nothing happens."""

    def turn_off(self):
        """Turns off the circuit: there is none, so nothing happens."""


class Light(IOCircuit):
    """Light is a singleton interface for interacting with the device's LED light"""

    def __init__(self):
        super().__init__()
        self.on = False

    def turn_on(self):
        # Nexus 5: the camera flash's torch node.  Half brightness is
        # enough for a nearby QR code and avoids heating the LED.
        level = 100
        try:
            with open(TORCH_MAX, "r") as f:
                maximum = int(f.read().strip())
            if maximum > 0:
                level = maximum // 2
        except Exception:
            pass
        _write(TORCH, level)
        self.on = True

    def turn_off(self):
        _write(TORCH, 0)
        self.on = False

    def toggle(self):
        if self.on:
            self.turn_off()
        else:
            self.turn_on()


light = Light()
