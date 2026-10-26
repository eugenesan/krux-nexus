# BCM2079x NCI transport — complete contract

Everything needed to talk to the NFC chip from bare Linux, recovered
2026-10-18. **The HAL source is on AOSP**, in
`platform/external/libnfc-nci`, path `halimpl/bcm2079x`. We were looking in
the wrong place (`src/hal/`) earlier; the real implementation is under
`halimpl/bcm2079x/`.

Fetched into `b/nfc/halimpl/` — see `fetch-aosp.sh`, run as
`./fetch-aosp.sh master halimpl/bcm2079x/<path>`.

## Verification

**The fetched source is the same version as the HAL binary on the device.**
Not assumed — checked. In `nfc_hal_nci_add_nfc_pkt_type` the C source says
`*p = HCIT_TYPE_NFC;`. The device disassembly of the same function says:

```asm
10428:  movs  r1, #16          @ 0x10
1042a:  strb  r1, [r0, #8]
```

and the packet-type dispatch in `my_read`:

```asm
16300:  cmp   r6, #16          @ HCIT_TYPE_NFC   -> read 3 more bytes
16302:  beq   16332
16304:  cmp   r6, #4           @ HCIT_TYPE_EVENT -> read 2 more bytes
16306:  bne   1639c           @ unknown type
16308:  movs  r4, #2
16332:  movs  r4, #3
```

which is byte-for-byte what `userial_linux.c` does. Source and binary agree,
so this can be ported by reading code rather than by reversing 23,000 lines of
disassembly.

## The two file descriptors

`USERIAL_Open()` opens **two** fds:

```c
linux_cb.sock = open(device_name, O_RDWR | O_NOCTTY);
...
linux_cb.sock_power_control = linux_cb.sock;        /* same fd by default */
if (separate power device configured)
    linux_cb.sock_power_control = open(power_control_dev, O_RDWR | O_NOCTTY);
```

Both are `/dev/bcm2079x` on this device — the literal string is in the HAL
binary's `.rodata`.

**`sock_power_control` is a file descriptor, not a string.** The `linux_cb.`
prefix in disassembly reads like a type name, but the field is `int` and is
passed straight to `ioctl()`. Do not go looking for a string table.

## Power and wake

```c
ioctl(linux_cb.sock_power_control, BCMNFC_POWER_CTL, 1);   /* power on  */
ioctl(linux_cb.sock_power_control, BCMNFC_POWER_CTL, 0);   /* power off */
ioctl(linux_cb.sock_power_control, BCMNFC_WAKE_CTL,  state);/* wake       */
```

Wake polarity at power-on: `NFC_WAKE_ASSERTED_ON_POR == UPIO_OFF`.

The six ioctls (`_IO(0xFA, nr)`) are in `include/bcm2079x.h`:

| nr | name | use |
|---|---|---|
| 0x01 | `POWER_CTL` | chip power |
| 0x02 | `CHANGE_ADDR` | `bcmi2cnfc_client_addr` |
| 0x03 | `READ_FULL_PACKET` | single packet |
| 0x04 | `SET_WAKE_ACTIVE_STATE` | |
| 0x05 | `WAKE_CTL` | wake assert/deassert |
| 0x06 | `READ_MULTI_PACKETS` | `bcmi2cnfc_read_multi_packets` |

## Packet framing — CONFIRMED ON HARDWARE

**This section is no longer inferred.** Measured on the device 2026-10-18 with
the NFC HAL frozen, using `b/phase0/nfc-probe.c`. Raw bytes:

```
read 6 bytes:  10 60 00 02 00 01
framing: HCIT_TYPE_NFC (command), 4-byte header, declared len=2, actual body=2 (MATCH)
```

### The wire format

```
byte 0   HCIT packet type       0x10 = NFC, 0x04 = event
byte 1   MT | PBF | CID         NCI_MT_MASK 0xE0 >> 5, PBF 0x10 >> 4, CID 0x0F
byte 2   OID                    OID_MASK 0x3F
byte 3   payload length
byte 4+  payload
```

`NCI_MSG_HDR_SIZE` is **3** (`nci_defs.h:37`, "per NCI spec"). So the 4-byte
header `my_read()` consumes is **1 HCIT type byte + the 3-byte NCI header**, and
`pbuf[3]` — the last header byte, the one it uses as the length — is the NCI
length field. That is why the source comment says "255 + 4 byte header": a
maximum Type-A APDU response is 255 payload bytes plus a 4-byte header.

Message types: `MT_DATA 0x00`, `MT_CMD 0x20`, `MT_RSP 0x40`, **`MT_NTF 0x60`**,
`MT_CFG 0x80`.

### The captured packet

| byte | value | meaning |
|---|---|---|
| 0 | `0x10` | HCIT_TYPE_NFC |
| 1 | `0x60` | **MT_NTF**, PBF = 0, CID = 0 |
| 2 | `0x00` | OID = 0x00 |
| 3 | `0x02` | length = 2 |
| 4–5 | `00 01` | payload |

**The chip speaks first.** With no firmware downloaded and nothing sent by us,
powering the chip on produces a notification within a few seconds. Better news
than expected: bring-up does not need a blind write sequence to provoke a
response — the chip announces itself and we listen.

The OID/payload `00 00 / 00 01` is a Broadcom download-mode notification whose
meaning is not yet identified. `nfc_hal_dm_proc_msg_during_init()` is where it
would be consumed, and that file is sourced — so this is a source lookup, not
another capture.

### Also measured

- All four ioctls return OK on this driver revision: `POWER_CTL`, `WAKE_CTL`,
  `SET_CLIENT_ADDR`, `SET_WAKE_ACTIVE_STATE`.
- `open("/dev/bcm2079x", O_RDWR|O_NOCTTY)` succeeds **while the Android NFC HAL
  holds the device**. The driver permits multiple opens.
- **The driver delivers one whole framed packet per `read()`.** A single read of
  6 bytes returned exactly one packet, complete and correctly framed, with no
  splitting and no coalescing. The port can therefore do ordinary buffered
  reads instead of replicating the byte-at-a-time header discovery in
  `my_read()` — worth knowing before porting that logic.

### Side effect worth knowing

Freezing the HAL for 30 s **crashes the Android NFC framework service**
(`com.android.nfc`) through its watchdog:

```
NfcService: run: Watchdog triggered, aborting
F DEBUG   : android::nfcManager_doAbort ... NfcService$WatchDogThread.run
```

It restarts itself cleanly and `nfc.initialized` returns to `true`, and the HAL
resumes fine. But the freeze window is bounded by that watchdog, which means
**testing from TWRP is cleaner than testing from Android** — no watchdog, no
framework, nothing to restore. Worth weighing for Phase 2.

## Source coverage

220 exported functions in the device HAL. Measured against the fetched tree:

| | count | |
|---|---|---|
| present in fetched source | **180** | **81 %** |
| not found | 40 | 18 % |

The 40 that are missing are, in full:

- **Logging** — `LogMsg`, `LogMsg_0` … `LogMsg_6`, `ScrLog`,
  `InitializeGlobalAppLogLevel`, `InitializeProtocolLogLevel`,
  `ProtoDispAdapterDisplayNciPacket`, `ProtoDispAdapterUseRawOutput`,
  `BTDISP_*_LOCK`. Stub these out; they are debug output.
- **The `Hai*` wrapper** — `HaiOpen`, `HaiClose`, `HaiWrite`,
  `HaiPowerCycle`, `HaiInitializeLibrary`, `HaiTerminateLibrary`,
  `HaiCoreInitialized`, `HaiControlGranted`, `HaiPreDiscover`,
  `HaiGetMaxNfcee`. A convenience layer over the GKI/ProtoDispAdapter bus.
  Its job is done directly by the port.
- **NDEF tag formatting** — `DispCHO`, `DispNDEFMsg`, `DispSNEP`,
  `DispCET4Tags`, `DispRWT4Tags`, `DispRWI93Tag`, `DispT3TagMessage`,
  `DispHcp`, `DispLLCP`, and `crcChecksumCompute` /
  `crcChecksumVerifyIntegrity`. Smart-card readers formatting NDEF payloads.
  **Not wanted** — SeedKeeper is ISO-DEP APDU, not NDEF.
- **Minor** — `GKI_igetpoolbuf`, `gki_timers_is_timer_running`.

**None of the gap is load-bearing.** Everything needed for ISO-DEP — the
transport, the framing, the NCI message assembly (`nfc_hal_nci_assemble_nci_msg`,
`nfc_hal_nci_preproc_rx_nci_msg`), the power/wake state machine, firmware
download (`nfc_hal_dm.c`, 48 KB, plus `nfc_hal_dm_cfg.c`) — is sourced.

## What this changes

Phase 3 was scoped as "reverse-engineer the transport from 23,249 lines of
disassembly". It is not. It is: **compile and trim ~180 functions of existing
Apache-2.0 Broadcom code**, replacing the logging with stubs, dropping the
`Hai*` indirection, and deleting the NDEF formatting.

Risk 1 from the original scoping is now **retired**: the driver does deliver
whole packets per `read()`, and the framing is confirmed on real hardware.

Risk 2 stands and is now the whole of the remaining transport work: firmware
download (`nfc_hal_dm.c`, 49 KB + `nfc_hal_prm.c`, 46 KB, plus the two `.ncd`
blobs). But the capture changed its shape — the chip sends a notification
unprompted on power-on, so bring-up is a state machine driven by real traffic,
not a blind write sequence.

## Still unknown

- **What the chip's first notification means.** `10 60 00 02 00 01` =
  MT_NTF, OID 0x00, payload `00 01`. Identifiable by reading
  `nfc_hal_dm_proc_msg_during_init()`, which is sourced.
- **APDU exchange capture.** Confirmed the phone detects an ISO-DEP/NfcA tag
  (8 detections on 2026-10-18), but there is no shell path to `IsoDep` —
  `cmd nfc` exposes only UWB `status`, and the binder service is `INfcAdapter`
  (enable/disable), with no tag IO. Driving a real transceive needs an app. Not
  required for the port; wanted as a reference trace.
- **Whether the port should run from TWRP rather than Android.** Freezing the
  HAL crashes the framework NFC watchdog, so Android is an awkward place to
  test. Recovery is the real target environment anyway.
