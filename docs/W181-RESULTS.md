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

## STUCK / next wall
```
STUCK:   the good-shape RTOS loop is legitimate (its flag is written); the guest still never leaves
         the disclaimer
TRIED:   scratchpad store census -- the poll byte 0x7000206d is written 6x, the VSync handler writes
         the struct, so the wait is not a frozen flag
BLOCKED BY: the wall is UPSTREAM of the RTOS loop: the event the loop is waiting for (a resource/game
         event) is not produced
NEED:    in a GOOD-shape run, capture the loop's return value / the condition it loops on, and the
         caller `0x0100DE80`'s decision -- to name the event by what the guest does when it is ready.
         Next dish `08-good-shape-event.txt`.
```
