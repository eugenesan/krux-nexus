# Build-time air-gap changes for LGE hammerhead / LineageOS 23.2

This is the part that actually matters. Layers L0–L4 below are compiled into the
image and survive reflash; the runtime script is only an audit tool.

All paths are relative to the LineageOS source tree.

> **Verified against the shipped ROM**
> `lineage-23.2-20260926-UNOFFICIAL-hammerhead_gms_go_2gb`, kernel
> `3.4.113-g11370a8eb99`, using `/proc/config.gz` from the live device rather
> than the in-tree defconfig. Where the two disagree, the device wins.
> The current profile lives in `../standalone/tools/profiles/hammerhead.list`. There is a script
> for this — see section **B1** and `build/build-kernel.sh`.

> **Reflash warning specific to this ROM.** This is not a stock-layout ROM.
> The XDA instructions require running a repartition script first
> (`hh_repart_v_16gb -m` or `hh_repart_v_32gb -m`), which moves ~1.1 GB from
> userdata into system and **wipes userdata**. Reverting the partition layout
> is a separate manual step (`-r`). A plain "flash the zip" does not restore
> the stock layout. Budget for this in your revert plan.

---

## L0 — kernel: do not build the radio drivers

Edit `arch/arm/configs/lineageos_hammerhead_defconfig`
(kernel/lge/hammerhead). Measured state from the shipped `/proc/config.gz`:

| line | current | action |
|------|---------|--------|
| 406 | `CONFIG_LGE_BLUETOOTH=y` | `# CONFIG_LGE_BLUETOOTH is not set` |
| 1245 | `CONFIG_WIRELESS=y` | not set |
| 1246–1247 | `CONFIG_WEXT_CORE=y`, `CONFIG_WEXT_PROC=y` | not set — this is the `SIOCSIWSCAN` surface used by every Wi-Fi scanner app, and `/proc/net/wireless` exists on the device today |
| 1248 | `CONFIG_CFG80211=y` | `# CONFIG_CFG80211 is not set` |
| 1249 | `CONFIG_NL80211_TESTMODE=y` | not set |
| 1252/1254/1255 | `CONFIG_CFG80211_DEFAULT_PS/_INTERNAL_REGDB/_WEXT` | not set |
| 1261–1264 | `CONFIG_RFKILL{,_PM,_LEDS}=y` | **keep** — pure software switch with no driver behind it; keeps `airgap.sh verify` able to report |
| 1271 | `CONFIG_NFC_BCM2079X=y` | **KEEP — see note below** |
| 1568 | `CONFIG_USB_NET_QMI_WWAN=y` | not set (USB tethering) |
| 1565–1572 | `CONFIG_USB_NET_{ZAURUS,CX82310_ETH,KALMIA,INT51X1,RNDIS_HOST,MCS7830,NET1080,PLUSB,CDC_SUBSET,CDCETHER,CDC_NCM,CDC_EEM,AX8817X,DM9601,GL620A,SMSC75XX,SMSC95XX}`, `CONFIG_USB_HSO`, `CONFIG_USB_IPHETH`, `CONFIG_USB_SIERRA_NET` | not set. **18 drivers are enabled, not the 7 I originally listed.** Note four of them are `CONFIG_USB_*`, not `CONFIG_USB_NET_*`. |
| 1221–1227 | `CONFIG_BT`, `BT_RFCOMM`, `BT_RFCOMM_TTY`, `BT_BNEP`, `BT_BNEP_*`, `BT_HIDP` | not set. RFCOMM is OBEX file transfer and BNEP is PAN IP-over-BT — both are textbook air-gap exfiltration channels, worth naming explicitly in your threat model. |
| 1238 | `CONFIG_BT_MSM_SLEEP=y` | not set |
| 1121–1122 | `CONFIG_BRIDGE=y`, `BRIDGE_IGMP_SNOOPING` | not set (bridging) |
| 899/1090 | `CONFIG_BRIDGE_NETFILTER=y`, `BRIDGE_NF_EBTABLES` | not set |
| 1511 | `CONFIG_MSM_RMNET_BAM=y` | not set — this is what creates the dormant `rmnet0..7` / `rev_rmnet0..8` netdevs |
| 440–460 | `CONFIG_MSM_SMD{,_PKG4,_QMI,_PKT,_NMEA}`, `MSM_IPC_ROUTER*`, `MSM_QMI_INTERFACE` | not set — no modem on this device, so the whole SMD/QMI transport is dead code. **Risk:** `MSM_SMD_PKG4` also backs `qcom,smd-rpm` and `CONFIG_MSM_SUBSYSTEM_RESTART`, which `init.hammerhead.rc` writes to. If ADSP or audio misbehaves after the first build, re-enable just this one symbol; nothing radio-related depends on it. |
| 2903 | `CONFIG_MODEM_SUPPORT=y` | not set |

### NFC is deliberately kept

The payload application talks to NFC smartcards, and physical access is in
scope. So `CONFIG_NFC_BCM2079X` **stays enabled**, along with:

- `/vendor/firmware/bcm2079x-b5_firmware.ncd`
- `/vendor/firmware/bcm2079x-b5_pre_firmware.ncd`
- `android.hardware.nfc-service-bcm` and the NFC HAL in the manifest

Carry this into the threat model rather than leaving it implicit:
`android.permission.NFC` is a **`normal`** permission (measured — see
`recovery/README.md` §12), so it is granted at install with no prompt. Your app
holding NFC is deliberate and user-visible in the permission list; a
*malicious* app would receive it silently. The property that actually holds is
that no third-party app can be installed at all.

Do **not** touch: `CONFIG_QSEECOM` (keystore/lock screen), and the
`CONFIG_MSM_PIL*` audio/video children — `LPASS_QDSP6V5`, `MSS_QDSP6V5`,
`VENUS` are audio and camera DSPs, not radios.

> **Note on `CONFIG_USB_NET_RNDIS_HOST`.** adb on this device does *not* use
> RNDIS or any netdev — it rides the USB gadget function
> (`configfs` `ffs.adb`, `/dev/android_usb/android0`). There is no `usb0` and
> no `adb0` in `/sys/class/net`. Removing RNDIS therefore does not break adb,
> and you can block every non-loopback interface at netfilter and still talk
> to the device. Verified on the live unit.

Automate with `standalone/tools/apply-defconfig.py` (device-agnostic):

```sh
python3 standalone/tools/apply-defconfig.py \
    kernel/lge/hammerhead/arch/arm/configs/lineageos_hammerhead_defconfig \
    standalone/tools/profiles/hammerhead.list -i --check \
    --kernel-root kernel/lge/hammerhead
```

Measured result on the real defconfig: 78 lines removed, no profile symbol
left `=y`/`=m`, `CONFIG_RFKILL` and `CONFIG_QSEECOM` preserved.

### L0b — Device tree: stop the controllers being enumerated

`arch/arm/boot/dts/msm8974-hammerhead/msm8974-hammerhead-nfc.dtsi` already has
`bcm2079x@77 { status = "disable" }` — verify it is not re-enabled by a
revision file. Additionally:

- Remove/disable the SDIO host node that carries the BCM4339 (whichever of
  `&sdc1` / `&sdc2` your board uses — confirm from the live device:
  the WiFi is on SDCC2 on hammerhead, USB is SDCC1).
- Disable `uart10` (`msm8974-lge-common.dtsi:38`) — the Bluetooth HCI UART —
  and the `qcom,tx/rx/cts/rfr-gpio` GPIOs 53–56, so `LGE_BLUETOOTH`'s
  probe cannot even find its pins. Measured bound drivers today:
  `bluetooth_rfkill`, `msm_serial_hs` (holds `f9960000.uart`),
  `msm_serial_hsl` (holds `f991d000.serial`).

**Do not try to power-cycle the Wi-Fi chip from software.** There is no WiFi
regulator node in `msm8974-hammerhead-regulator.dtsi` — it only defines the
ANX7808 HDMI bridge rails. The BCM4339 sits on a permanently-enabled rail, so
there is no GPIO or regulator to cut. Once `CONFIG_BCMDHD=n` the chip simply
never receives firmware and never comes out of reset; the SDIO controller will
still enumerate it. Removing the SDIO host node is the strongest in-kernel
measure available. Anything beyond that is a hardware mod (cut the antenna
feed or the rail).

---

## L2 — do not ship radio firmware

`device/lge/hammerhead/wlan/bcm.mk` — delete the whole file and remove its
`inherit-product-if-exists` from `device.mk`. That drops:

- `fw_bcmdhd.bin`, `fw_bcmdhd_apsta.bin`
- `/vendor/etc/wifi/bcmdhd.cal` (contains the factory NVRAM: MAC address, cal)
- `wpa_supplicant_overlay.conf`, `p2p_supplicant_overlay.conf`

`device/lge/hammerhead/bluetooth/bcm.mk` — delete and remove the inherit. That
drops `bcm4335c0.hcd`, the `bluetooth_loader` MAC-loader binary, and the
`bluetooth.*` property set.

From `device/lge/hammerhead/device.mk`, remove the `PRODUCT_COPY_FILES` for:

- ~~`bcm2079x-b5_firmware.ncd`~~ — **keep**, NFC is in use
- `android.hardware.wifi.xml`, `android.hardware.wifi.direct.xml`
- `android.hardware.bluetooth_le.xml`
- `android.hardware.telephony.gsm.xml`, `android.hardware.telephony.cdma.xml`,
  `android.hardware.telephony.ims.xml`
- `android.software.sip.voip.xml`, `android.software.ipsec_tunnels.xml`

> Removing the FEATURE_FLAG xml files is what stops `pm list features` from
> advertising `android.hardware.wifi` to apps. This is the piece that makes a
> well-behaved app give up on networking gracefully instead of hanging.

Also in the vendor tree `android_vendor_lge/hammerhead`, drop:

- `proprietary/vendor/bin/qmuxd`, `libqmi*`, `libril-qc-qmi-1.so`,
  `libril-qcril-hook-oem.so`, `libnetmgr.so`, `netmgrd`, `nl_listener`,
  `usbhub*`, `bridgemgrd`, `diag_klog`, `diag_mdlog`, `subsystem_ramdump`,
  `vss_init`, `framework/qcrilhook.jar`, `framework/serviceitems.jar`,
  `etc/qcril.db`, `etc/permissions/{qcrilhook,serviceitems}.xml`,
  `app/qcrilmsgtunnel`, `libshim_ril`

---

## L3 — `BoardConfig.mk` and `init.hammerhead.rc`

In `device/lge/hammerhead/BoardConfig.mk`:

```diff
-BOARD_HAVE_BLUETOOTH := true
+# BOARD_HAVE_BLUETOOTH is not set

 # Wifi related defines
-WPA_SUPPLICANT_VERSION      := VER_0_8_X
-BOARD_WLAN_DEVICE           := bcmdhd
-BOARD_WPA_SUPPLICANT_DRIVER := NL80211
-BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
-BOARD_HOSTAPD_DRIVER        := NL80211
-BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
-WIFI_DRIVER_FW_PATH_PARAM   := "/sys/module/bcmdhd/parameters/firmware_path"
-WIFI_DRIVER_FW_PATH_AP      := "/vendor/firmware/fw_bcmdhd_apsta.bin"
-WIFI_DRIVER_FW_PATH_STA     := "/vendor/firmware/fw_bcmdhd.bin"
-WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
```

In `device/lge/hammerhead/init.hammerhead.rc`, delete:

- the `on boot` Bluetooth block (rfkill chown, `/proc/bluetooth/sleep/*`,
  `msm_serial_hs.0` clock)
- the `mkdir /dev/socket/qmux_{radio,audio,bluetooth,gps}` lines
- the `chown ... /sys/module/bcmdhd/parameters/{firmware,nvram}_path` lines
- the `hsicctl0/modem_wait` chown
- the `rmnet0..7` `accept_ra` writes
- the modem SSR `restart_level` write

In `fstab.hammerhead`, drop the `modem` → `/firmware` and `modem` → `/radio`
mount lines (there is no modem on this device).

Add at the very top of the init rc, inside `on early-init`:

```rc
# --- AIR GAP -------------------------------------------------------------
# Fail closed: block every rfkill before the framework probes anything.
on early-init
    start airgap-kill
# -------------------------------------------------------------------------
```

with a dedicated service in a new `init.airgap.rc`:

```rc
service airgap-kill /system/bin/sh /vendor/bin/airgap-kill.sh
    class core
    user root
    group root
    oneshot
    disabled

on property:sys.boot_completed=1
    start airgap-kill
```

and `airgap-kill.sh` = the core of `runtime/airgap.sh` (phases 1–3), copied to
`/vendor/bin/`. Doing it at `early-init` (before system_server) and again at
`boot_completed` closes both windows.

---

## L4 — `device.mk` package removals

From `PRODUCT_PACKAGES` in `device/lge/hammerhead/device.mk` remove:

```
android.hardware.wifi-service-lazy   wpa_supplicant   hostapd   wificond
libwpa_client   iw_vendor   wireless-regdomain.sh
WifiOverlay     TetheringConfigOverlay
android.hardware.bluetooth@1.0-service   android.hardware.bluetooth@1.0-impl
android.hardware.bluetooth.audio@2.0-impl   audio.bluetooth.default
libbt-vendor   bluetooth_loader
Iwlan   QualifiedNetworksService   PhhIms
android.hardware.radio@1.4-service.hammerhead
android.hardware.radio.config@1.1-service.hh
android.hardware.nfc@1.0-service   android.hardware.nfc@1.0-impl-bcm   Tag
```

Remove the `soong_config_set` lines for `brcm_libbt`.

Remove the `overlay-lineage` / `rro_overlays/{WifiOverlay,TetheringOverlay}`
targets and the ~90 `values-mccXXX` overlays plus the rro `CarrierConfigOverlay`.

Remove `PRODUCT_PROPERTY_OVERRIDES` for `wifi.interface`, `persist.wifi.regdomain.override`,
and every `persist.radio.*` / `ro.ril.*` / `ro.telephony.*` entry.

Then delete `libshims/` entirely (`libshim_ril` only exists to serve the
`vendor.rild.libpath` blob you just deleted).

### L4a — telephony, and why the earlier "no modem" note was wrong

The Nexus 5 **does have a baseband**, and it is powered at every boot. Measured
on the live device as root: `pil-q6v5-mss` bound, `/dev/subsys_modem`,
`/dev/smdcntl0..7`, `/dev/smd_cxm_qmi`, ~45 MB of modem firmware in
`/firmware/image/modem.*`, `rmnet0..8`, a SIM read over UIM
(`gsm.sim.state=LOADED`, AT&T `310280`), `vendor.ril-daemon` running, and
`me.phh.ims` for VoLTE. Full correction in `../README.md` §6.2.

It has never registered on a network in any test (`NOT_REG_MT_SEARCHING_OP`,
`REG_DENIED`, no PDP, no address on any `rmnet*`). **That is not a security
control.** Emergency calling is permitted to transmit without registering, so a
powered baseband holding a valid SIM is exactly the state in which a spoofed
emergency call could put a bearer on the air. L4a is therefore mandatory, not
optional — the runtime lockdown only sets airplane mode, which is a setting, and
a setting is not the same as an absent radio.

Remove, additionally:

```
com.android.providers.telephony     TelephonyProvider  - also owns /data/system/users/0/telephony* DBs
com.android.server.telecom          Telecom
com.android.dialer                  Dialer
com.android.telephony.qns           QualifiedNetworksService
ImsServiceEntitlement
vendor.ril-daemon                   the rild binary itself
libshim_ril                         (already covered above)
```

And in the **vendor** tree, which is a separate partition:

```
/vendor/bin/hw/android.hardware.radio@1.4-service.hammerhead
/vendor/bin/hw/android.hardware.radio.config@1.1-service.hh
/vendor/etc/init/android.hardware.radio@1.4-service.hammerhead.rc
/vendor/etc/init/android.hardware.radio.config@1.1-service.hh.rc
```

plus their VINTF entries in `/vendor/etc/vintf/manifest.xml` (`android.hardware.radio`,
`.config`, `.deprecated`) — see L5. Removing the binary without removing the
manifest entry is a **bootloop**, because the framework blocks waiting on a HAL
that will never register.

The baseband itself is stopped in two places:

- **kernel**: `CONFIG_MSM_PIL`, `CONFIG_MSM_PIL_MSS_QDSP6V5`,
  `CONFIG_MSM_PIL_LPASS_QDSP6V5`, `CONFIG_MSM_RMNET_BAM`, `CONFIG_USB_NET_QMI_WWAN`.
  Without `MSS_QDSP6V5` the modem firmware is never loaded into the MSS
  processor, so no baseband runs regardless of what userspace asks for.
- **firmware**: `/firmware` is its own **vfat partition**, so it cannot be
  touched from a recovery zip. To actually strip `modem.mdt`/`modem.b00..b24` you
  need `fastboot`/`dd` to the partition, or ship a modified `firmware` image.
  Removing only the userspace half leaves a baseband that boots, scans and
  cannot be reached — better than nothing, but not the same as not having one.

The honest summary: **you can remove all of the above from a rebuilt ROM, and
you can remove most of it from a TWRP zip, but removing the modem firmware
requires repartition-level access.**

### What you cannot remove (and why it is fine)

`com.android.wifi` and `com.android.bluetooth` ship as **APEX**
(`com.android.wifi`, `com.android.bluetooth` in `apex/`), not as device
`PRODUCT_PACKAGES`. Deleting an APEX will fail the build and break the
framework. Their behaviour is gated at runtime by the HAL service being
absent — with `android.hardware.wifi-service-lazy` gone and no
`wifi@1.0-supplicant`/`wifi@1.0-passthrough` HAL in the VINTF manifest,
Wi-Fi never comes up. Leave the APEX alone.

---

## L5 — VINTF manifest

`device/lge/hammerhead/manifest.xml`: delete the
`android.hardware.bluetooth` and `android.hardware.nfc` `<hal>` blocks.
`android.hardware.radio` / `radio.config` / `radio.deprecated` can stay or go;
they are inert with no modem, but removing them makes `SystemServer` skip
telephony entirely.

---

## B1 — building only the kernel (no ROM rebuild)

**You do not need an Android source tree.** That is the common misconception and
it is what makes people think a kernel build is a heavy job. A kernel build needs
two git repositories and nothing else:

| What | Repo | Size |
|---|---|---|
| kernel | `LineageOS/android_kernel_lge`, branch `lineage-18.1`, `android_kernel_lge_hammerhead` | ~1.5 GB |
| toolchain | `LineageOS/android_prebuilts_gcc_linux-x86` (only the `arm` subtree) | ~2 GB |
| build output | — | ~5 GB |

**Realistic total: ~10 GB and 20–40 minutes**, not the 30 GB I said earlier —
that figure was for a full ROM build and I should not have let it stand next to a
kernel.

There is no device tree, no vendor blobs, no AOSP `frameworks/`, no `soong`, no
`envsetup.sh` in this path. Those are needed to produce a **ROM**; a kernel is
just a cross-compile.

```sh
git clone -b lineage-18.1 --depth 1 \
    https://github.com/LineageOS/android_kernel_lge android_kernel_lge_hammerhead
git clone -b lineage-18.1 --depth 1 \
    https://github.com/LineageOS/android_prebuilts_gcc_linux-x86 prebuilts

./build/build-kernel.sh --tree ./android_kernel_lge_hammerhead --prebuilts ./prebuilts
```

`build/build-kernel.sh` wraps the whole thing: locate tree → locate toolchain →
`hammerhead_defconfig` → apply `standalone/tools/profiles/hammerhead.list` → verify nothing
re-enabled itself → compile → print the config it produced.

Full option list — `./build-kernel.sh --help`:

```sh
--tree PATH        kernel tree; auto-detected from a few well-known locations
--prebuilts PATH   AOSP prebuilts root; auto-detected
--out PATH         build output directory (default: ./out)
--jobs N           parallelism (default: nproc)
--config           apply the profile and stop, no compile (~1 min) -- do this first
--check            verify tree and toolchain, change nothing at all
--install-to DIR   copy the finished Image into a device tree's kernel/ dir
-h, --help         usage
    --selftest     verify help text, parser and profile agree, then exit
```

`--selftest` exists because this file, the script's header comment and the
script's actual arguments disagreed three ways at one point. It diffs the
options the parser really implements against the documented set and exits
non-zero on any drift. Run it after touching either script.

### Two things that will bite you

**The kernel is 32-bit ARM, not arm64.** The MSM8974 is 64-bit-capable, so it is
natural to assume `arm64` and then pick the wrong toolchain. Measured from
`/proc/config.gz` on the live device: `CONFIG_ARM=y`, and neither `CONFIG_ARM64`
nor `CONFIG_64BIT` appears. The cross prefix is `arm-linux-androideabi-`, not
`aarch64-linux-android-`.

**One symbol matters most, and two of its siblings must survive.**

```kconfig
CONFIG_MSM_PIL                    # framework — KEEP
  CONFIG_MSM_PIL_MSS_QDSP6V5      # the baseband — REMOVE  <- the whole point
  CONFIG_MSM_PIL_LPASS_QDSP6V5    # the audio DSP — KEEP or you lose audio
  CONFIG_MSM_PIL_VENUS            # the Venus GPU — KEEP
```

`CONFIG_MSM_PIL_MSS_QDSP6V5` is the only kernel symbol that stops the modem
firmware in `/firmware/image` from being loaded into the MSS/baseband processor.
Remove it and the baseband never comes out of reset, whatever user space does.
The profile marks the other two as protected, and the build script prints their
final values so you can see they survived.

### Why the baseband strip is Tier 1 and the SMD/IPC strip is commented out

`standalone/tools/profiles/hammerhead.list` deliberately does **not** strip
`CONFIG_MSM_SMD`, `CONFIG_MSM_IPC_ROUTER_SMD_XPRT`, `CONFIG_MSM_QMI_INTERFACE`
or `CONFIG_MSM_SUBSYSTEM_RESTART`. Those are not modem-private: the same SMD
channel carries QMI for WLAN, GNSS, sensors and the DSP. Stripping them is
tidier, buys no security once the firmware is not loading, and risks breaking
GPS and sensors. The reasoning is in the profile; the lines are there, commented,
if you want to bisect.

### Flashing it

A kernel is not flashable on its own. Either `--install-to` the device tree and
build a full ROM (the expensive path), or unpack the existing `boot.img` from
your ROM zip and swap the `Image` — which is the repack path from §Q4 and keeps
everything else exactly as it was.

After flashing, check in this order:

```sh
ls /sys/bus/platform/drivers | grep pil-q6v5-mss    # expect NOTHING
dmesg | grep -iE 'pil-|baseband|subsys_modem'      # expect NOTHING
zcat /proc/config.gz | grep PIL_MSS                 # expect "# ... is not set"
# then: audio works? GPU alive? GPS working?        # LPASS, Venus, SMD
```

## Build & flash

```sh
source build/envsetup.sh
lunch lineage_hammerhead-userdebug     # userdebug: you need root for airgap.sh
mka bacon
```

Verify the ROM is clean without flashing it:

```sh
unzip -l out/target/product/hammerhead/lineage_hammerhead-*-userdebug.zip
# expect NO match for:
#   fw_bcmdhd|bcm4335c0\.hcd|bcmdhd\.cal|bcm2079x|wpa_supplicant|hostapd|wificond
```

That `unzip -l | grep` is the cheapest possible regression gate. Put it in CI.