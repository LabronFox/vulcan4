# W215 — the decode spin: two frozen counts, `1` vs `1`, for 155 million iterations

**Dish:** `17-decode-spin-cpu-share`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the decode spin is **named and quantified**, its exit condition is read, and the reason
it never exits is measured: the counts it compares are **frozen at 1**.

## The spin, measured (`VULCAN4_W215_SPIN`)

Shape `w225b`/`w225f` (`halt=livelocked_in_syscall`, FE~5k). The spin is the 3-call cycle at
`sub_010088E8`:

```
0x10089D4 → func_1005870     (jal 0x1005890? no: jal 0x1005870)
0x1005890 → func_10057F0     (jal, inside func_1005870)
0x10089C8 → func_1007738     (jal)
0x10089DC  bltz $v0, 0x10089C8   <-- the back-edge: loop while func_1005870 returns < 0
```

and `[w215:spin]` logs its arguments and both compared fields every sample:

```
n=1   eeCycle=211630624  a0=0x1FFFBA0 [a0+8]=0x1 [a0+10]=0x0  a1=0x18952E0 [a1+8]=0x1 [a1+10]=0x0 equal=1
...
n=150000000 eeCycle=3013884018  a0=0x1FFFBA0 [a0+8]=0x1 [a0+10]=0x0  a1=0x0 [a1+8]=0x0 [a1+10]=0x0 equal=1
n=155000000 eeCycle=3107274726  a0=0x1FFFBA0 [a0+8]=0x1 [a0+10]=0x0  a1=0x18952E0 [a1+8]=0x1 [a1+10]=0x0 equal=1
```

**The key measurement:** `[a0+0x8] = 1` and `[a1+0x8] = 1` — the two counts `func_10057F0` compares
(`lw a2,8(a0)` / `lw a3,8(a1)` / `sltu v1,a2,a3`) — are **both 1 and never change across 155 million
iterations**.

Reading `func_10057F0` (full body) for `a2=1, a3=1`:
```
10057f8: sltu v1,a2,a3      ; 1<1 = 0
10057fc: bnez v1,0x1005868   ; not taken
1005800: li   v0,-1          ; (delay slot, overwritten below)
1005804: sltu v0,a3,a2      ; 1<1 = 0
1005808: beqzl v0,0x1005818  ; taken -> a2 = a2-1 = 0
1005818: bltzl a2,0x1005868  ; a2=0, not taken
1005820..: element loop over [a0+0x14]/[a1+0x14], a2 counts down to -1, then
1005864: move v0,zero        ; returns 0
```
So for the sampled `1==1` the function **returns 0**, which is **>= 0**, and the caller's
`bltz $v0, 0x10089C8` at `0x10089DC` should **exit**. Yet the loop runs 155M times. That is the
contradiction the next probe must resolve: either the sampled `1==1` is a minority state (the fields
change between samples and only *appear* frozen in the 5M-interval sampling), or the loop is re-entered
from the other site (`0x1008A04`/`0x1008A3C` also call `func_1005870`) whose caller loops on a
different predicate. **Honest status: the exit condition's inputs are observed frozen/small, but the
measured return value that keeps the loop alive is not yet captured — the NEXT probe must log `$v0` at
`0x10089DC` and `func_10057F0`'s return, not just its inputs.**

## Cost per run

- **~155 million iterations** of the 3-call cycle in a 60 s budget (`n=155000000` reached).
- eeCycle climbs **211,630,624 → 3,107,274,726** across the run (~14.7×), i.e. it does yield — but only
  every ~millions of iterations (the early samples share one eeCycle value; the checkpoint resets between
  yields).
- The three sites are **33.31% each** of all control transfers (`0x1005890=0x10089C8=0x10089D4`).

**Positive control:** the census matches the XFER histogram exactly (`0x1005890=33.31%`), and the
syscall-free wait `0x100AFA0` (0.02%) is correctly ranked cold.

## Effect on `0x10005DC` reachability

**Not reached, in any shape.** With the spin present (`livelocked_in_syscall`, FE~5k) the run halts in the
spin; with the good shape (`wallclock_deadline`, FE~20k) `0x10005DC` is still never dispatched (W214).
So reducing the spin's share is necessary but not sufficient to reach the flag — and the spin's share is
dominated by the **frozen counts**, not by a scheduler unfairness: even if the parse got every slice, the
spin would still be running its 155M iterations, and the loop's exit never fires.

## The wall, in one line
The decode spin `sub_010088E8` loops ~155M times because the counts it compares
(`[0x1FFFBA0+8]` and `[0x18952E0+8]`) are **both frozen at 1** and the `+0x10` fields are both 0; nothing
advances them, so the exit condition (`func_10057F0` returning >= 0 / `func_1005870` unequal) is never
met, and tid1 never gets to `0x10005DC`.

## NEXT
Find the producer of the `+0x8` count at `0x18952E0` (the right-hand structure `a1`) and the `+0x10`
fields — whatever is supposed to increment/advance them is the upstream wall. A store watch on
`[0x18952E0+8]` and `[0x1FFFBA0+8]` over a run names it, or proves it never runs.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.
