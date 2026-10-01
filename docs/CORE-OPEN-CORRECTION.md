# CORRECTION TO CORE-OPEN-DISASM — the offsets are NEGATIVE, and that changes the reading

I got the sign convention wrong in `CORE-OPEN-DISASM.md`. Everything below fixes it. **The
instruction stream and the section map are still right; the addresses I derived from the `lw`
offsets were not.** I am recording my own error rather than quietly replacing the file.

## The error

The `lw` instructions carry a **signed** 16-bit offset, and I read them as unsigned:

```
0x01000318: lw a3, 0xdc88(v0)   <- I wrote this.  Actual: lw a3, -0x2378(v0)
0x0100031c: lw a2, 0xdc80(v1)   <- Actual: lw a2, -0x2380(v1)
0x01000324: lw v0, 0xdc84(a1)   <- Actual: lw v0, -0x237c(a1)
0x01000358: lw v0, 0xdca0(s4)   <- Actual: lw v0, -0x2360(s4)
```

`0xdc88` as a signed 16-bit is **−0x2378**, not `+0xdc88`. With `lui v0, 0x0103` already in the
register, the real target is `0x01000000 + (−0x2378) = 0x00FFDC88` — **not** `0x0103DC88`. My whole
"`FileName` vtable" story, and the "word at `0x0102DCA0` is the retry governor" claim, came from
that mistake.

## What is actually at the address I thought was a vtable

`0x0103DC80` in `.rodata` decodes as nonsense instructions — because it is **ASCII, not code**:

```
0x0103dc80: 6d726f46   "Form"
0x0103dc84: 61547461   "atTa"
0x0103dc88: 74656772   "rget"
0x0103dc8c: 65736142   "Base"
0x0103dc90: 00000000
```

`0x0103DC80` = **"FormatTargetBase"**. A rodata string, nothing to do with `core.gt4`. My decoder was
being fed text.

## What survives, and what does not

**Survives — verified, and the important part:**

- The ELF is little-endian R5900, and the section table (`.text 0x01000000/0x1000/0x02DC10`,
  `.data 0x0102DC80/0x02EC80`, `.rodata 0x01036D80/0x037D80`, plus `.ctors`, `.dtors`, `.reginfo`,
  `.rdata`, `.eh_frame`, `.gcc_except_table`, `.sbss`, `.bss`) is correct.
- The `j`/`jal` region rule `0x01000000 | (imm << 2)` — 2,565 of 2,565 targets land in `.text`, versus
  0 of 2,492 for the textbook `PC & 0xF0000000` rule. That measurement stands.
- The function at **`0x010002C8`** is real: `addiu sp, sp, -96`, a 3-iteration loop calling
  `0x010002A8`, `addiu s0, zero, 3` / `addiu s2, zero, -1`.
- **`0x0100036C: jal 0x01005430`** and the **second call at `0x010003C0` with `a1 = 1`**, with
  `beq v0, zero, ...` guarding each, and `0x0100043C: bne`-style loop-back. So a function really is
  called twice with the second attempt flagged — **that shape is real**, whatever the arguments mean.
- `0x01005430` has a real prologue: `addiu sp, sp, -80`, then callees `0x010050B8`, `0x01003A50`,
  `0x01003E10`.
- `opcode 0x2B` / fn `0x2D` words are `break` trap slots, not mystery instructions.

**Does not survive:**

- The "`FileName` vtable at `0x0102DC80` holding `core.gt4` / `DISK` / `MCARD 0` / `MCARD 1` / `HOST`".
  Those five strings are real and adjacent in `.rodata` — `core.gt4` is at `0x0103D1D8`, then `DISK`
  at `+0x10`, `MCARD 0`, `MCARD 1`, `HOST` — but **I have not shown any pointer to them, and the
  `lw`s in question do not reference them.**
- "The loop is gated on a word at `0x0102DCA0`." The `lw` at `0x01000358` is `-0x2360(s4)`, not
  `0xDCA0`. I do not know what that address is.
- Any claim about which string is passed to the open.

## The honest state, and the real next step

I have **not** proven what `0x01005430` opens. I have proven a function is called twice, the second
time with `a1 = 1`, and that the caller branches on the result and can loop.

The next step is narrow and does not need more of my guessing: **`0x01005430` is 30 instructions of
ordinary code with a clean prologue. Decode it to its `jr ra`, following the three `jal`s at
`0x010050B8`, `0x01003A50`, `0x01003E10`, and find the one that reaches a syscall.** One of them is
the open. Until that is done, nobody should act on the string claims in either of my documents.

This is the third document I have written tonight containing a confident claim that did not survive
its own verification. The pattern is the instrument, not the game: I read addresses off a decoder I
had not tested against a known-good case. **Decode one instruction, check it against something
recognisable, then continue** — that is the rule this file exists to enforce.
