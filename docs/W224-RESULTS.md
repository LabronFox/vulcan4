# W224 — `sub_010088E8` is INLINED (no dispatch); the merge's stream is `0x1FFFBA0` by construction

**Dish:** `23-merge-consumer-contract`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the consumer contract is named — `sub_010088E8` is **inlined** into `sub_01008C50`
(never a dispatch target), so dispatch-level probes cannot log its four caller streams; and the merge's
input stream is `0x1FFFBA0`, which is single-element and never fed. Verdict: **a game-state/producer
question, not a runtime defect.**

## `sub_010088E8` is inlined

`sub_010088E8` is called at `0x1008FFC` (`jal 0x10088e8`) from `sub_01008C50`. A probe keyed on
`targetPc == 0x10088E8` fires **0 times** across shapes, while the spin's internal sites
`0x1005890`/`0x10089C8`/`0x10089D4` are **33.31% each** of transfers (`w249a/e`, `w248a`). So the emitter
inlines the call (`emitDirectFunctionJumpIfAvailable`) and `sub_010088E8` runs **inside** its caller's
generated body — the same blindness the W129 note records for the copy loop. Its four streams
(`a0/a1/a2/a3` passed at `0x1008FFC`) therefore cannot be logged at the dispatch layer.

## What IS measured

- At the spin, `func_1005870`'s `a0 = 0x1FFFBA0` (W218) — so the merge stream is `0x1FFFBA0` by
  construction.
- That stream: bound `+8=1`, base `+0x14=0x1895480`, all-zero, **written only by its own rotator**
  (`0x1007774`, 624×, all 0), no producer (W219/W220/W223c).
- `sub_01008C50`'s only caller is `0x10082DC` in the decoder `sub_01008080` (W176-W179 parse chain).

## Verdict (the dish's explicit question)

**This is a game-state / producer question, not a runtime defect.** The merge runs on a single-element,
all-zero stream built by the parse; the parse (via the decoder `sub_01008080`) does not supply a second
element. No runtime layer is broken: the recompiler, scheduler, GS and stores are all exonerated by the
W171-W223 chain. The remaining distance to the disclaimer advancing is **GT4's own decode/parse state**
— whether the decoder is expected to produce more output for this merge, or whether `sub_010088E8` is
fed the wrong stream by a game-logic path.

## Positive control

The spin-site probe fires (33.31% of transfers) and the `[w224:merge]` probe is silent **because the
target is inlined**, which is itself the measured fact (a dispatched function would appear). The
`[w215:spin]`/`[w218:pred]` probes fire with stable values, so the silence is a real "inlined", not a
blind zero.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
Emit a W224 probe at the **emitter layer** for the call at `0x1008FFC` (or the entry of
`sub_010088E8`) to log its four `a0/a1/a2/a3` and each one's bound/base, so which of the four streams is
the empty `0x1FFFBA0` one is a number. If all four are as built and the input is a game-state value, the
campaign should re-scope milestone-2 to a game-logic effort (the disclaimer is GT4's own screen/state,
not a runtime defect — the W183/W184 conclusion).
