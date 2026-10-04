# W218 — the spin's -1 comes from `func_10057F0`'s ELEMENT-ARRAY compare; W216 REFUTED

**Dish:** `18-spin-compare-producer` (re-run, disambiguation). **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the exit predicate is **disambiguated by counters over a full run**, and W216's claim
is **REFUTED**.

## The contradiction settled

W216 claimed "the UNEQUAL `+0x10` `bne` at `0x1005888` returns -1". W216's own probe printed
`equal=1` (both `+0x10` = 0), which means that `bne` is **not taken**. The new counters
(`VULCAN4_W215_SPIN`, `[w218:pred]`/`[w218:cmp]`) settle it, on four spin shapes:

```
[w218:pred] c5870=49973317 bnePredTaken=0 c57f0_fallthrough=49973317
            ret_neg=49973317 ret_zero=0 ret_pos=0 exits=0
            [a0+10]first=0x0 min=0x0 max=0x0 [a1+10]first=0x0 min=0x0 max=0x0
[w218:cmp]  a0=0x1fffba0 a1=0x18951f0 c0=1 c1=1 e0lo=0x1895360 e1lo=0x1895310
```

- **`bnePredTaken = 0`** over ~50 million calls: the `0x1005888 bne` (`[a1+0x10] != [s0+0x10]`) is
  **never taken**; both `+0x10` fields stayed `0x0` for the whole run (min=max=0).
- **`c57f0_fallthrough = c5870`**: every call falls through to `func_10057F0`.
- **`ret_neg = c57f0`** (all 49.97M returns are **-1**), `ret_zero=0`, `ret_pos=0`.
- **`exits = 0`**: the loop never exits (the `0x10077B0` exit target never appears).

**So the -1 is `func_10057F0`'s return from its `+0x14` ELEMENT-ARRAY compare** — `e0lo=0x1895360` vs
`e1lo=0x1895310` differ — exactly as the dish brief predicted. W216's `0x10058A8 li v0,-1` path is
**REFUTED** (it is never reached).

## Why the W216 probe could not fail

`[w216:exit]` printed `v0=-1` at `target=0x1007738`, the loop-body call reached from **either** path
(taken → `0x10058A8 li -1`; not-taken → `func_10057F0` return, also -1). A probe that prints -1 on both
paths proves nothing about the mechanism. The counters above split the two by dispatch, which is why
they can.

## The instrument defect I made and fixed

My first disambiguation attempt had the element comparison **backwards** (`if (e1<e0) r=-1`), producing
`ret_pos` — a wrong prediction that would have hidden the answer. Matching the generated code
(`e0<e1 → -1`) gives `ret_neg` and agrees with the observed `-1`. Recorded because it is the same class
of error this project keeps paying for: an instrument that can only reproduce its own assumption.

## Positive control

- The counter `exits` and the `0x10077B0` target are live observables; `ret_neg`/`ret_zero`/`ret_pos`
  are three separate buckets, so a constant -1 and a mixed stream are distinguishable. In these shapes
  it is uniformly -1 — a real result, not a stuck sample, because `c5870` climbs to 49.97M.
- The store watch (`VULCAN4_W217_PROD`) independently fired on `0x1895200` (a target word), proving it
  sees target writes.

## The wall, in one line
`sub_010088E8`'s loop never exits because `func_1005870` falls through to `func_10057F0`, whose
`+0x14` element arrays (`[0x1FFFBA0]`'s vs `[0x18951F0]`'s) never compare equal — it returns -1 on
~50M calls — and nothing advances those arrays.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
Find what writes the two `+0x14` element arrays (`e0lo`/`e1lo`, currently `0x1895360`/`0x1895310`, which
move between runs) and why they never become equal. A store watch on the array bases and their
`+0x14` pointer fields names the producer.
