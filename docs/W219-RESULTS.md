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

## NEXT
Name the parse call that is *supposed* to feed the stream a second node (the merge's other input) and
why it does not run: the stream's `[+0]=0x1012658` is a code pointer — find its users (a `jalr [ptr]`)
and trace the feeder. If the parse is gated on GT4.VOL data absent from the build, that is the input.
