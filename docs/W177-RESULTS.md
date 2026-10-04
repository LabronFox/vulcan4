# W177 — the parse copies the file into `0x105xxxx`, but the decode's node points at `0x18953c0`

**Dish:** `05-trace-parse-node`. **Result:** ❌ no picture change (gate fails). **Outcome:** the
destination of the parsed bytes is named, and it is **not** the decode's work buffer — so the drop is
a **node data-pointer that does not reference the parsed data**.

## Measurements (one run that reaches the decode, `VULCAN4_W178_COPY` + `VULCAN4_W177_MEMCPY`)

**1. The copy loop's byte stores go to `0x105xxxx`, never to the decode nodes.**
`sub_0100F390`'s byte store (`sb`, op `WRITE8`, pc `0x100f4ec`/`0x100f800`) writes the file's bytes to:
```
writes: 0x1051a40 "T e x 1 0 0 0 0 ..."   (then zeros)
distinct 16 KB destination pages over 30,000 byte-stores: 0x1050000, 0x1054000, 0x1058000
writes into the decode node region 0x1895xxx: 0
```
So the parse **does** consume `CORE.GT4` (W176) and writes it into string/data buffers in
`0x1050000-0x105BFFF`. It never writes the decode's node buffers.

**2. The decode's work buffer is written by nothing but the zeroing.**
W175 already showed `arg2`'s word 0 (`0x18953c0`) has exactly one writer in the whole run — `0x1008ce0`,
value 0. Neither `sub_0100F390` (string/data copies) nor the clone's `sub_0101E81C` memcpy reaches it.

**3. The clone's copy is a 0x50-stride sibling mix-up (W177b).**
At the `sub_01008080` clone memcpy (`0x1008264`, `jal 0x101e81c`):
```
dst=0x1895370 src=0x18952a0 len=0x4  sp+36=0x1895310(+4=1)  sp+52=0x1895270(+4=2)
```
`0x1895370` is the decoder's **`arg1`** data buffer; the decoder's `arg2` node (`0x1895390`, data
`0x18953c0`) is an **untouched 0x50-away sibling**.

## What this establishes

The parsed bytes exist in memory (`0x105xxxx`), and the decode's `arg2` node exists with a valid data
pointer (`0x18953c0`) — but **nothing wires the parsed data to that node**. The drop is a
**node-data-pointer selection**: the clone fills the `arg1` side of a 0x50-stride pair; the decoder
reads the `arg2` side, which is never populated. This is one hop before `sub_01008C50`.

## Gate (authoritative, honest)
```
GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference — the screen did not change.
```
Suite **497/497**. Probes OFF by default (`W176/W177/W178`). No graphic change.

## STUCK / next wall
```
STUCK:   the loading screen (2005 disclaimer) never ends
TRIED:   copy-loop destinations (they go to 0x105xxxx, not the nodes); clone dst/src/slots;
         runner-level absence of any writer to arg2's buffer
BLOCKED BY: the decode's arg2 node (0x1895390 / data 0x18953c0) has no producer; the parsed bytes sit
         in 0x105xxxx and the clone fills arg1's sibling instead
NEED:    decide whether arg2's data pointer should reference one of the parsed 0x105xxxx buffers (then
         find what writes the node's +0x14) or whether the compare's meaning is inverted (arg2 is the
         accumulator target and arg3 the source). One next measurement: dump the arg2 node's producer
         chain at 0x1895390 and compare its +0x14 against the parsed buffers' addresses.
```
