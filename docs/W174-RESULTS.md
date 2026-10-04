# W174 — the sizing premise is REFUTED: the empty branch never decodes; the spin is a zero DATA word

**Dish:** `03-the-sizing-happens-before-the-read`. **Result:** ❌ no picture change, but the premise
is **refuted** with two independent measurements, and the real defect is narrowed from "a missing
size" to "an allocated, sized, zero-filled data word".

## Retraction of my own W172 claim

W172 said: *"the empty branch allocates a fresh node, zeroes it (`0x1008ce0`), and hands it to
`sub_010088E8`, which spins."* **The empty branch never calls the decoder.** Disassembly of the
empty path (`0x1008ca4`-`0x1008e34`): `grep -c "jal.*10088e8"` over it is **0**, and it exits
`1008e2c: b 0x1009008` — the epilogue (`1009008: ld s0,16(sp) ... jr ra`). The decoder at `0x1008ffc`
is reachable **only** from the data-present path (`0x1008e38+`). So making the size non-zero earlier
cannot "stop the empty-branch spin" — that path does not spin. **Retracted.**

## Measurement 3 — which `sub_01008080` call, and the node sizes

New probe (`VULCAN4_W175_8080`, off by default) at `sub_01008080` entry, and the existing
`VULCAN4_W162_DECODE` at the decoder. One nondeterministic run, both reached:

```
[w175:8080] n=1 sourcePc=0x10061bc a1=0x1fffd80 ->node=0x1895270 sz=0x1 dat=0x18952a0[1,0]
                                  a2=0x1fffe30 ->node=0x1895190 sz=0x0 dat=0x0[0,0]
[w162:dec]  a0=0x18953e0 a1=0x1895340 a2=0x1895390 a3=0x1895310
            arg0[sz=0x1 dp=0x1895410 w0=0x0]
            arg1[sz=0x1 dp=0x1895370 w0=0x1]   <- populated
            arg2[sz=0x1 dp=0x18953c0 w0=0x0]   <- the rotate/compare WORK buffer, ZERO
            arg3[sz=0x1 dp=0x1895430 w0=0x1]   <- populated
```

**Every decoder arg has size `+8 = 1`.** The premise "the source node's size reads 0 at the test" is
true only of the branch that *returns without decoding*. The decode that actually spins receives an
`$a2` whose size is 1 and whose **data word is 0**.

## The recompiler, re-verified this pass

`SET_GPR_S32(ctx,3,READ32(ADD32(GPR_U32(ctx,23),4)))` for `$s7` at `0x100811c`, `$s2`→r18 at
`0x1008134`, and the call setup (`s3=sp+64,s1=sp+32,s0=sp+48,s5=sp+80`) at `0x10082c0` are all
faithful. No codegen defect.

## Measurement 2 — does the `$s7` clone set `+8`? YES

`1008230: lw a1,8(s1)` / `1008238: sw a1,8(s0)` — the clone **copies `+8`** from its source. So the
clone cannot be blamed; if the source's `+8`/data is 0, the clone is 0. Confirmed by the store watch:
`0x1895348 val=0x1 writerPc=0x1008238`.

## The decision (this dish must force one)

Neither (a) nor (b) in the brief's binary holds as stated, because the premise is wrong. The honest
reading, forced by the two measurements:

> **The spin is `func_1005870(work=0, target=1)` inside the data-present path. The work buffer
> (`$a2`, node `0x1895390`, data `0x18953c0`) is allocated, sized `1`, and zero-filled, but its DATA
> IS NEVER WRITTEN from the source stream.** The empty-branch size is a red herring: that path
> returns. This is a data-population defect in `sub_01008C50`'s data-present path (its clone blocks
> at `0x1008e7c`/`0x1008f50`/`0x1008f5c` copy from a source whose data is itself empty), i.e. **the
> guest is handed the empty one of a pair** — but at the data level, not the size level.

## Gate (authoritative, honest)

`bash .auto/verify-menu.sh` → **GATE FAIL** (capture byte-identical to the disclaimer reference).
Suite **497/497**. All probes off by default. Capture path reported, not opened.

## Next wall, in my own words

Key the store watch on the **data pointer captured at the decoder** (`arg2 dp`), not on fixed
addresses (heap reuse made W172's fixed-address watch ambiguous). Watch `dp[0]` (`0x18953c0`):
find **whether anything ever writes it** and, if so, who — because today the decoder's
`arg2` data word is born zero and stays zero, while `arg1`/`arg3` beside it are non-zero.
The pair `arg1` (filled) / `arg2` (empty) under the same `sub_01008C50` call is the thing to explain.
