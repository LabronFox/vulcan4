# W220 — the stream's bit array is written ONLY by the rotator itself; no producer ever fills it

**Dish:** `19-stream-advance-producer` (re-run). **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the missing producer is **named** — there is none. The only writer of the bit array
`0x1895480` is `func_1007738`'s own rotate write-back (`0x1007774`), which writes `0x0` every time.

## The decisive measurement (`VULCAN4_W217_PROD`, wide watch on `0x1895480..0x1895540`)

Four shapes (`w239a-d`, both `livelocked_in_syscall` and `wallclock_deadline`):

```
addr=0x1895480 v=0x0 writerPc=0x1007774   (622 / 629 / 630 / 629 writes)
```

**Every single write to the bit array base is `0x0`, from `0x1007774`** — and `0x1007774` is inside
`func_1007738` itself (`sw v0,0(v1)` at `0x1007774`, the rotate write-back where `v0 = (word<<1)|carry`).
So the rotator reads `0`, computes `(0<<1)|0 = 0`, and writes `0` back, ~625 times, forever. **No other
PC ever writes the array** — the parse's one-time build is the only other content (and it left it zero).

## What this means for the merge

- `func_1007738` rotates a **zero** word: `v0 = (0<<1)|carry`, and since `carry` (the top bit, `srl a0,31`)
  is 0, it writes `0` back and `beqz a2,0x10077a4` returns **without** calling the refill.
- The refill `0x1005AB8` (which would write a `1` bit and grow the bound) runs **once** (W219b:
  `refill1005AB8=1`) — the initial fill — and never again, because the carry never becomes 1.
- So the stream stays length-1, all-zero; `func_10057F0` compares the two fixed sorted-list keys
  (`e0[0]=0x0` vs `e1[0]=0x1`) and returns -1 forever; `sub_010088E8`'s `bltz` spin never exits.

## Positive control

The watch fires 600+ times on the target word (`0x1895480`), all from `0x1007774` — so the observer sees
the array, and its zero value is a real measurement, not a blind probe. The `0x18951F0`/`0x1895200`
targets and the `0x70002050` control (via the earlier W217 runs) also fire.

## The `[+0]=0x1012658` field (step 2, partial)

`0x1012658` is an instruction inside the parse helper `sub_010124F8` (`0x10124F8`), whose callers are
`0x1010F90` and `0x1012818` — the node-builder region. It is **stored as a data value** in `[+0]`
(a computed code address), not called through a `jalr` in any run (neither `0x1012658` nor `0x10124F8`
appears as a dispatch target). Tracing whether it is later `jalr`'d needs instruction-level hooks (the
recompiler emitter layer); it is **not** the missing bit-source, which is the array itself.

## Verdict (dish P2 complete)

**The missing producer is named: there is no producer.** The bit-array base `0x1895480` is written only by
`func_1007738`'s own rotator (`0x1007774`), always with `0x0`; the refill that would supply a real bit
runs once and is never re-triggered. The parse builds a **length-1, all-zero stream** for this merge, and
nothing ever fills it — so the merge cannot terminate and the disclaimer never advances.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
The stream is a consumer with no producer in this run. Either the parse is supposed to feed the array
from a source that is absent (GT4.VOL data / a second input the build does not supply), or the merge is
being driven at the wrong point. The next measurement is the parse callback in `sub_0100F390`'s `jalr`
table (`0x1036AC0`) that should `jalr` into the stream's `[+0]=0x1012658` continuation — instrument it at
the recompiler emitter layer, since dispatch-level probes cannot see indirect calls inside the parse.
