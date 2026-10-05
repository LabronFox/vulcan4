# W221 — the parse's callback table is NEVER dispatched; `0x1012658` is never a target

**Dish:** `20-stream-feeder-source` (emitter-layer dispatch question). **Result:** ❌ no picture change;
gate fails. **Outcome (P2):** answered at the **emitter layer** — the parse's indirect callback dispatches
do not happen at all, so the stream's `[+0]=0x1012658` continuation is never reached.

## The instrument (emitter layer, the only place that can see it)

`emitRuntimeBranchDispatch` (`control_flow_emitter.cpp:195`) is the single choke point every indirect
dispatch is routed through. A probe was emitted there (`VULCAN4_W221_JALR`, OFF by default), after the
W129 precedent (`control_flow_emitter.cpp:505`): emitted by the translator (not pasted into generated
files), guarded by a runtime env knob, regenerated via `ps2_recomp` (RECOMP_EXIT=0, 2081 probe sites
emitted).

```
ps2_recomp gt4_recomp.toml   ->  Recompilation completed successfully
grep -c vulcan4W221JalrPrint ps2_recompiled_functions.cpp  ->  2081
```

## Positive control (required, and clean)

| build | `[w221:jalr]` lines |
|---|---|
| `VULCAN4_W221_JALR=1` | 87 (one 30 s run) |
| unset (default) | **0** |

The probe fires on many real indirect dispatches — `0x10089D4→0x1005870`, `0x1005890→0x10057F0`,
`0x10089C8→0x1007738` (the spin), and the `0x102xxxx` handlers (`0x1028540`, `0x1028680`, …). So it is
not blind, and "0 hits" for a target is a real zero.

## The answer

Across **6 shapes** (`w240a-f`, both `livelocked_in_syscall` and `wallclock_deadline`):

- **`target=0x1012658`: 0 hits.** The stream's `[+0]` continuation is **never** a resolved dispatch
  target.
- **No indirect dispatch has a source in the parse region `0x100F390–0x1010A68`** — in particular
  `source=0x100F498` (the parse's callback `jalr` through the table `0x1036AC0`, W203) fires **0 times**.

So **`sub_0100F390`'s callback table `0x1036AC0` is never invoked.** The parse's inner code runs by
fall-through, and the callback that would `jalr` the continuation `0x1012658` is never dispatched — the
parse does not reach the point where it would call its callback.

## What this names

- The parse's indirect-callback mechanism (`0x100F498` → table `0x1036AC0`) **never dispatches**, so the
  bit-array producer it should invoke never runs. This is upstream of the rotator and explains W220's
  "no producer": the producer is a **parse callback that is never called**, because the parse's control
  flow does not arrive at `0x100F498`.
- The only indirect dispatches in the whole run are the spin's 3 calls and the `0x102xxxx` syscall
  handlers. Every parse-side `jalr` site is silent.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
The parse reaches `0x100F390` but never `0x100F498` (the callback `jalr`) — find the branch between them
that is not taken, i.e. why the parse stops before invoking its callback. Watch the branch at the
`jalr` guard in `sub_0100F390` (the table select / bound test) and name the register/condition that
never becomes true. That is the parse's stop point, named at the instruction level.
