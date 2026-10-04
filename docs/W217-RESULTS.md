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

## W218b — the producer NAMED and the element values MEASURED

`func_10057F0`'s `+0x14` fields point into **sorted singly-linked lists** built by the parse's insert
routine `sub_010121F8` (`0x10121F8`):

```
10121f8: lw    v1,4(a0)      ; node->sortkey
10121fc: addu  a0,a1,a2
1012200: lw    a3,4(a1)
1012204: sltu  v1,a3,v1
1012208: beqz  v1,0x1012214
101220c: move  v0,a0
1012210: sw    a0,0(a3)      ; link
1012214: sw    a3,4(a0)
1012218: sw    a0,4(a1)
```
and the store watch (`VULCAN4_W217_PROD`) saw exactly those writers:
```
[w217:prod] addr=0x1895204 val=0x1895230 writerPc=0x1012218   (+0x14 of the a1 structure)
[w217:prod] addr=0x1895204 val=0x1FF8000 writerPc=0x1012214
[w217:prod] addr=0x1FFFBB4 ... writerPc=0x101150C              (+0x14 of the a0 structure)
```

The element words themselves (`[w218:cmp]`, shape `w235e` `livelocked_in_syscall`):

```
[w218:cmp] a0=0x1fffba0 a1=0x1895310 c0=1 c1=1
           e0lo=0x1895480 e1lo=0x1895430
           e0[0]=0x0 e1[0]=0x1 e0[0]+4=0x0 e1[0]+4=0x0
```

So `func_10057F0` compares **`0x0` vs `0x1`** — two different sorted-list keys — and returns -1 on
`0x0 < 0x1` (matching `ret_neg`) for all ~50M calls. The lists are built once by the parse's node
builder (`0x10121F8`/`0x1012218`, `0x101150C`) and never made equal.

**The producer is the parse's sorted-list insert `sub_010121F8` — it builds two different-keyed lists
for the two structures, and `func_10057F0` is comparing keys from two lists that were never meant to be
equal.**

## NEXT
`func_10057F0` is being used as an equality/ordering test between two *different* lists; either the
consumer's loop is a merge that should advance the list (and the spin is the merge making no progress
because both lists are length 1 and already ordered), or the caller is asking the wrong predicate.
The merge's input (`func_1007738`'s stream at `0x1FFFBA0`, bound `+8=1`) is the next measurement: what
should advance it. This is the same `0x1fffba0` wire W122 chased.
