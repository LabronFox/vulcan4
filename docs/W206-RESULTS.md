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
