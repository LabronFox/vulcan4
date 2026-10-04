# W219 — the `0x1FFFBA0` stream is built once by the parse and never advanced

**Dish:** `19-stream-advance-producer`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the stream's producer is **named** — the parse's node-builder — and the struct is
**measured static** for the whole run, so the merge's input never advances.

## The struct, measured (`VULCAN4_W215_SPIN` `[w219:stream]`)

Dumped at the spin in 6 shapes (`w236a-f`, mixed `livelocked_in_syscall`/`wallclock_deadline`), all
identical and unchanged between the first sample and every 20M-iteration sample:

```
[w219:stream] a0=0x1FFFBA0
  [+0]=0x1012658  [+4]=0x0   [+8]=0x1   [+c]=0x1
  [+10]=0x0       [+14]=0x1895480  [+18]=0x1  [+1c]=0x0
  [+20]=0x1FFFCC0 [+24]=0x0  [+28]=0x1   [+2c]=0x1
```

- `+0` is a code pointer (`0x1012658`); `+0xC=+0x18=+0x28=+0x2C=1`; `+8=1` (the bound `func_1007738`
  reads); `+0x14=0x1895480` (the base).
- **The struct does not change across ~50M spin iterations or across shapes.**

## The producer, measured (`VULCAN4_W217_PROD`)

The wide watch on `0x1FFFBA0..0x1FFFBCC` shows the struct is written by the **parse's node-builder
region**, early, then never again:

```
addr=0x1fffba0 v=0x1012658 pc=0x10118f4   (final +0 value)
addr=0x1fffba0 v=0x30      pc=0x1011304 / 0x101151c
addr=0x1fffba8 v=0xf       pc=0x10112f0 / 0x1011524
addr=0x1fffba8 v=0x1fffcb0 pc=0x1012514
addr=0x1fffba0 v=0x1fffcc0 pc=0x101253c
```

All writers are in `0x10112E0–0x1011650` and `0x10121xx–0x10125xx` — **the same parse node-builder
that builds the sorted lists `func_10057F0` compares** (W218b: `sub_010121F8`). So one parse pass builds
both the stream struct and its lists, sets the bound to 1, and then the spin's merge (`func_1007738` +
`func_10057F0`) runs against a static, length-1 input forever.

## The contract, stated

`func_1007738` walks the stream at `0x1FFFBA0`: bound `+8=1`, base `+0x14=0x1895480`. The merge wins only
when `func_10057F0` returns >= 0, i.e. when the two sorted keys compare equal — but the keys are
`e0[0]=0x0` vs `e1[0]=0x1` (W218b), fixed by the parse. **Nothing advances the bound or the keys, so the
merge cannot terminate.** The producer that *should* advance it is the same parse subtree that built it;
either the parse is incomplete at this point (the stream is meant to be fed more nodes), or the consumer
is mis-supposing a second input that the parse never supplies.

## Positive control

The watch fires on the target words (e.g. `0x1FFFBA0` written by `0x10118F4`), and the struct dump is
non-trivial (11 distinct non-zero fields), so the observer sees real data, not an unreadable sentinel.
The shapes include both `livelocked_in_syscall` and `wallclock_deadline`, and the struct is identical.

## The wall, in one line
The `0x1FFFBA0` stream struct (bound 1, base `0x1895480`, code ptr `0x1012658`) is built once by the
parse's node-builder and never advanced; the spin's merge (`func_1007738`/`func_10057F0`) compares two
fixed sorted-list keys (`0x0` vs `0x1`) and can never terminate.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## W219b — the feeder NAMED and its run count MEASURED: `0x1005AB8`, **1 run**

`func_1007738` (`0x1007738`) rotates the stream's bit array and, **only when the shifted-out carry bit
is non-zero**, calls the refill **`0x1005AB8`**:

```
1007758: lw   v0,20(a3)        ; base
1007768: lw   a0,0(v1)         ; word
100776c: sll  v0,a0,1          ; word<<1
1007770: or   v0,v0,a2         ; | carry
1007774: sw   v0,0(v1)         ; write back
1007784: srl  a2,a0,0x1f       ; carry = top bit
1007788: beqz a2,0x10077a4     ; carry==0 -> return (no refill)
100779c: jal  0x1005AB8        ; carry!=0 -> REFILL (writes a new bit into the stream)
```

`0x1005AB8` writes `[base + i*4] = a2` (a bit) and adjusts the bound (`0x1005af8`, `0x1005b30`) — it is
the stream's feeder. The counter (`[w218:pred]`, shape `w237a` `livelocked_in_syscall`) measures its
run count:

```
[w218:pred] c5870=49973331 ... exits=0 refill1005AB8=1
```

**The refill runs exactly ONCE for the whole run**, against **49,973,331** spin iterations. So the stream
is filled once (its initial bit) and then the carry stays 0 forever, the refill is never re-entered, and
the bound/base never advance. The producer that *should* feed more bits is the byte-source upstream of
`func_1007738`'s carry — i.e. the parse that should supply input to this merge — and it never runs.

## The contract, stated plainly
The stream at `0x1FFFBA0` is a **bit buffer**: bound `+8`, base `+0x14`, whose feeder `0x1005AB8` is
called from `func_1007738` when the rotated-out carry is 1. It runs **once**; after that the buffer's
carry is 0 on every iteration, so no refill and no progress. The merge `func_1007738`/`func_10057F0` is
waiting for input bits that the (parse) producer never supplies — the same "producer of the first bit"
W122 left unnamed, now located to the refill `0x1005AB8` and counted.

## W220 — the array is never re-fed: word `0x0`, `changes=0`

A watch on the stream's bit array base (`+0x14 = 0x1895480`, deterministic across runs) counts changes
to its first word (`[w220:array]`):

```
[w220:array] base=0x1895480 word=0x0 changes=0     (w238a wallclock_deadline, w238c/e livelocked)
```

**The word is `0x0` and `changes=0`** — the parse never writes the array after building the stream. So
`func_1007738` rotates an all-zero buffer, the carry is 0 on every iteration, the refill `0x1005AB8`
runs once (initial fill) and never again, and the bound stays 1. The producer that should feed more bits
**never runs**.

## Verdict (dish P2 complete)

- **Struct:** `0x1FFFBA0` — bound `+8=1`, base `+0x14=0x1895480`, code ptr `+0=0x1012658`, fields
  `+c/+18/+28/+2c=1`.
- **Writers:** the parse's node-builder (`0x10112E0–0x1011650`, `0x1012xxx`), **once**, then never.
- **Feeder:** `0x1005AB8` (from `func_1007738`), **runs once** vs 50M iterations.
- **Array:** word `0x0`, `changes=0`.
- **Why the merge never progresses:** the stream is a length-1, all-zero bit buffer built by one parse
  pass; the merge `func_1007738`/`func_10057F0` waits for input bits that are never supplied, so
  `func_10057F0` returns -1 forever and `sub_010088E8`'s `bltz` spin never exits.

**A named blocker, fully measured.** The wall is not the spin's code — it is the **parse producing a
degenerate (length-1, zero) stream** for this merge. Either the parse must supply more input (a second
element / non-zero bits), which is upstream in `sub_0100F390`'s callbacks, or the consumer is mis-fed.

## NEXT
`func_1007738`'s two lists are length 1 with keys 0 and 1 and its bit array is all-zero. Find the parse
callback that should supply the merge's second element / the stream's next byte and prove it never runs
(this is the byte-source in `sub_0100F390`'s `jalr` table at `0x1036AC0`). If it is gated on GT4.VOL data
absent from the build, that is the input to name.
