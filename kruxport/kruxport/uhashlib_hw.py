"""
uhashlib_hw -- the hardware accelerated hashes, on Linux.

The K210 boards have a hardware SHA-256; the MaixPy firmware asks for
it as `uhashlib_hw`.  There is no such hardware here, so this is the
software one under the name the firmware uses.
"""

from uhashlib import *  # noqa: F401,F403
from uhashlib import sha1, sha256, sha512  # noqa: F401
