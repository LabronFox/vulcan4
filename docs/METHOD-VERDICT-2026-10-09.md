# THE METHOD VERDICT — are we actually debugging? (2026-10-09, Caine, at the captain's question)

The captain asked: *"Are we actually debugging? I feel like we did something wrong with the way we're
working."* He is right, and this is the specific wrong thing.

## The loop we have been running

1. Boot the recomp. Find the address where the guest spins (today: `0x5608e0`, tag `0x00874304`).
2. Form a plausible hardware story about that address (scheduler starvation, round-robin, LGDEV reply).
3. Instrument OUR runtime until the story looks confirmed.
4. Never check the story against the reference machine, which is **running on the same box the whole
   time** (`pcsx2-qt -debugger`, Ghidra with `SCUS_973.28`, the PCSX2 DebugServer MCP).

That is not debugging. That is hypothesis-writing with a runtime as the only witness. The project's own
record already says so — `docs/W277-RESULTS.md` and the W7 post-mortem both contain the same sentence in
different words: *"five passes of instrumentation had built a story, and every part of that story was
wrong."*

## The cost, measured

- **2026-10-09:** 26 commits, 2 walls measured and retracted (producer starvation, round-robin
  scheduling), **0 picture change**. The two retractions were honest and cheap; the four hours before
  them were spent in our own runtime rather than in the oracle.
- **Today's gate was self-referential too:** captures were a 5%-brightness ghost of the screen while the
  reference was the bright one — fixed in `5c09ce8`. A gate that compares our own two capture paths
  cannot tell us what the game did.
- **The gap audit (12:00 today)** answered the question we should have asked in September: every layer
  we are hand-writing already exists, proven, in `/mnt/ssd/tools/pcsx2-src` — `Sif0/Sif1/sif2`, `Dmac`,
  `IopHw/IopIrq/IopCounters`, `Counters`, `Gif`. We have been re-deriving them from the guest's spin
  addresses instead of reading them.

## The rule that changes as of now (proposed law, VULCAN 4 AGENTS)

**NO WALL WITHOUT AN ORACLE READING.**

- A wall is not accepted until the same moment has been reproduced in the oracle and the disagreement is
  named as a **value**: register, memory word, DMA transfer, or interrupt line — with the oracle's value
  and ours side by side, in the commit.
- A wall report that names only a guest address is a HYPOTHESIS, not a wall, and does not count as
  progress.
- Before inventing a model of any hardware mechanic, read the PCSX2 implementation of it and cite the
  file + function.
- The only progress metric that counts: **the picture changes, or the divergence moves closer to the end
  of boot.** Commit count is not progress.

## The strategic consequence (one line, for the captain's decision)

If we are going to port the hardware's semantics anyway, the honest question is whether we keep growing
our own SIF/DMAC/IOP/GS layer at all, or take PCSX2's proven cores and drive them with our recompiled EE
code — replacing its JIT instead of re-inventing its hardware. That decision is the captain's; this
document exists so it is a decision about a measured state, not about a feeling.
