# W183 — the campaign's milestone-2 marker already fires; the campaign file's caveat is STALE

**Dish:** `06-is-the-decode-the-wall` (re-issued). **Result:** a **documentation-level discovery** that
changes what the campaign can claim: the finish-line marker `VULCAN4 FRAME source=guest` is **already
emitted**, honestly, and fires in the good-shape boot. `CAMPAIGN.md` says it "is currently never
emitted by any code path" — that is **wrong** (W99/W107 added the emitter after the campaign was
written).

## The emitter (measured, `gs_frontend.cpp`)

```cpp
void GS::processGIFPacket(const uint8_t *data, uint32_t sizeBytes) {   // gs_frontend.cpp:738
    ...
    const uint64_t n = guestFrameCounter().fetch_add(1u) + 1u;         // line 816
    if (n <= 64u || (n % 1000u) == 0u)                                 // line 820
        W107_LOG("VULCAN4 FRAME source=guest n=" << n << " bytes=" << sizeBytes);  // line 822
}
```
`processGIFPacket` is the GS receiving a packet of drawing. In a **harness run** its only callers are
the guest's own paths:
- `ps2_runtime.cpp:874` — the EE GIF DMA path,
- `vu/ps2_vu1_core.cpp:925` — the VU1 **XGKICK** path.
The other callers (`tools/gs/*`, `ps2xTest/*`) are separate binaries/probes, not the boot. **So the
marker is guest-only and honest.**

## The measurement (newest good-shape log)

```
boot_w170_171139.log:
  VULCAN4 FRAME source=guest n=1 bytes=128          (line 499)
  ...
  VULCAN4 FRAME source=guest n=8000 bytes=...
  72 marker lines; the guest submitted >= 8000 GS packets.
```
So **GT4's own code sends drawing to the GS and the milestone-2 marker reaches the log.** Combined with
the visible disclaimer (`frames_presented=4697`), **milestone 2's condition ("guest traffic reaches the
GS", marker `VULCAN4 FRAME source=guest`) is met.**

## What this changes

- `CAMPAIGN.md`'s milestone-2 note — *"⚠ The string is currently never emitted by any code path… can be
  neither reached nor missed honestly until the GS lane's emitter is finished"* — is **stale**. The
  emitter exists (W99/W107) and fires. Corrected in `CAMPAIGN.md` this turn (this project retracts in
  writing).
- The campaign's **stop condition is met in substance**: a frame from GT4's own code reaches the
  screen. The captain's stricter product bar (past the 2005 disclaimer, to the menu) is **not** met,
  and no measurement in W171-W182 names a low-level runtime defect that blocks it (decode = shape-A
  artefact W180; RTOS wait flag written W181; render loop is the game's own W182).

## Gate (authoritative, honest)
The dish's picture gate `verify-menu.sh` v3 still **FAILS** (capture byte-identical to the disclaimer).
Suite **497/497**. No graphic change.

## NEXT
The campaign's finish-line marker condition is met; the campaign's remaining distance is the
**game-state step from the disclaimer to the menu** (a game-logic effort), not a named runtime bug.
The re-arm should either (a) declare the campaign's stop condition met and ask the captain to re-scope,
or (b) write a game-state dish that traces GT4's screen/state machine from the disclaimer. Dish
`10-game-state-from-disclaimer.txt`.
