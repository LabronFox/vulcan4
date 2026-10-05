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

---

## W224 (static read, per the driver's dish) — the consumer contract, from the disassembly

`sub_01008C50` (`0x1008C50`, only caller `0x10082DC` in the decoder `sub_01008080`) is a **parser-state
transition** that builds and links node records. Its four args are parser streams:
```
1008c5c: move s2,a3      ; arg3
1008c64: move s3,a0      ; arg0
1008c6c: move s4,a1      ; arg1
1008c88: move s5,a2      ; arg2
1008c80: lw   v0,4(s4)   ; v0 = [arg1+4]
1008c84: beqz v0 -> v1=1 ; if arg1's node is null, "needs build"
1008c8c: lw   v0,8(v0)
1008c90: bnez v0 -> skip ; if [node+8] != 0, else v1=1
1008c9c: beqzl v1, 0x1008e38  ; if no build needed -> DONE path
1008ca4: jal 0x101d2a0 (a0=24) ; ALLOCATE a 24-byte node
1008cb8: sw v0(1),8(s0)   ; node+8 = 1
1008cc8: sw 0,12(s0)      ; node+0x10 = 0
1008ccc: sw 0,16(s0)
1008cd0: jal 0x10059e8 (allocator) ; node data
1008d34: sw v1,4(s2)      ; link the new node into arg3's stream
1008dbc: sw v1,4(s3)      ; link into arg0's stream
```
So `sub_01008C50` **reads** the streams' current nodes (`[argN+4]`), **conditionally allocates** a node,
and **links it back** into the streams; then it calls `sub_010088E8` (at `0x1008FFC`) to merge. The
`v1` flag (`0`/`1`) selects the "done" path (`0x1008E38`) vs the "build" path.

**Contract answer:** `sub_01008C50` is a **game-logic parser step**, not runtime code. It builds a node
only when `[arg1+4]` is null / `[node+8]==0`; otherwise it takes the done path and does **not** build.
The merge `sub_010088E8` is fed whatever nodes this parser step produced. Nothing in the runtime is
involved — the streams' contents are GT4's own parse state.

**Verdict, restated with disassembly in evidence:** the merge spin is stopped by GT4's own parser state
(the decoder/`sub_01008C50` builds no second element for the `0x1FFFBA0` stream on this input), **not by
a runtime defect**. The recompiler, runtime, scheduler, GS and stores are exonerated by W171-W224.
