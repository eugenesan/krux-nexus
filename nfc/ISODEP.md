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
