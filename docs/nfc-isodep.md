# ISO-DEP layer — architecture and plan

Investigated 2026-10-18. **Good news first: the NFC stack we thought was gone
from public AOSP is not.** `platform/system/nfc` really is empty upstream
(Google moved the NFA implementation internal), but the whole stack we actually
need is in `platform/external/libnfc-nci`, which is public and which we have
already been fetching from.

## The stack, top to bottom

| layer | where | port? |
|---|---|---|
| SeedKeeper protocol | `krux/src/krux/seedkeeper.py` (pure Python) | reuse as-is |
| card contract | `kruxmobileapp/mocks/nfc.py` | **reuse the shape** |
| ISO-DEP (Type 4 tag) | `external/libnfc-nci/src/nfc/tags/rw_t4t.c` — 79 KB | **port** |
| discovery loop | `external/libnfc-nci/src/nfa/dm/nfa_dm_discover.c` — 117 KB | **port (trimmed)** |
| NCI messages | `external/libnfc-nci/src/nfc/nci/nci_hmsgs.c` | reference |
| Broadcom chip HAL | `external/libnfc-nci/halimpl/bcm2079x/` — 81 % sourced | **port** |
| kernel driver | `/dev/bcm2079x`, 7 ioctls, 4-byte framing | already proven |

Fetched into `b/nfc/stack/`.

## The key realisation: we do NOT implement ISO 14443-4

The NFC controller **is** the ISO-DEP stack. Anticollision, activation, the
block protocol, chaining of long APDUs — all of that lives in the silicon. Our
job is to hand it APDUs.

From `rw_t4t.c`:

```c
static BOOLEAN rw_t4t_send_to_lower (BT_HDR *p_c_apdu)
{
    if (NFC_SendData (NFC_RF_CONN_ID, p_c_apdu) != NFC_STATUS_OK)
        return FALSE;
    nfc_start_quick_timer (... RW_T4T_TOUT_RESP ...);
    return TRUE;
}
```

and an APDU being sent:

```c
p_c_apdu->offset = NCI_MSG_OFFSET_SIZE + NCI_DATA_HDR_SIZE;
p = (UINT8 *) (p_c_apdu + 1) + p_c_apdu->offset;

UINT8_TO_BE_STREAM (p, T4T_CMD_DES_CLASS);          /* 0x00 */
UINT8_TO_BE_STREAM (p, T4T_CMD_INS_GET_HW_VERSION);  /* 0xCA */
UINT16_TO_BE_STREAM(p, 0x0000);                      /* P1 P2 */
UINT8_TO_BE_FIELD (p, 0x00);                         /* Le */

p_c_apdu->len = T4T_CMD_MAX_HDR_SIZE;
```

**That is a bare ISO 7816 APDU — CLA INS P1 P2 Le — handed to `NFC_SendData`
and nothing else.** No GID, no OID, no proprietary wrapper. `NFC_SendData`
frames it as an NCI **DATA** packet (MT_DATA, CID = `NFC_RF_CONN_ID`) and the
chip does the rest.

Compare with what the device HAL does for the *chip*: `10 2F 04 00` — a
proprietary command. Two different planes, and ISO-DEP lives on the standard one.

### So the sequence is

```
1. HAL bring-up                 (we have it: power on -> Core Reset ntf ->
                                 GET_BUILD_INFO -> DM/PRM -> IDLE)
2. Discovery / poll loop        (port nfa_dm_discover.c, trimmed)
3. On tag detected: activate ISO-DEP, NFC_Connect(NFC_RF_CONN_ID)
4. APDU exchange:               NFC_SendData / NFC_RecvData, MT_DATA frames
5. SeedKeeper SELECT + INS      (reuse seedkeeper.py unchanged)
```

Steps 2–4 are the "ISO-DEP layer". Step 5 is already written and tested.

## What actually has to be written

Estimated against the fetched sources:

| piece | size | notes |
|---|---|---|
| discovery loop | **trim** `nfa_dm_discover.c` | keep poll-mode config and the tag-detected callback; drop NDEF, peer-to-peer, routing, battery-full handling |
| ISO-DEP activate | **small** | `NFC_Connect` + the activation exchange |
| data plane | **small** | MT_DATA framing, response reassembly, timeout |
| block/chaining | **medium** | responses > 255 bytes need reassembly — a SeedKeeper export can exceed this |
| APDU helpers | small | `T4T_CMD_*` constants and the same buffer discipline as `rw_t4t.c` |

`nfa_dm_discover.c` is 117 KB but most of it is NDEF, P2P, routing and
screen-state policy that a dedicated device has no use for. The actual poll
configuration is a small part of it.

## The card-side contract we must match

From `kruxmobileapp/mocks/nfc.py` — four methods:

```python
class CardTimeout(Exception): ...

class IsoDepTransport:
    def connect(self, timeout_ms=TAG_TIMEOUT_MS): ...
    def is_connected(self): ...
    def transmit(self, apdu): ...      # auto-connects if needed
    def close(self): ...
```

That is our port's API. The Android implementation (`NfcProvider.java`) uses
reader-mode flags worth copying:

```java
FLAG_READER_NFC_A | NFC_B | NFC_F | NFC_V
FLAG_READER_SKIP_NDEF_CHECK     <- skip NDEF type parsing, we never need it
FLAG_READER_NO_PLATFORM_SOUNDS
```

**But note what Android gives itself for free:** `IsoDep.get(tag)` hands you a
connected ISO-DEP object because the framework already resolved the tag and
activated it. Our port must do the discovery and activation itself — which is
steps 2–3 above.

## SeedKeeper specifics

From `krux/src/krux/seedkeeper.py`:

```python
SEEDKEEPER_AID = [0x53,0x65,0x65,0x64,0x4B,0x65,0x65,0x70,0x65,0x72]  # "SeedKeeper"
SELECT_APDU    = [0x00, 0xA4, 0x04, 0x00]
```

`select()` notes that a transport which already resolved the applet can skip
the SELECT — *"a raw one cannot"*. We are raw, so we send it. Consistent with
the Phase 0 capture: the tag came back as `[IsoDep, NfcA]` with no SeedKeeper
AID in the list.

Everything after that is ordinary APDU traffic, already implemented and tested
on the Android side.

## Risks

1. **Response length.** A SeedKeeper *export* can exceed 255 bytes of payload.
   The chip does the chaining, but we must reassemble correctly and know the
   real total. This is the most likely place to get it subtly wrong, and it is
   testable without a real card by feeding short APDUs first.

2. **Timeout behaviour.** `rw_t4t.c` uses `RW_T4T_TOUT_RESP`; we need one
   that does not hang the UI on a removed card. The `CardTimeout` contract
   expects a clean raise.

3. **Poll-mode tuning.** Getting discovery fast enough to feel responsive while
   not spinning the RF is empirical. It will need tuning against a real card.

4. **Activation edge cases.** A card that is present but slow, or a foreign
   card (a bank card, an ISO 14443-4 UICC), must produce a clean error rather
   than a hang.

None of these require the hardware to *start*; items 1 and 2 can be driven
against the existing `nfc-probe` once Phase 3's data plane exists.

## Build order

```
A. bring-up state machine complete   (DM/PRM, firmware .ncd)   — HAL
B. NCI data plane                    (SendData/RecvData)        — new, small
C. discovery + activation            (trimmed nfa_dm_discover)  — new, medium
D. IsoDepTransport adapter           (4 methods)               — new, small
E. SeedKeeper                       (seedkeeper.py)           — reuse
```

A is the only one still blocked on work, and its remaining pieces are sourced.
B–D are the "ISO-DEP layer" proper, and none of them is reverse engineering.

---

## Status 2026-10-28 — the layer works; the card needs a secure channel

This section is the measured outcome, not the plan above.  The bring-up,
discovery, activation and NCI data plane are **all working on the device**
against a real SeedKeeper JavaCard.  The first command that exercises the
card's applet, however, ends the RF link, and the card's own status shows
why the current plaintext protocol cannot finish the job on a stock card.

### What works, end to end

The chip is brought up with the vendor's replayed sequence (power cycle,
`GET_BUILD_INFO`, `CORE_RESET`, `CORE_INIT`, the `SET_CONFIG`s, routing and
`RF_DISCOVER`).  `RF_DISCOVER` (GID RF, OID 3) starts the poll and the chip
then activates the card by itself:

```
[tx]  10 21 03 0f 07 00 01 01 01 02 01 80 01 06 01 74 01 77 01   RF_DISCOVER
[rsp] 10 41 03 01 06                                             (6 = already active)
[ntf] 10 61 05 26 01 02 04 00 ff 01 0c 48 00 07 04 31 55 6a 99 15 90 01 20
                  00 00 00 0f 0e 78 77 71 02 50 56 4a 43 4f 50 34 53 49 44
```

`10 61 05 26` is `RF_INTF_ACTIVATED_NTF` (MT_NTF | GID_RF, OID 5, 38 bytes).
The payload starts with discovery id `01`, interface `02` and protocol `04`
(both ISO-DEP), technology/mode `00` (NFC-A passive), max data payload
`ff` and one initial credit.  The tail contains `0E 78 77 71 02` followed by
the ASCII historical bytes `PVJCOP4SID`.  Read as an ATS (TL `0E`, T0 `78`,
TA `77`, TB `71`, TC `02`), TB gives `FWI = 7` and `SFGI = 1`; the card is a
JCOP4 JavaCard.

The APDU plane is exactly the vendor's:

```
out:  10 00 00 <len> <apdu>
in:   10 00 00 <len> <response data> <SW1> <SW2>
```

Observed exchange:

```
[data tx] 10 00 00 0f 00 a4 04 00 0a 53 65 65 64 4b 65 65 70 65 72   SELECT AID "SeedKeeper"
[data rx] 10 60 06 03 01 00 01                                       CORE_CONN_CREDITS_NTF
[data rx] 10 00 00 0f 00 02 00 01 05 01 00 00 00 01 01 01 00 90 00
[data tx] 10 00 00 04 b0 3c 00 00                                    GET_STATUS
[data rx] 10 60 06 03 01 00 01
[data rx] 10 00 00 0f 00 02 00 01 05 01 00 00 00 01 01 01 00 90 00
```

`10 60 06 03 01 00 01` is `CORE_CONN_CREDITS_NTF` (destination type 1 = RF,
connection 0, one credit), not a deactivate.  The 13-byte data body of both
replies decodes exactly as the applet's `GetStatus`:

| byte | value | meaning |
|---|---|---|
| 0–1 | `00 02` | card-edge protocol 0.2 |
| 2–3 | `00 01` | SeedKeeper applet 0.1 |
| 4 | `05` | PIN0 tries left |
| 5 | `01` | PUK0 tries left |
| 6–7 | `00 00` | PIN1 / PUK1 (unused) |
| 8 | `00` | 2FA not used |
| 9 | `01` | "seeded" flag (always 1 in this applet) |
| 10 | `01` | setup done |
| **11** | **`01`** | **needs secure channel** |
| 12 | `00` | NFC policy enabled |

The AID SELECT and the following GET_STATUS returned the same 13 bytes; only
the latter is parsed, and it is a valid, self-consistent card status.

### Where it stops: the PIN command

The app then prompts for the PIN and sends `VERIFY_PIN`:

```
[data tx] 10 00 00 0b b0 42 00 00 06 31 32 33 34 35 36   B0 42 00 00 06 "123456"
[data rx] 10 60 06 03 01 00 01                            CORE_CONN_CREDITS_NTF
[data rx] 10 60 08 02 b2 00                               CORE_INTERFACE_ERROR_NTF
```

`10 60 08 02 b2 00` is GID CORE, OID `0x08` = `CORE_INTERFACE_ERROR_NTF`
(AOSP `system/nfc/src/nci_packets.pdl`: `CORE_INTERFACE_ERROR = 0x8`), status
`0xB2` = `RF_TIMEOUT_EXCEPTION`, connection 0.  No `MT_DATA` answer follows,
and every later SELECT on the same activation gets no answer either: the
NFCC has torn the RF link down.

This is the **NFCC's** timeout, not the host's.  Android's `IsoDep.setTimeout`
only widens a host-side wait (`NativeNfcTag.cpp`: `mTechnologyTimeoutsTable`
and `sTransceiveEvent.wait(timeout)`); when the NFCC reports an interface
error, `NFA_RW_INTF_ERROR_EVT` calls `nativeNfcTag_notifyRfTimeout()` and the
application gets a timeout immediately.  A longer timeout in `nfc.py` can
therefore never fix this.

The obvious "the card is slow" explanation is also weak: the applet's
`VerifyPIN` is just `OwnerPIN.check(...)` (source lines 2756–2788); the
expensive PBKDF2 work is in setup/import, not in PIN verification.  A stock
SeedKeeper should answer that command in milliseconds.

### The real blocker: this card requires the secure channel

The stock SeedKeeper applet (`Toporin/Seedkeeper-Applet`, tag `v0.2-0.1`,
which is exactly protocol 0.2 / applet 0.1) has
`needs_secure_channel = true`.  Its `process()` rejects every instruction
except `GET_STATUS` (`0x3C`), `INIT_SECURE_CHANNEL` (`0x81`) and
`PROCESS_SECURE_CHANNEL` (`0x82`) with `SW_SECURE_CHANNEL_REQUIRED`
(`0x9C20`):

```java
// only 3 commands are allowed, the others must be wrapped in a secure channel command
if (ins == INS_GET_STATUS) { ... }
else if (ins == INS_INIT_SECURE_CHANNEL) { ... }
else if (ins == INS_PROCESS_SECURE_CHANNEL) { ... }
else if (needs_secure_channel) {
    ISOException.throwIt(SW_SECURE_CHANNEL_REQUIRED);
}
```

Byte 11 of the card status confirms `needs_secure_channel = 1`.  So even
with a perfect RF link, **plaintext `VERIFY_PIN`, import and export cannot
work on this card**.  The expected reply to our plaintext VERIFY is
`9C20`, not silence; that discrepancy is the open question (see below).

`pysatochip` (the reference library) handles this automatically: on
`SW1=0x9C` / `SW2=0x20..0x23` it calls `card_initiate_secure_channel()`
(INS `0x81`), derives the session, and wraps subsequent commands as INS
`0x82` (`CardConnector.py`, `card_transmit` / `card_encrypt_secure_channel`).
The Krux module explicitly implements **only the plaintext flows**.

Implementing the secure channel in the port would need:

* a P-256 keypair and ECDH on the host (embit's pure-Python `ec` can do
  this; it must produce the same uncompressed point format the card expects);
* HMAC-SHA160 for the session/MAC keys and the message MAC — mbedtls in
  `crypto_fast` has SHA-1, so this is a small C addition;
* AES-128-CBC for the command/response bodies (also mbedtls, not currently
  exposed by `crypto_fast`);
* the IV counter rules and the card's `0x81` response parsing/verification.

That is a separate feature, not an NFC-transport bug, and it is not started.

### Why the PIN command goes silent

Three candidates, in order of likelihood:

1. **The card moved and the link died.**  `0xB2` is the NFCC's own
   no-response timeout; a card that shifts off the antenna produces exactly
   this.  The next test must keep the card flat on the phone for the whole
   PIN prompt and command.
2. **The card mutes on the gated command.**  Nothing in the applet source
   suggests it (it throws `9C20`), but the installed applet could be a
   modified build.
3. **A card/firmware timing quirk** that produces the timeout before the
   `9C20` status is sent.

The new port build (2026-10-28) makes the next test decisive: `_data()` now
recognises `CORE_INTERFACE_ERROR`/`0xB2`, prints timestamps, marks the link
dead and raises a named error instead of waiting out the host timeout;
`connect()` closes a stale node and re-brings-up the chip on the next
transmit.  If the card answers `9C20`, the transport is correct and the
remaining work is the secure channel.  If it still times out with the card
held still, we probe individual instructions (`0xA7`, malformed `0x42`) to
separate "card mutes on gated commands" from "link drops on the third APDU".

### Port changes made for this

* `krux/src/nfc.py`
  * `_data()`: 5 s host timeout; understands `CORE_INTERFACE_ERROR`
    (0x08), `CORE_GENERIC_ERROR` (0x07) and RF `RF_DEACTIVATE`; maps status
    0xB2/0xB0/0xB1/0xB3 to readable errors and marks the transport
    disconnected on link loss.
  * `_dbg()`: every packet is timestamped (milliseconds), so the difference
    between an immediate NFCC error and a real timeout is visible in
    `/data/krux/fw.log`.
  * `connect()`: closes a chip left open by a failed attempt before
    power-cycling it again.
* `krux/src/krux/seedkeeper.py`: `0x9C20` now reads
  "Secure channel required".

---

## Update 2026-10-28 — the secure channel works; the antenna decides

The secure channel described above is implemented and verified end to end
against the real card: `0x81` handshake, manual ephemeral-key recovery,
AES-128-CBC/HMAC-SHA1 envelopes, PIN verify, list, export, free-space and
import/save all work.

### What was actually wrong

* **The card's coupling is marginal on some Nexus 5 units.**  On the first
  phone, even a 15-byte SELECT failed randomly with `CORE_INTERFACE_ERROR`
  / `0xB2`.  Android's own NFC reader on that same phone was equally
  inconsistent (some reads took 10 s, some taps produced duplicate reads).
  A second Nexus 5 reads the same card reliably, including with the card
  attached for the whole sequence.
* **The PIN-before-card order is deliberate.**  It matches the official
  SeedKeeper Android app and keeps the card session uninterrupted.  The old
  order read the status first, asked for the PIN, and then needed the card
  re-presented because the link had gone idle during keypad entry.  Now the
  label (for save) and the PIN are entered first; then the card is presented
  once for select → secure channel → PIN verify.
* **A lost `0x81` is not retried on the same activation.**  After
  `CORE_INTERFACE_ERROR` the NFCC tears the RF link down, so the page starts
  a fresh activation instead.

### RF experiments that were tried and reverted

Both parameters were accepted by the NFCC and made no reliability
difference on the marginal phone:

* `NCI_PARAM_ID_FWI` (`0x58`) raised from the vendor's `0x70` (FWI 7,
  ~39 ms) to `0xF0` (FWI 15, ~1.2 s).  The 300 ms `0x81` answer still
  depended on the card's WTX frame.
* `NCI_PARAM_ID_PA_FSDI` (`0xBA`) set to 2 (32-byte reader frames) so the
  217-byte `0x81` answer would arrive as several short blocks.  No
  improvement; sometimes worse.

The vendor `SET_CONFIG_RF` is therefore back to `0x51 … 0x58 0x01 0x70`.

### UX on a marginal phone

`krux/src/krux/pages/seedkeeper.py` now:

* starts the field, then shows **"Tap your SeedKeeper card flat on the back,
  over the camera. Hold it still."**
* retries a session up to 20 times with an 8 s activation window each;
* retries a read action (list/export/status) with a fresh secure channel,
  without asking for the PIN again;
* shows a retry counter and **"Tap to cancel"** between attempts;
* asks for the save label before the card is presented, so the write starts
  immediately after PIN verification.

### NFC files on the target

`nfc/blobs/bcm2079x-b5_firmware.ncd` and `..._pre_firmware.ncd` are the
vendor firmware patches.  The current port **does not load them**: the chip
already has firmware and answers `GET_BUILD_INFO` after a power cycle, and
`nfc.py` starts with `CORE_RESET`/`CORE_INIT`.  They are kept as reference
for a future vendor-HAL comparison.  The runtime needs only the kernel's
`/dev/bcm2079x` node; no NFC blob is required in the target filesystem.
