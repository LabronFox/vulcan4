# W223 — `t0 != t2` is the NORMAL "window not exhausted" state; the callback is a refill, correctly skipped

**Dish:** `22-anchor-vs-pointer`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the `t0 != t2` question is answered — **neither side is "wrong"**. `t0`/`t2` are the
decompressor's window cursor/end, and `t0 != t2` means the input window still has data, so the `0x100F498`
callback (a refill) is correctly not invoked. This **refutes** W221's implied reading that the callback
should run.

## The fields, measured (`VULCAN4_W222_GUARD` + `VULCAN4_W217_PROD`)

`sub_0100F390` loads its window from the struct `s0=0x1FFCED0` (`0x100F3F4 lw t0,12(s0)`,
`0x100F3F8 lw t2,16(s0)`). Across 4 shapes (`w243a-d`) they are **frozen**:

```
[w223:struct] s0=0x01ffced0 [+8]bound=0x1 [+c]t0=0x01036E28 [+10]t2=0x01036D7F [+14]base=0x0 [+18]=0x0
```

The **writers** (`VULCAN4_W217_PROD`) show the caller `sub_0100F8C8` sets them right before the call:
```
addr=0x1ffced8 ...  == [s0+0x8]  bound  (0x1010A50)
addr=0x1ffcedc v=0x1036e28 pc=0x1010A54   [+0xc] t0
addr=0x1ffcee0 v=0x1036d7f pc=0x1010A58   [+0x10] t2
```
i.e. `0x1010A50 sw s2,8(s0)` (bound=1), `0x1010A54 sw s4,0xc(s0)` (t0), `0x1010A58 sw s6,0x10(s0)`
(t2), then `0x1010A68 jal 0x100F390`.

## What this means (the reframe)

`sub_0100F8C8` (`0x100F8C8`) is the **same decompressor** shape as `sub_0100F390`: it loads `s4=[s0+0xc]`,
`s6=[s0+0x10]`, and loops `bne s4,s6` while `s4 += 4` (a **word cursor** walking toward the end pointer),
**refilling when `s4 == s6`**. So:

- `t0 = 0x1036E28` is the **current window cursor**; `t2 = 0x1036D7F` is the **window end/start anchor**
  (the gzip stream `1f 8b 08 08 … "in.notice2005.img"` begins at `0x1036D7F`, W203).
- `t0 != t2` means **the window is NOT exhausted** — there is input left (`0xA9` bytes).
- The callback `0x100F498` is a **refill callback invoked when `t0 == t2`** (window empty). It is
  **correctly skipped** because input remains.

**So W221's "the callback never dispatches" is explained — and it is correct behaviour, not a defect.**
Neither `t0` nor `t2` is "wrong"; the `0x100F464` skip is the normal non-refill path. The callback is not
the missing producer.

## Positive control

Both probes fire many times (guard 65–68×/shape, struct 61×), values stable across 4 shapes — real
measurements, not blind zeros.

## The wall, restated

`sub_0100F390` is decoding with a non-empty window; the refill callback is not needed yet. The stream with
**no producer** is `func_1007738`'s bit buffer at `0x1FFFBA0` (W219/W220) — a **different** structure from
the decompressor window `s0=0x1FFCED0`. W221's emitter probe answered the wrong question: the callback
being silent is normal. **The missing producer is not this callback.**

## NEXT
Re-aim at the `0x1FFFBA0` bit buffer's producer directly: the parse's node-builder writes it once (W219),
and no rotator-external writer exists (W220). Find the **parse call that should run after the first fill**
to add the second element/bit — trace `sub_0100F8C8`'s loop exit (`s4 == s6`) and whether it ever refills
the `func_1007738` stream, or whether the merge is simply fed a single-element input by design and the
consumer at `sub_010088E8` is the wrong caller.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.
