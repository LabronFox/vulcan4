# W117 — BOTH TASKS FAILED. HERE IS EXACTLY WHAT I COULD AND COULD NOT PROVE.

**Does the captain see a picture yet: no, not the finished one.** Real X11 window `44040199` on `:0`,
captured with `import -window`: **640x448, 16 distinct colours, mean=4193.67, stddev=13109.5**
(`docs/evidence/w117-verified.png`). Gate needs 1000, so **GATE_FAIL**. No regression: the same 16
colours GT4 disclaimer text W115/W116 produced.

Same run (budget 300000/60, `boot_w117_desk.log`): `halt=wallclock_deadline frames_presented=3145
gs_packets=3474`. Suite **493/493**, both trees clean.

## TASK 1 — ROOT-CAUSE THE DBW CRASH: FAILED

**I applied DBW=bits 46-51 and could not reproduce the crash in twelve runs, so there is no backtrace.**

What I actually did, in order:
- `gdb -batch -ex "break __chk_fail" -ex run` → `Function "__chk_fail" not defined`. Symbol unresolvable,
  so I could not stop at the fault.
- A full `gdb -batch -ex run -ex "bt 18"` run **exited normally** — no abort, no stack.
- `ulimit -c unlimited` with `core_pattern = /mnt/ssd/vulcan4-build/run/core.%e.%p`, across 12 runs.
  **No core file was ever written.**
- 4 runs at budget 300000/45 on `:0` (the crash-free shape): 0 aborts.
- 4 runs at budget 300000/60 under `xvfb-run` (the exact shape that crashed in W116): 0 aborts.
- 5 more at 300000/60 with **my own W115/W116 probes disabled**, in case my instrumentation was the
  culprit (the W116 crash tails landed next to `[frame:upload]` and `[w115:fulldest]`): 0 aborts.

So: the original observation was 2 aborts in 3 runs in W116, and it has not recurred in 12 since, across
two different displays and two budgets. I am not going to narrate a mechanism I could not observe. **DBW
stays reverted**, per the standing instruction, and the correct shift remains recorded at the call site
with its measurement (`raw 0x1280000000000` → DBW=4 authoritative, 1 under the current shift).

I should also say plainly: because the crash never reproduced, **"a latent bounds defect in the GS VRAM
write path" is no longer a supported claim.** It was an inference from two aborts I can no longer
reproduce. That is a correction to what I told you last turn.

## TASK 2 — APPLY THE CBP FIX: FAILED, AND I WAS WRONG THAT IT WAS A BUG

I landed `CBP = bits 41-54` at both sites. **9 tests failed**, and their names settle the question:

    T4 triangle sampling should fetch the manual-layout atlas texel from the correct 128x128 page
    CT16 CSM1 should retain CSA[4] instead of aliasing the upper palette onto the lower one
    T4 CSM1 lookup should keep the cached palette after its source VRAM is reused
    T4HL sampling should resolve through its own CLUT plane, unaffected by the co-resident T4HH nibble

**This codebase deliberately implements the MANUAL texture-page layout, where CBP is at bits 37-50.**
Bits 41-54 is PCSX2's AUTO layout. My W116 "BUG 1, CONFIRMED" was a correct comparison against the wrong
reference, and I reported a deliberate design decision as a defect. **Reverted.** Suite back to 493/493.

The arithmetic was never in doubt — `(raw >> 37) & 0x3FFF` gives 10378 and `(raw >> 41) & 0x3FFF` gives
648 for `raw = 0x511466942a800`. What I got wrong was assuming the hardware AUTO layout is the one this
project targets. The tests say otherwise, and the tests are older than my measurement.

## WHAT I DID LAND

Only comments recording both corrections at the call sites, so neither is rediscovered or re-"fixed":
- `gs_frontend.cpp` BITBLTBUF: DBW's correct shift, with the unreproduced crash and the twelve-run attempt.
- `gs_frontend.cpp` TEX0 (both sites): CBP at 37-50 is the deliberate MANUAL layout, not a bug.

Plus one new instrument, `w116:tex0atlas`, which prints the raw qword beside the decoded fields. It is
what exposed both mistakes — in both directions. It is also the fifth probe in this project that found a
real fault by printing its work rather than its conclusion.

## TASK 3 — NOT ATTEMPTED

I did not drive the boot further. With DBW reverted (so atlases are still scattered by `dbw=1`) and the
palette still empty at both candidate addresses, the next screen was not reachable in the time left, and
guessing at it would have been another conclusion without a measurement behind it.

## WHERE THE WALL ACTUALLY STANDS, with everything now measured

1. The window works. It has shown GT4's own 2005 Sony disclaimer text at 640x448 since W115.
2. The glyph atlas **is** written: `dbp=10240`, 28,672 pixels copied, **9,005 non-zero**, verified by
   reading every covered pixel back immediately after the write.
3. The atlas **is** scattered by a layout mismatch whose fix is known (`dbw` 46-51, DBW=4) and blocked on
   a crash nobody can reproduce.
4. The palette **does not exist** at either candidate CLUT address — 4096 entries, all zero, at both
   `cbp=648` and `cbp=10378` — and no transfer writes to either.

Item 4 is the one I would attack next, and it does not depend on item 3. The texels for the CLUT are most
likely inside the 114,688-byte payload we already receive, positioned after the texel run, and our
`totalPixels` bound stops before reaching them; or they are in a transfer that has not been observed yet.
That is a measurement, not a guess, and it is the next single step.
