# W172 — Q1 and Q2 answered: the decode is handed an empty node whose size is set LATER

**Dish:** get the decode to complete so the loading screen ends. **Result:** ❌ no fix landed and the
gate still fails — but the two questions W171 left are answered with instructions, and the answer is
**"the guest routes an empty node, and the translation is faithful"**, so there is no recompiler bug
here and no honest one-line fix was found. Reported as measured.

## Q1 — WHO WRITES THE DECODER'S ZERO RECORD?

A store watch over the **whole** `0x1895000-0x1896000` page (`VULCAN4_W163_WATCH`, off by default,
100k lines, every writer PC + size + value), on a run where the decoder is reached:

```
decoder stream node 0x1895390, data(+0x14)=0x18953c0, dataw: 0 0 0 0 ...

writes to 0x18953c0..0x18953df:
  addr=0x18953c0 size=4 val=0x0     writerPc=0x1008ce0   <-- word 0, the value the decoder rotates
  addr=0x18953d0 size=4 val=0x18953b0 writerPc=0x1012220
  addr=0x18953d4 size=4 val=0x1ff8000 writerPc=0x1012214
  addr=0x18953d8 size=4 val=0x0     writerPc=0x10122fc
  addr=0x18953dc size=4 val=0x0     writerPc=0x1012304
```

**Word 0 of the decoder's record is written by exactly ONE instruction: `sub_01008C50` at
`0x1008ce0`, `sw $zero,0($v0)`** — it zeroes the data buffer of the node it JUST allocated
(`0x101d2a0` at `0x1008ca4`, data at `0x10059e8`). The link fields at `+0x10..+0x1c` come from the
tree-insert routines (`0x1012214`-`0x1012304`); nothing else writes word 0. So the record is empty
**by construction**, not by a lost copy.

## Q2 — WHY DOES THE GUEST PICK THE EMPTY ONE? (MIPS quoted)

`sub_01008C50` (0x1008c50), the branch that chooses the empty path:

```
1008c80: 8e820004  lw   v0, 4(s4)        ; v0 = *(a1+4)
1008c84: 10400004  beqz v0, 0x1008c98     ; a1 empty -> v1=1
1008c88: 00c0a82d  move s5, a2            ; (delay)
1008c8c: 8c420008  lw   v0, 8(v0)         ; v0 = *(*(a1+4)+8)   <- the size at +8
1008c90: 14400002  bnez v0, 0x1008c9c     ; size != 0 -> v1 stays 0 (data present)
1008c98: 24030001  li   v1, 1             ; else v1 = 1
1008c9c: 50600066  beqzl v1, 0x1008e38    ; v1==0 -> data-present path
1008ca0: 8e620004  lw   v0, 4(s3)         ; (delay) else fall through -> ALLOC+ZERO+DECODE
```

**Measured, per call (`VULCAN4_W164_SRC`):**
```
[w164:src] BRANCH 0x1008c80: a1+4=0x18951f0 *(a1+4)+8=0x0 -> EMPTY (alloc+zero then decode)
[w164:src] BRANCH 0x1008c80: a1+4=0x1895220 *(a1+4)+8=0x1 -> data-present
```
So `sub_01008C50` takes the empty path **because its source node's `+8` (the size) reads 0 at the
test**, then allocates a fresh node, zeroes it (`0x1008ce0`), and hands it to `sub_010088E8`.

**The recompiler is faithful at the decision.** Generated code:
```cpp
// 0x100811c: lw $v1, 0x4($s7)
SET_GPR_S32(ctx, 3, READ32(ADD32(GPR_U32(ctx, 23), 4)));   // r23 = $s7  -- correct
// 0x1008134: lw $a0, 0x4($s2)
SET_GPR_S32(ctx, 4, READ32(ADD32(GPR_U32(ctx, 18), 4)));   // r18 = $s2  -- correct
```
`sub_01008080` routes `$s2`(its `$a1`) to `sp+52` (→ `sub_01008C50`'s `$a2` → the decoder stream) and
`$s7`(its `$a2`) to `sp+36` (→ `sub_01008C50`'s `$a1`, the source). That matches the MIPS. So the
guest itself is choosing to decode the `$s2`-side node.

## The actual defect: the size is set LATER than it is read

Watching the source node (`0x1895310` in the run above, field `+8` = `0x1895318`):

```
addr=0x1895318 val=0x0 writerPc=0x1008184
addr=0x1895318 val=0x0 writerPc=0x10072f4
addr=0x1895318 val=0x0 writerPc=0x10071fc
addr=0x1895318 val=0x1 writerPc=0x1008f78    <-- INSIDE sub_01008C50
addr=0x1895318 val=0x1 writerPc=0x1005710
```

The size is written `0` early and `1` **later — including by `0x1008f78`, which is inside
`sub_01008C50` itself, i.e. the data-present path the empty branch skipped.** So at the moment
`sub_01008C50` tests `*(a1+4)+8`, the node has not yet been sized; it is sized afterwards. The decode
of the empty node happens first and spins, so the sizing call never gets to run.

**This is a guest ordering dependency, not a codegen or runtime read/write bug** that this dish could
show. Be ready for the honest reading: either (a) the guest's own initialisation order is violated
because an earlier step our runtime performs (the `CORE.GT4` parse) did not populate the node in the
expected sequence, or (b) `sub_01005D48` is entering this clone before the size field is committed.

## LEFT: THE EXACT NEXT MEASUREMENT

1. Log the **order** of `0x1008f78` (size write inside `sub_01008C50`) vs the decoder entry
   `0x10088e8` in one run — confirm the decode truly precedes the sizing, or find the interleaving.
2. Watch `0x1895318` for the writer that SHOULD set it before the branch (candidate: the `$s7` clone
   at `0x100826c`, which copies `*(s1+0x14)` — check whether that copy also sets `+8`).
3. `sub_01005D48` calls `sub_01008080` twice, at `0x10061bc` (`a1=$s3,a2=$s4`) and `0x1006260`
   (`a1=$s4,a2=$s2`). Determine which call reaches `sub_01008C50` with the unsized node, and why the
   other one is sized.

## Gate / artifacts (honest)

The gate is v3 (perceptual compare to `.disclaimer-reference.png`). It **FAILS**, as it must — the
screen is still the disclaimer:
```
GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference — the screen did not change.
```
Fresh capture path: `/mnt/ssd/vulcan4-build/run/disclaimer-break-W172.png` (reported, not opened).
Suite: **497/497**. All new probes off by default (`VULCAN4_W162/W163/W164`).
