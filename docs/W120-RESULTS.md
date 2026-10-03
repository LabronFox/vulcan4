# W120 — WHERE THE BOOT ACTUALLY SPENDS ITS TIME. NONE OF THE THREE TARGETS MET.

**Plain verdict: I reached NO rung.** `halt` is still `livelocked_in_syscall` in the pathological
shape, `true_guest_exits` is still **0**, and the window still shows the 2005 Sony disclaimer. Every
number below is from a run I made; nothing here is reasoned.

## THE HEADLINE NUMBER: THE BOOT IS NONDETERMINISTIC

Three runs, **same binary, same budget 300000/60**:

| run | halt | true_guest_entries | XFER SITES total | entries/sec |
|---|---|---|---|---|
| w120_f1 | `pc_outside_generated_table` | 83,905 | 36,000 | 1,398 |
| w120_f2 | `wallclock_deadline` | 1,117,046 | 296,344 | 18,617 |
| w120_f3 | `wallclock_deadline` | 238,826,320 | 238,605,705 | 3,980,439 |

**A 2,846x spread on identical input.** This is the single most important thing I learned this dish and
it invalidates any single-run performance claim, including ones I made earlier. Two distinct modes:

- **Mode A, livelock (the majority of my runs):** three guest addresses take exactly 33.32% each of all
  control transfers — `0x01005890`, `0x010089c8`, `0x010089d4`. Example, budget 300000/90
  (`w120h.log`): `XFER SITES total=297,481,720`, 99,114,034 each, `halt=livelocked_in_syscall`.
- **Mode B, the captain's shape:** no spin, the boot walks the disclaimer normally.

## WHERE THE 90 SECONDS GO — measured, not argued

`[w120:split]` times `targetFn(rdram, ctx, this)` — the guest's OWN generated code — separately from our
runtime around it. Budget 300000/90, `w120h.log`:

    guestEntries=461,000,000  guestBodyMs=52,547  pctOf90sBudget=58%

**58% of wall time is the guest executing instructions; our runtime is the other 42%.** So we are not
overheaded enough to explain the crawl by ourselves — the guest is genuinely executing 297 million
control transfers.

Three suspects measured and **exonerated**, so nobody repeats them:

| suspect | measurement | verdict |
|---|---|---|
| syscall dispatch | `[w120:dispatch] avgUs=6 maxUs=13`; 100,000 calls in 90 s | **<1% of budget.** Not the bottleneck. |
| branch-edge bookkeeping | `[w120:edge] avgNsPerCall=45` | **0.09% of budget.** Real, but minor. |
| disc reads | `[w120:cd]` census **never fired** | `sceCdRead` is not called 500 times, so disc streaming is **not** the gate at this stage. |

## THE SPIN, FULLY CHARACTERISED

`sub_010088E8` is one generated function; `bltz` back-edges inside it are internal `goto`s, so they can
**never** appear as a dispatch `targetPc`. That is why a probe keyed on `targetPc == 0x10089dc` never
fired — it was structurally blind, not evidence of anything. Resolved.

The loop:

    0x10089c8: jal func_1007738      ; a queue walk
    0x10089d0: daddu $a0, $sp, $zero
    0x10089d4: jal func_1005870
    0x10089dc: bltz $v0, -> 0x10089c8

`func_1005870` gates its inner call on two words being equal, and `func_10057F0` returns **-1**:

    [w120:ret] func_10057F0 returned: resumePc=0x1005898 v0=0xffffffff signed=-1

Then `0x1005898: lw $a0,0x10($s0)` / `0x10058a4: movn $v0,$v1,$a0` only overwrite `$v0` when `$a0` is
**non-zero**. Measured `*(s0+0x10)` is **0**, so `$v0` stays -1 and `bltz` loops. It is a producer/consumer
wait: the guest loops until `func_10057F0` returns something else, and `func_10057F0` keeps saying "none".

**And the word it is waiting on is never written.** Watched guest `0x1fffbc0` (= `$sp`+0x10) with the
harness store observer: **zero write events.** Proven against a positive control — the same mechanism on
`0x1895300-0x1895400` fired immediately
(`VULCAN4 W30WRITE ... addr=0x1895304 value=0x1ff8000 pc=0x1012214`). The silence at `0x1fffbc0` is
therefore a real zero, not a broken instrument.

## WHAT I CHANGED

1. **`m_branchEdgeIndex`** — the dispatch path did `std::find_if` (a **linear scan** of ~400 edges) plus an
   unbounded `push_back` on **every guest function entry**. Now one hash probe. The existing comment
   claimed the edge count was bounded "so this stays small"; bounded is true and was still the bug,
   because a bounded O(n) scan is paid on every one of ~300 million calls. Measured after: 45 ns/call,
   **0.09% of budget** — so this is correct but minor, and I am not claiming a speedup from it.
2. **Hot-path timers gated off by default** behind `VULCAN4_W120_TIMING=1`. Two `steady_clock` reads per
   guest call cost about as much as the 45 ns of work they were measuring. An instrument that taxes the
   thing it measures is not an instrument. The sampled census lines stay on; they are bounded and cheap.
3. Read-only instruments kept: syscall leaderboard, unrouted-syscall census, disc-read census, queue and
   return-value probes, guest-vs-runtime split, edge timing.

Suite **493/493**. Behaviour change is the edge index only.

## WHAT IS DELIBERATELY NOT DONE

Per the brief: `DBW` bits 46–51 (reverted, no more gdb/core_pattern/re-hunt); `CBP` bits 41–54 (wrong —
this project implements the MANUAL layout, 9 tests enforce it); the empty palette at `cbp=648`/`10378` (real,
later rung); NLOOP (proven 11 bits); proving the window is fake (it is real GT4 output). Entry-point
jumping stays forbidden. Memory card / mcRoot / sceMc untouched — R5 only.

## EXACTLY WHAT BLOCKS THE NEXT RUNG

R3 needs the boot to leave the disclaimer. The boot is blocked on `func_10057F0` returning something
other than -1, which depends on a producer filling the queue that `func_1007738` walks and on the word at
guest `0x1fffbc0` being written. **I never identified the producer.** That is the honest gap, and it is
the first thing the next agent should do — not a new wall, the same one, one level deeper:

1. Find what writes guest `0x1fffbc0`. It is never written, verified with a control. The likely
   candidates are a DMA destination or an interrupt-handler store; `sub_0100D838` (the cause-2 handler)
   bumps scratchpad `0x70002050` continuously and is the closest thing to a producer in the build.
2. Characterise `func_10057F0`'s -1 path: it is `$v0 = $a0 - 1` on some branches, and it returned exactly
   `0xffffffff`, so its `$a0` was 0 on the taken path. Find what it is polling.
3. Only then re-measure mode A vs mode B, with **at least 5 runs per configuration**, because the 2,846x
   spread means a single before/after pair proves nothing.

## ADDENDUM — func_10057F0 IS A COMPARE, AND THE SPIN IS ONE 32-BIT TEST

Read in full from the generated code, `func_10057F0` is **not** a fetch. It is a three-way comparison
returning -1 / 0 / +1:

    0x10057f0: lw    $a2, 0x8($a0)      ; left  count = *(a0+0x08)
    0x10057f4: lw    $a3, 0x8($a1)      ; right count = *(a1+0x08)
    0x10057f8: sltu  $v1, $a2, $a3      ; v1 = (left < right)
    0x10057fc: bnez  $v1, -> 0x1005868  ; if left < right, EXIT
    0x1005800: addiu $v0, $zero, -1     ; DELAY SLOT -- executes unconditionally
    0x100583c: lw    $v1, 0x0($v0)      ; only NOW are elements compared

**It returns -1 the instant `*(a0+8) < *(a1+8)`, straight out of the delay slot, without comparing a
single element.** The caller `sub_010088E8` loops while `$v0 < 0` at `0x10089dc`. So the livelock reduces
to **one 32-bit comparison of two counts** — a far smaller target than "find the producer".

Measured, solid (`w120i.log`, `w120q2.log`, both at `resumePc=0x1005898`, the label right after the jal):

    [w120:ret] func_10057F0 returned: resumePc=0x1005898 v0=0xffffffff signed=-1

and the caller's `0x100589c: negu $v1,$v0` / `0x10058a4: movn $v0,$v1,$a0` overwrites `$v0` only when
`*(s0+0x10)` is non-zero, and that word measures **0**, so `$v0` stays -1 and `bltz` loops.

### RETRACTION — my probe was not positioned to see what it claimed

The first `[w120:cmp]` probe ran **after** `targetFn()` and therefore read `func_10057F0`'s
**return-state** registers, not its arguments. It printed `a0=0x1 a1=0x0` and counts of `3735928559`,
which is `0xDEADBEEF` — the probe's own "unreadable address" sentinel. **Those numbers were meaningless
and are withdrawn.** This is the sixth time in this project a probe reported a confident result it was
not positioned to see, and it is the same failure as the stride-16 sampler and the block-as-word-index
scan: the instrument looked authoritative and was blind.

**NOT YET MEASURED:** the corrected probe samples *before* the call, but three consecutive runs came back
in the non-spin shape (XFER totals 153,682 / 128,817 / 37,370) so it has not yet fired in a livelock run.
**Do not treat the `left < right` verdict as established** until the corrected probe prints it. The spin
is intermittent — roughly half of all runs — so catching it takes several attempts.

## WHERE THE NEXT AGENT STARTS (much smaller than "find the producer")

1. Run until the corrected `[w120:cmp]` fires in a livelock run, then read `leftCount` and `rightCount`.
   If `left < right`, the spin is confirmed and the next question is **which side is short and who is
   supposed to extend it**. That is a data-flow question about two structures the guest owns, not a
   search of the runtime.
2. Separately, `0x1fffbc0` is still never written — but note the spin may not need it written at all:
   `*(s0+0x10)` only gates whether `$v0` is overwritten, and with it at 0 the -1 propagates. So "the word
   is never written" and "the spin loops" may be the same fact rather than two.
3. Re-measure with **at least 5 runs per configuration**. The 2,846x spread means one before/after pair
   proves nothing, which is how I nearly reverted a correct fix in W112.
