# W222 — the parse's stop point: `0x100F464` (`t0 != t2`) always skips the callback

**Dish:** `21-parse-callback-jalr`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the parse's stop point is named at the instruction level — the callback `jalr` at
`0x100F498` is gated by two branches, and the **second (`0x100F464 bne t0,t2`) always takes the skip**.

## The gate, instrumented at the emitter layer

`sub_0100F390` reaches the callback `jalr` at `0x100F498` only if **both** gates fall through:
```
100f45c: bgez  a2, 0x100f4d0     ; gate 1: a2 < 0 required
100f464: bne   t0, t2, 0x100f4c0 ; gate 2: t0 == t2 required
100f498: jalr  v1                ; THE CALLBACK (via table 0x1036ac0)
```
A translator-emitted probe (`VULCAN4_W222_GUARD`, OFF by default) logs each gate's taken flag and
operands. Across **6 shapes** (`w242a-f`, mixed `livelocked_in_syscall`/`pc_outside_generated_table`):

```
[w222:guard] n=1 pc=0x0100f45c taken=0 a2=0xfffffff8 t0=0x01036e28 t2=0x01036d7f a3=0x2fe07a5f
[w222:guard] n=2 pc=0x0100f464 taken=1 a2=0xfffffff8 t0=0x01036e28 t2=0x01036d7f a3=0x0017f03d
[w222:guard] n=3 pc=0x0100f45c taken=1 a2=0x00000016 ...
```

- **Gate 1 (`0x100F45C`): not taken once** (`a2 = 0xFFFFFFF8 < 0`) — then taken on every later pass
  (`a2 >= 0`).
- **Gate 2 (`0x0100F464`): TAKEN** (`t0 = 0x01036E28 != t2 = 0x01036D7F`).

## The stop point, named

**The parse skips the callback at `0x100F464` because `t0 != t2`** (`0x01036E28` vs `0x01036D7F`), so it
branches to `0x100F4C0` and never executes `0x100F498`. That is exactly why W221 measured the callback
`0x100F498` dispatched **0 times** and the stream's `[+0]=0x1012658` never reached.

- `t0` is the current position in the gzip input (`0x01036E28`, inside the loaded notice data at
  `0x1036D7F..`); `t2` is the expected anchor `0x01036D7F` (where the stream began).
- The mismatch `t0 != t2` is the parse saying "the input pointer is not at the code's expected site", so
  it takes the shortcut path instead of invoking its callback.

## Positive control

The probe fired 65–68 times per shape (both halt classes) — not a blind zero. The values are stable
across runs and shapes, so the `t0 != t2` result is real.

## The wall, in one line
`sub_0100F390` never calls its callback (`0x100F498`) because the gate at `0x100F464` always sees
`t0 != t2` (`0x01036E28` vs `0x01036D7F`); the callback that would feed the `0x1895480` bit stream is
therefore never invoked, the stream stays empty, and the merge spin cannot exit.

## NEXT
Why `t0 != t2` at `0x100F464`: trace where `t2 = 0x01036D7F` is set (the expected input anchor) and what
advances `t0` past it (`0x01036E28`). The parse's input pointer (`t0`) and its expected anchor (`t2`)
disagree by ~0xA9 bytes; find which side is wrong (the anchor label or the input offset) and why.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.
