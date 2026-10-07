# W236 — the engine TU is dominated by a FEW giant functions; subset/wiring not reached

Goal: split output, prove ExecPS2→engine dispatch with a small subset, then scale.

## Memory

Stopped only MY compile unit (`v4-cc`) — never java/Minecraft. `free -g` went 2 GB → **12 GB
available**; all four Minecraft services stayed running.

## 1. Bounding attempts (tool changes, all OFF by default)

`tools/patches/ps2recomp-linux-w236-bounded-subset.patch` (71 lines):
- `PS2RECOMP_MAX_FUNCTIONS=N` — cap the reachability closure (BFS frontier) at N functions.
- `PS2RECOMP_NO_FALLBACKS` — drop the per-instruction indirect-fallback entry set
  (`control_flow_analyzer.cpp`), which otherwise turns every instruction of a JALR-containing function
  into an entry point.

Measured (engine, `PS2RECOMP_REACHABLE_ONLY=1`, distinct symbols):

| cap | functions generated | TU bytes | fallbacks |
|----:|--------------------:|---------:|----------:|
| 1   | 6   | 297,703,489 | 10 |
| 2   | 21  | 296,914,542 | 30 |
| 6   | 21  | 296,914,542 | 10 |
| 40  | 89  | 294,206,758 | 30 |
| 150 | 335 | 207,782,159 | 110 |
| 400 | 758 | 217,522,773 | — |

With `NO_FALLBACKS` the TU is STILL ~200–300 MB for **single-digit function counts**. The reason is
per-function size, measured line-by-line in the emitted file:

```
// Function: sub_00100008   lines 13..501        (~488 lines — the crt0)
// Function: sub_0048EF90   lines 4,536,617..5,886,103   (1,349,486 lines  <-- ONE function)
// Function: sub_005ADF20   lines 5,886,342..6,447,400   (~561,058 lines)
```

So a handful of engine functions expand to ~1.3 M lines each (the emitter writes ~30–45 lines per guest
instruction, and these functions are tens of thousands of instructions / have large computed-jump
switch bodies). **Neither function-count bounding nor disabling fallbacks bounds the output — the unit
of the problem is a single FUNCTION, not a file.** A ≤8 MB TU split therefore requires splitting *inside*
a function (or capping per-function emission), which is a larger tool change than a file split.

## 2. Wiring — NOT landed

Because no compilable engine TU was produced, the second dispatch path (harness resolution of
`[0x00100000,0x00617A14)` base `0x00100008`; ExecPS2 entering `g_ps2EngineFunctionTable` instead of
`execps2_unmapped_entry`; engine↔loader jump verification) was not implemented. It remains a LOUD
`VULCAN 4 LIMITATION`; `halt=execps2_unmapped_entry` is unchanged.

## 3. State

- Symbol split (W235): landed. Bounded-subset knobs (W236): landed, committed `b4e5061`.
- Engine TU split / compile / dispatch / boot: NOT reached. No build time (no TU compiled).
- Capture unchanged (`/mnt/ssd/vulcan4-build/run/w231b-capture.png`, disclaimer). Suite green; loader's
  723 functions untouched; no generated output hand-edited.

## 4. NEXT (bounded)

Cap the EMITTER's per-function output (e.g. a `PS2RECOMP_MAX_LINES_PER_FUNCTION` that truncates and
raises a LOUD `VULCAN 4 LIMITATION` for the truncated tail), so every function fits a bounded TU; then
the ≤8 MB file split + aggregate registration TU is mechanical, and the wiring proof can compile a
small image in minutes.
