# W210 — the setup tree COMPLETES fast; the wall is the frame pump, and its waits are real syscalls

**Dish:** `14-corrupted-ra-race-deterministic` (re-run). **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** (1) the setup subtree is **not** the bottleneck — every node completes within ~750 ms;
(2) the RTOS waits `0x100AFA0`/`0x100B678` are **real `SleepThread` syscalls that yield**, not busy
spins. The starve hypothesis (H-starve) — for these two sites — is **refuted**.

## 1. The whole setup subtree, per node (`VULCAN4_W210_TREE`)

In good shapes (`w210j`/`w210k`, `FE~7900`, `halt=livelocked_in_syscall`) the scoreboard prints at halt:

```
[w210:node] pc=0x1000558 caller=0x1000210 count=1 first_ms=5   last_ms=5
[w210:node] pc=0x10047c0 caller=0x10005d4 count=1 first_ms=26  last_ms=26
[w210:node] pc=0x1004500 caller=0x10047ec count=1 first_ms=26  last_ms=26
[w210:node] pc=0x1004308 caller=0x100451c count=1 first_ms=26  last_ms=26
[w210:node] pc=0x100f8c8 caller=0x10043a4 count=1 first_ms=26  last_ms=26
[w210:node] pc=0x1010b10 caller=0x1004368 count=1 first_ms=27  last_ms=27
[w210:node] pc=0x100ed78 caller=0x100438c count=2 first_ms=16  last_ms=27
[w210:node] pc=0x100f390 caller=0x1010a68 count=49 first_ms=16 last_ms=719
[w210:node] pc=0x1010bd0 caller=0x1004424 count=8  first_ms=22 last_ms=725
[w210:node] pc=0x101d2a0 caller=0x1008f5c count=35 first_ms=6  last_ms=741
```

**Every node returns.** `0x100F390` is entered 49 times and its `last_ms=719` — it is the decoder's
internal loop, and it finishes. `0x1010BD0` (`count=8,last_ms=725`) and `0x101D2A0`
(`count=35,last_ms=741`) likewise complete. So the setup tree is **not** an infinite loop and **not**
the dominating cost — the wall is later, in the frame pump.

`w210h`/`w210i` were `livelocked_in_syscall` and printed no scoreboard (their sub_01000558 never ran to
completion), so the positive control is the `wallclock_deadline` shapes above: the probe **does** see the
tree complete.

## 2. (H-starve) vs (H-long) at `0x100AFA0` — MEASURED

`0x100AFA0` is a `jal` to `0x101F340`, and `0x101F340` is the **`sce_SleepThread` wrapper**
(`li v1,50 ; syscall` — syscall 0x32). So it is a **syscall that yields**, not a spin. The surrounding
loop (`0x100AF50 bgez s3 -> 0x100AFA0`) polls a byte at `[s0]` after each sleep — a bounded retry loop,
not a busy spin.

The other dominant site, `0x100B678` (`jal 0x101F340` inside `sub_0100B628`), is the same: a
`SleepThread` inside a wait loop that exits when `[0x70002050]` changes
(`0x100B66C bne s1,v1 -> 0x100B68C`). **Both top transfer sites are real waits that call the scheduler,
not busy spins.**

**Verdict: H-starve is refuted for `0x100AFA0`/`0x100B678`** — they yield. The transfer histogram
counts the *entry* to the sleep wrapper, which is why a yielding wait and a spin look identical in the
histogram; the disassembly + the syscall number settle it. So W207/W208's "tid1 is starved against the
render thread's RTOS vsync wait" was **an inference the histogram could not support**, and it is now
corrected: the render thread is **waiting**, and tid1 gets slices (W173: both threads alternate).

## 3. W204 live suspect (async-pool / main-stack overlap)

Still real and unlanded: the invocation-stack pool's first stack `[0x1FFBFF0,0x1FFFFF0]` sits inside the
main thread's frame (`$sp=0x1FFFFFF0`). It is **not consistent with the setup tree looping** (the tree
completes), but it can corrupt tid1's frame on a VSync-handler window and is a candidate for the
`livelocked_in_syscall` shapes. Left for a dedicated dish.

## Corrected wall, in one line
The setup tree `sub_01000558 → … → sub_0100F390` **completes in ~0.75 s**; the disclaimer flag
`0x1000E00` is still never reached because the run's dominant cost is the frame pump's waits
(`0x100AFA0`/`0x100B678` = ~70 % of transfers) and the total budget is spent before the setup's later
calls; the top waits are **real `SleepThread` yields**, not spins.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
The frame pump: `0x100AFA0` (poll loop `[s0]`) and `sub_0100B628` (`[0x70002050]` wait) are called tens
of thousands of times — find who is meant to advance `[s0]`/`[0x70002050]` (the "what releases the wait"
question), and why the disclaimer's later setup calls (`0x1004424 → 0x1010BD0` etc.) are entered only 8
times in a whole run. That is the lever.

---

## W211 — the (H-starve)/(H-long) number: `0x100AFA0` costs 336–680 eeCycles per visit and yields

`VULCAN4_W210_TREE` now also logs the eeCycle delta at each visit to the two top waits. Measured
across four shapes (`w214a` `livelocked_in_syscall`, `w214b/c` `pc_outside`, `w214d` `wallclock_deadline`):

```
[w211:afa0] n=1 eeCycle=6929657 delta=6929657 tid=1
[w211:afa0] n=2 eeCycle=6930140 delta=483     tid=2
[w211:afa0] n=3 eeCycle=6932532 delta=2392    tid=2
[w211:afa0] n=4 eeCycle=6933212 delta=680     tid=2
[w211:afa0] n=5 eeCycle=6933652 delta=440     tid=2
[w211:afa0] n=6 eeCycle=6933988 delta=336     tid=2
[w211:afa0] n=7 eeCycle=6934668 delta=680     tid=2
[w211:afa0] n=8 eeCycle=6935108 delta=440     tid=2
```

- After the first visit (tid1, at the RTOS setup), **every visit is on tid2** and costs **336–680
  eeCycles**. A `sce_SleepThread` that busy-spun or failed to yield would produce a large or
  near-zero-interval delta; a **sub-1000-cycle delta per visit is the signature of a wait that yields
  and returns a fresh slice**, exactly as W210's disassembly showed (`0x100AFA0 = jal 0x101F340`,
  which is `li v1,50 ; syscall`).
- So **H-starve is refuted by number, not just by disassembly**: the render thread is not burning
  cycles in the wait; it hands the CPU off each visit.

## W211 — `0x1000E00` is unreachable in these runs (the positive-control gap)

`0x1000E00` (which sets `[0x1047A84]=1`, the disclaimer-exit flag) has **one caller**, `0x1000618`
inside `sub_01000558`. The subtree scoreboard shows `sub_01000558` (`0x1000558`, `count=1`) and its
chain to `0x10043A4 → 0x100F8C8`, but **never** `0x1000608`/`0x1000610`/`0x1000618` (the tail that
follows `sub_010047C0`'s return). `[w211:tail]` fires **0 times** in every run. So the flag is not
merely late — its call site is never reached, because `sub_01000558`'s next statement after the
`0x10005D4 → 0x10047C0` call (i.e. `0x1000608`) is never dispatched.

**Positive control:** the probe *does* see the chain complete when it happens (the `w210j/k/w213b`
scoreboards print every node with a `last_ms`), so a `[w211:tail]` count of 0 is a real "this call site
is never reached", not a blind probe.

## W211 — the W204 async-pool overlap, re-checked

Not consistent with the setup tree looping (the tree completes in ~0.5–0.75 s and no node loops). It is
still a real defect (the pool's first stack sits in the main frame) and a candidate for the
`livelocked_in_syscall` shapes, but it is **not** what keeps `sub_01000558` from reaching `0x1000608`.

## Corrected wall, in one line
`sub_01000558` reaches the`0x10005D4 → 0x10047C0` call, whose tree completes, but the statement after
it (`0x1000608`, which leads to `0x1000E00` and sets the disclaimer flag) is **never reached**; the top
wait `0x100AFA0` is a **yielding SleepThread (336–680 cycles/visit)**, not a starve source.

---

## W215 — the decode spin measured: 3-site loop, `+0x10` fields equal, `func_10057F0` returning the spin

`VULCAN4_W215_SPIN` logs the three spin sites (`0x1005890 → func_10057F0`, `0x10089C8 → func_1007738`,
`0x10089D4 → func_1005870`) with their arguments and the eeCycle. Shape `w224b` (`FE=4959`,
`halt=livelocked_in_syscall`) has the spin (`0x1005890=0x10089d4=0x10089c8 = 33.31% each` of ~117M
transfers).

```
[w215:spin] n=1  eeCycle=211632465 from=0x10089d4 target=0x1005870 a0=0x1fffba0 [a0+10]=0x0 a1=0x18952e0 [a1+10]=0x0 equal=1 ra=0x10089dc
[w215:spin] n=2  eeCycle=211632465 from=0x1005890 target=0x10057f0 a0=0x1fffba0 [a0+10]=0x0 a1=0x18952e0 [a1+10]=0x0 equal=1 ra=0x1005898
[w215:spin] n=3  eeCycle=211632465 from=0x10089c8 target=0x1007738 a0=0x1fffba0 [a0+10]=0x0 a1=0x0      [a1+10]=0x0 equal=1 ra=0x10089d0
...
[w215:spin] n=115000000 eeCycle=2360018086 from=0x10089d4 target=0x1005870 a0=0x1fffba0 [a0+10]=0x0 a1=0x18952e0 [a1+10]=0x0 equal=1
```

- **The loop is a tight 3-call cycle** — `0x10089D4 → func_1005870`, `0x1005890 → func_10057F0`,
  `0x10089C8 → func_1007738` — and it runs **~117 million times** in the run.
- **The compared fields are `0` on both sides** (`[a0+0x10]=0x0`, `[a1+0x10]=0x0`, `equal=1`) in the
  sampled window. `func_1005870` returns the **equal** branch (`jal func_10057F0`), and `func_10057F0`
  compares the two `+0x8` counts; the caller loops while `$v0 < 0`. So the exported party is a **count at
  `+0x8`** (`func_10057F0`) whose left side never reaches the right side.
- **It yields, but only slowly:** the eeCycle sampled at the logged entries climbs from **211,632,465 to
  2,366,018,086** across the run (≈11×), i.e. the scheduler does advance time between checkpoints. The
  `n=1..27` samples share `eeCycle=211632465` (the checkpoint resets between yields), which is why the
  loop dominates XFER — each yield runs millions of the loop's iterations.

**Positive control:** the census sees a known-hot site (`0x1005890` = 33.31%, matching the XFER
histogram) and the syscall-free `0x100AFA0` wait (0.02%), so the ranking is trustworthy.

## The wall, in one line
The shape-A livelock is the 3-call decode cycle at `sub_010088E8` (`0x1005890`/`0x10089C8`/`0x10089D4`),
117M iterations/run, comparing the `+0x10` fields (both 0) and the `+0x8` counts of two structures; it
yields only every ~millions of iterations, so it consumes the budget before tid1's `sub_01000558` can
finish — `0x10005DC` is never reached.

## NEXT
Find what writes the `+0x8` count `func_10057F0` compares and the `+0x10` fields `func_1005870` compares
(`a1=0x18952E0`, `a0=0x1FFFA0`), and why they stay equal/zero; that producer is upstream of the spin.
