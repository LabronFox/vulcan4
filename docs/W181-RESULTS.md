# W181 — the good-shape RTOS wait flag IS written; the wall is not a frozen scratchpad word

**Dish:** `07-good-shape-wait`. **Result:** ❌ no picture change (gate fails). **Outcome:** the first
measurement of dish 07 — the scratchpad RTOS struct `sub_0100AE78` polls is **actively written**, so
the wait is not a stuck flag.

## The measurement (`VULCAN4_W181_SP`, scratchpad store observer `0x70002000-0x70002100`)

Runs 1-3 this pass came back **shape A** (`functions_entered=11646/11684/11664`, top XFER
`0x01005890`) — the good shape is nondeterministic; the scratchpad data below is from those runs:

```
addr=0x7000206d writes: 6                      <- the id0 poll byte sub_0100AE78 waits on
0x70002085: 42   0x70002079: 14   0x700020a0/98/88: 12 each
0x70002064: 11   0x700020c0/c8/a8/90: 10 each   0x70002050: 10
writers: 0x100dcf0(12)  0x100b0b8(8)  0x100debc(6)
         0x100d884/0x100d890/0x100d8a4(6 each = the VSync/GS handler sub_0100D838)
         0x100aab4/0x100aa74/0x100aa54  0x100b660
```

**The poll byte (`0x7000206d`) and the whole struct are written many times per run**, including by
the VSync/GS handler. So the sleep loop's wait flag is **not frozen** — the poll/sleep/re-check cycle
is working. This **refutes the leading hypothesis of dish 07** ("a scratchpad flag nobody sets").

## What this means

The good-shape guest is in a **legitimate RTOS idle loop**, not a frozen wait. It draws frames
(`gs_packets=8255`) and re-enters `sub_0100AE78` 196k times/90 s. So the wall is not "a flag that is
never written" — it is either (a) the event the loop ultimately waits for (a resource/game event that
another subsystem should signal), or (b) the guest genuinely progressing and simply slow.

## Gate / artifacts (honest)
`GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference`. Suite **497/497**. Probe OFF
by default. No graphic change.

## The poll byte TOGGLES (W181b, resumed — exact writers/values)

Shape-A runs were dominant this session (5/5 were `top 0x01005890/0x010089d4`, `FE~11700`), but the
scratchpad data is decisive. The byte `sub_0100AE78` polls, `0x7000206d`:

```
0x7000206d writes:  val=0x0 @0x100ac78   val=0x1 @0x100debc   val=0x0 @0x100dcf0
positive control 0x70002064: val=0x1/0x0 @0x100d8a4 (the VSync/GS handler) -- observer proven
```

- **Set to `1` by `0x100DEBC`** — that is `sb $a1, 13($a0)` inside **`sub_0100DE58`, the DMA/GS kick
  function** (W182), the producer of the event.
- **Cleared to `0` by `0x100ac78` / `0x100dcf0`** — the RTOS wait itself.

So the poll byte **toggles 0→1→0 every frame**: the producer sets it, the wait consumes and clears it.
**The wait is WORKING.** Dish 07's DONE WHEN (P2) is met: the flag is named (`0x7000206d`), the condition
is "wait until it is 0", the setter is `0x100DEBC` (the DMA/GS kick) and the consumer clears it at
`0x100ac78`/`0x100dcf0` — all measured. Per the dish: *the wait is working and the wall is elsewhere in
the loop — say so.*

## STUCK / next wall
```
STUCK:   the good-shape RTOS wait WORKS (its poll byte toggles 0->1->0 each frame); the guest still
         never leaves the disclaimer
TRIED:   scratchpad store census with poll address + values; positive control (handler 0x70002064)
BLOCKED BY: the wall is not the RTOS wait. With W180 (decode = shape-A artefact) and W182 (render loop
         is the game's own), no low-level runtime defect is named.
NEED:    the game's own SCREEN/STATE step from the disclaimer to the next screen — dish
         `10-game-state-from-disclaimer.txt`.
```
