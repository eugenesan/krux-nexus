# Working Krux set

Pulled from the original Nexus 5 (`0643efdcf0eca63a`), `/data/krux`, on
2026-11-05. This is the last known-good firmware that ran the camera and
QR reliably on the original device.

The Python files here are the reference. `krux/src` mirrors the
camera/QR-related files from this set. Settings, seeds, logs, sockets
and binaries are intentionally not included.

Deploy to a stub device:

    scripts/deploy-working-krux.sh [serial]
