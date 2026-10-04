# W175 — the filled/empty pair, named by writer PC: `arg2`'s data is zeroed and never copied

**Dish:** resume of `03-size-before-read`. **Result:** ❌ picture unchanged (gate fails), but the
defect is now one writer-PC wide and reproducible in a single run.

## The measurement (one run: `VULCAN4_W162_DECODE` + `VULCAN4_W163_WATCH`)

At the decoder `sub_010088E8` entry, the four arg nodes and their data buffers:
```
arg0 nd=0x18953e0 sz=0x1 dp=0x1895410 w0=0x0
arg1 nd=0x1895340 sz=0x1 dp=0x1895370 w0=0x1   <- populated
arg2 nd=0x1895390 sz=0x1 dp=0x18953c0 w0=0x0   <- the rotate/compare WORK buffer, ZERO
arg3 nd=0x1895310 sz=0x1 dp=0x1895430 w0=0x1   <- populated
```
The store watch (whole `0x1895000-0x1896000` page) over the **same run**, for each `dp`'s word 0:

```
arg1 dp=0x1895370:  val=0x1895370 writerPc=0x100826c op=memcpy      <- FILLED
arg3 dp=0x1895430:  memset 0x1007060, then memcpy 0x1008fac, 0x100572c <- FILLED
arg0 dp=0x1895410:  val=0x0 writerPc=0x1008da4 (and 0x10057a8)       <- only zeroed
arg2 dp=0x18953c0:  val=0x0 writerPc=0x1008ce0                        <- ONLY ZEROED, never copied
```

## What this proves

- `arg1` and `arg3` data buffers are written by `memcpy`s (`0x100826c`, `0x1008fac`, `0x100572c`).
- **`arg2`'s data word 0 has exactly one writer in the entire run: `0x1008ce0`, value `0`.** It is
  never the destination of any copy. `0x1008ce0` is `sw $zero,0($v0)` in the node-creation block, so
  `arg2`'s buffer is **allocated, sized to 1, zero-filled, and never populated**.
- Same for `arg0` (`0x1008da4`). So the decoder is given a mix: two populated buffers (`arg1`,
  `arg3`) and two zero-filled ones (`arg0`, `arg2`) — and it compares the zero one (`arg2`) against a
  populated one (`arg3`), so `func_1005870` returns −1 forever.

## The decision

This is **(a)**, sharpened: the guest is fed a node that our/its own earlier step **zero-filled and
never copied into** — a data-population defect at the byte level, not a size-ordering defect. The
size is already `1` on every arg (W174), the empty branch returns without decoding (W174), and the
`$s7` clone does copy `+8` (W174 metric 2). So the brief's "size before read" premise is dead; **the
missing thing is the COPY into `arg2`'s data buffer.**

## Gate (authoritative, honest)

```
newest capture : /mnt/ssd/vulcan4-build/run/disclaimer-break-W174.png  (byte-identical disclaimer)
GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference — the screen did not change.
```
Suite **497/497**. Committed, author `Or Golan <or024662@gmail.com>`.

## STUCK — next wall, in my own words
```
STUCK:   picture that is not the disclaimer
TRIED:   decoder arg census + per-dp writer watch in one run; clone/size semantics; empty-path reachability
BLOCKED BY: arg2 (work buffer) is zero-filled by 0x1008ce0 and is never the destination of any
         memcpy in the whole run, while arg1/arg3 are; the compare therefore sees work(0) < target(1)
NEED:    name the source that SHOULD be memcpy'd into arg2 (0x18953c0): compare the arg2 node's
         producer with arg1's producer (whose clone memcpy at 0x100826c works). If the source buffer
         the guest should copy from is the CORE.GT4 read buffer (0x10d1ac0) and nothing points at it,
         the missing link is between the file read and the parse -- that is the fix location.
```
The premise of this dish is dead (a useful refutation); the picture is unchanged, so the goal gate stays failed.
