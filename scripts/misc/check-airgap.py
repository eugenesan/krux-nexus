#!/usr/bin/env python3
"""
check-airgap.py -- fail if the standard image can do wireless comms.

The standard image is allowed exactly one wireless interface: the NFC
reader (CONFIG_NFC_BCM2079X, /dev/bcm2079x).  Wi-Fi, Bluetooth, the
cellular modem and their userspace helpers are not part of it.

Two layers are checked:

  * the kernel config -- no WLAN/CFG80211/BCMDHD, no BT/RFCOMM/BNEP/HIDP,
    no MSS baseband/RMNET/QMI-WWAN, no FM radio tuner drivers;
  * the ramdisk -- no wpa_supplicant/hostapd/wificond, no Broadcom Wi-Fi
    or Bluetooth firmware, no Bluetooth HCI helpers.

This is a build gate, not a proof about the phone's other partitions.
The Android system partition may still contain such files; it is not
part of this image, and without the kernel drivers those files cannot
communicate.  The NFC driver is deliberately kept.

Usage:
    python3 scripts/misc/check-airgap.py \
        --config artifacts/built/ko/.config \
        --ramdisk artifacts/built/ramdisk-bundled
"""
import argparse
import os
import re
import shlex
import subprocess
import sys

# An ON symbol is forbidden when its name matches one of these.  The
# patterns are anchored so CONFIG_BT does not catch CONFIG_BTRFS_FS.
FORBIDDEN_SYMBOLS = [
    r"^CONFIG_WLAN$",
    r"^CONFIG_WIRELESS$",
    r"^CONFIG_WEXT",
    r"^CONFIG_CFG80211",
    r"^CONFIG_MAC80211",
    r"^CONFIG_NL80211",
    r"^CONFIG_BCMDHD",
    r"^CONFIG_BCM433",
    r"^CONFIG_BRCMFMAC",
    r"^CONFIG_BT$",
    r"^CONFIG_BT_",
    r"^CONFIG_BLUETOOTH",
    r"^CONFIG_LGE_BLUETOOTH",
    r"^CONFIG_RFCOMM",
    r"^CONFIG_BNEP",
    r"^CONFIG_HIDP",
    r"^CONFIG_MSM_PIL_MSS_QDSP6V5$",
    r"^CONFIG_MSM_RMNET",
    r"^CONFIG_USB_NET_QMI_WWAN$",
    r"^CONFIG_USB_HSO$",
    r"^CONFIG_USB_IPHETH$",
    r"^CONFIG_USB_SIERRA_NET$",
    r"^CONFIG_WIMAX",
    r"^CONFIG_MHI",
    r"^CONFIG_QRTR",
    r"^CONFIG_RADIO_(SI|TEA|SAA|TEF|WL|IRIS)",
]
FORBIDDEN_RE = [re.compile(pattern) for pattern in FORBIDDEN_SYMBOLS]

# The one wireless interface we keep.
REQUIRED = {
    "CONFIG_NFC_BCM2079X": "y",
}

# Userspace names that would mean a wireless stack is present.
BAD_RAMDISK_NAMES = {
    "wpa_supplicant",
    "wpa_supplicant.conf",
    "wpa_cli",
    "hostapd",
    "wificond",
    "bcmdhd",
    "fw_bcmdhd.bin",
    "fw_bcmdhd_apsta.bin",
    "brcmfmac",
    "brcm_patchram",
    "bluetoothd",
    "btattach",
    "btmgmt",
    "bluetoothctl",
    "hciattach",
    "hcitool",
    "hciconfig",
    "rfcomm",
}


def read_config(path):
    values = {}
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
            if m:
                values[m.group(1)] = m.group(2)
                continue
            m = re.match(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$", line)
            if m:
                values[m.group(1)] = "n"
    return values


def forbidden(symbol):
    return any(pattern.search(symbol) for pattern in FORBIDDEN_RE)


def ramdisk_entries(path):
    if not os.path.isfile(path):
        raise SystemExit("check-airgap: no ramdisk at %s" % path)
    command = "gzip -dc %s | cpio -it 2>/dev/null" % shlex.quote(path)
    out = subprocess.run(
        command, shell=True, check=True, stdout=subprocess.PIPE, text=True
    ).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--config", required=True, help="kernel .config")
    parser.add_argument("--ramdisk", required=True, help="compressed cpio ramdisk")
    args = parser.parse_args()

    failures = []
    values = read_config(args.config)

    for symbol, wanted in REQUIRED.items():
        got = values.get(symbol, "n")
        if got not in ("y", "m"):
            failures.append("kernel: %s is %s, expected %s" % (symbol, got, wanted))

    for symbol, value in sorted(values.items()):
        if value not in ("y", "m"):
            continue
        if forbidden(symbol):
            failures.append("kernel: forbidden wireless/radio symbol %s=%s" % (symbol, value))

    entries = ramdisk_entries(args.ramdisk)
    for entry in entries:
        name = os.path.basename(entry).lower()
        if name in BAD_RAMDISK_NAMES:
            failures.append("ramdisk: %s" % entry)
        elif re.match(r"^(bcm433|brcmfmac|fw_bcmdhd)", name):
            failures.append("ramdisk: %s" % entry)

    if failures:
        print("check-airgap: FAIL")
        for item in failures:
            print("  !!", item)
        return 1

    print("check-airgap: PASS")
    print("  kernel: no WLAN/BT/cellular/FM drivers (%d symbols checked)" % len(values))
    print("  kernel: NFC_BCM2079X=y is the only wireless interface")
    print("  ramdisk: %d entries, no Wi-Fi/Bluetooth userspace or firmware" % len(entries))
    return 0


if __name__ == "__main__":
    sys.exit(main())
