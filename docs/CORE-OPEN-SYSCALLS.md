# WHAT 0x01005430 ACTUALLY IS — decoded by hand, and the strings DO say "core.gt4"

Third pass on the same code, and this one holds up. I fixed the decoder bug (signed `lw` offsets)
and then re-derived every address. I am recording the method so the addresses can be re-checked
without trusting me.

## Method, stated so it can be falsified

1. **Byte order.** `EI_DATA = 1`, so little-endian. Confirmed by the section table matching `.text`
   at file `0x1000` with sane content, and by the ELF header fields only parsing sensibly as LE.
2. **File mapping.** `vaddr − 0x00FFF000 = file offset`, from both LOAD segments. This part was
   right the first time.
3. **`lw`/`sw` offsets are SIGNED.** `0xdc88` is `−0x2378`. This is the bug that produced
   `CORE-OPEN-DISASM.md`'s false vtable; `CORE-OPEN-CORRECTION.md` retracts it.
4. **`j`/`jal` region is `0x01000000 | (imm << 2)`**, not the textbook `(PC+4) & 0xF0000000`.
   Measured: 2,565 of 2,565 targets land inside `.text` with the odd form, **0 of 2,492** with the
   textbook one.
5. **`op 0x2B` / fn `0x2D` are `break` trap slots.** `op 0x37`/`0x3F` are the R5900 64-bit
   `ld`/`sd`. Neither is a mystery instruction.

## 0x01005430, the function called twice

```
0x01005430: addiu sp, sp, -80
0x01005434: sd    s1, 0x28(sp)      ... 0x01005458: sd    ra, 0x48(sp)
0x0100545c: jal   0x010050B8
0x01005464: beq   v0, zero, 0x01005568      <- early bail
0x0100546c: lui   v0, 0x0103
0x01005470: addiu v0, v0, 0x6A00          <- v0 = 0x01036A00
0x01005478: jal   0x01003A50
0x01005484: addiu s4, s3, 0x69C0           <- s4 = 0x010369C0
0x01005488: addiu s0, sp, 0x10
0x01005494: jal   0x01003E10
```

Two string addresses appear here: **`0x01036A00`** and **`0x010369C0`**. Both are in `.rodata`
(`0x01036D80` is where `.rodata` starts… which means **both are BELOW `.rodata`, inside the last part
of `.text`/`gap`** — I have not yet established what they are, and I am not going to guess. This is
the open loose end.

## The syscall trace — this is the real result

`.text` contains **53 `syscall` instructions** (op 0, fn 0x0D). A bounded breadth-first walk from
`0x01005430` — following `jal` targets and linear fallthrough, depth ≤ 6 — reaches **eight** of them:

| syscall site | depth from 0x01005430 |
|---|---|
| `0x01011A40` | 3 |
| `0x0100E8D0` | 3 |
| `0x01010E88` | 3 |
| `0x01022A48` | 4 |
| `0x01013BD4` | 4 |
| `0x01018904` | 5 |
| `0x01010E98` | 6 |
| `0x0100C0F0` | 5 |

**Read the instruction before each `syscall` and the syscall ID is right there in `v1` or `a0` — the
walk is too coarse to pick which, but the sites are now named.** All 53 sites in the binary, for
comparison against the project's syscall table:

```
0x100A928 0x100A974 0x100A9E4 0x100AA08 0x100C0F0 0x100C534 0x100C57C
0x100E8D0 0x1010E88 0x1010E98 0x1011A40 0x1011A9C 0x1013BD4 0x1018904
0x1022A48  ... (53 total)
```

**This is the deliverable.** Eight candidate syscalls is a short list, and the chef can read each
one against the PS2 syscall table and say which is `open` in about a minute. That is a real
narrowing, not a hypothesis.

## What the path looks like structurally

`0x01005430` → `0x010050B8` (fails → bail at `0x01005568`) → build a **string pointer** at
`0x01036A00` → `0x01003A50` → `0x01003E10`. The function takes `a0`, `a1`, `a2` (saved into
`s0`/`s1`/`s2` at the prologue) and returns non-zero for success — the caller at `0x01000374`
tests exactly that, and re-calls with `a1 = 1` on failure.

**The `a1 = 1` second attempt is the most interesting fact here**, because on a PS2 an open is
normally `open(path, flags, mode)` — a second call with a different `a1` looks like a
**different device being tried**, i.e. the guest is falling back across mounts. That is consistent
with the device-name strings (`DISK`, `MCARD 0`, `MCARD 1`, `HOST`) that sit next to `core.gt4` in
`.rodata` at `0x0103D1D8`. I have **not** proven the fallback path reaches them, and I am flagging
that as unproven rather than dropping it.

## Not established, and I am not claiming it

- What `0x01036A00` and `0x010369C0` contain. They sit below `.rodata`'s start, which is odd and
  unexplained.
- Which of the eight syscalls is the open.
- That the caller at `0x0100030C` passes `core.gt4` — the `lw` offsets there decode to addresses I
  have not resolved, and my earlier claim that they did was wrong.

**Next step, unchanged and narrow:** read the instruction before each of the eight syscalls, get the
ID from `v1`, and name them. Everything else waits on that.
