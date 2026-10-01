# Caine's own disassembly of the core.gt4 open path — LE decoding, and it CHANGES the story

I decoded the ELF myself rather than trusting the brief I was handed. **The brief's instruction
addresses are wrong** (they were produced by a big-endian decoder), but the shape of its conclusion —
the guest opens the file and retries — survives, and one detail is new and important.

## First, a correction that invalidates part of the brief

`SCUS_973.28` is an **ELF32 LITTLE-ENDIAN MIPS R5900** binary. `EI_DATA = 1`. The section table proves
it and gives the real map:

| section | vaddr | file offset | size |
|---|---|---|---|
| `.text` | `0x01000000` | `0x1000` | `0x02DC10` |
| `.ctors` | `0x0102DC10` | `0x02EC10` | `0x18` |
| `.dtors` | `0x0102DC28` | `0x02EC28` | `0x14` |
| `.reginfo` | `0x0102DC3C` | `0x02EC3C` | `0x18` |
| `.data` | `0x0102DC80` | `0x02EC80` | `0x90E8` |
| `.rodata` | `0x01036D80` | `0x037D80` | `0x9478` |
| `.rdata` | `0x01040200` | `0x041200` | `0xA80` |

So `vaddr − 0x00FFF000 = file offset` holds, as the brief said — but that is the only part of the
brief that survives. **Every instruction address it quoted was computed from a big-endian read of
little-endian bytes**, which is why decoding `0x010057A0` that way produced pure noise
(`bltz t8`, `j 0x000002b8`).

**Confirmed by measurement, not assumption:** scanning `.text` for `j`/`jal` and resolving the
target as `0x01000000 | (imm << 2)` puts **2,565 of 2,565** inside `.text`. The textbook MIPS region
rule (mask the PC with `0xF0000000`) puts **0 of 2,492** inside. That is the decisive test, and the
non-obvious form is the correct one for this image.

## The real open path, at the real addresses

The `FileName` vtable at `0x0102DC80` is confirmed: slot 0 = `"core.gt4"` (`0x0103D1D8`, in
`.rodata`), then `DISK`, `MCARD 0`, `MCARD 1`, `HOST`.

The consumer is the function whose prologue is at **`0x010002C8`**:

```
0x010002c8:  addiu sp, sp, -96            <- real prologue
0x010002dc:  addiu s0, zero, 3            <- 3-iteration loop
0x010002ec:  addiu s2, zero, -1
0x010002f8:  addiu s1, s1, 12
0x010002fc:  jal  0x010002a8
0x01000300:  addiu s0, s0, -1             <- delay slot
0x01000304:  bne  s0, s2, 0x010002f8      <- loop
```

Then, at **`0x0100030C`** — and here the strings are loaded *properly*, which the brief missed:

```
0x0100030c:  lui  v1, 0x0103
0x01000310:  lui  v0, 0x0103
0x01000314:  lui  a1, 0x0103
0x01000318:  lw   a3, 0xdc88(v0)          <- "MCARD 0"
0x0100031c:  lw   a2, 0xdc80(v1)          <- "core.gt4"
0x01000324:  lw   v0, 0xdc84(a1)          <- "DISK"
0x01000338:  jal  0x01015130              <- constructor
0x0100033c:  sd   s7, 0x10(sp)            <- delay slot
```

**`0x0100036C: jal 0x01005430`** is the real open call, and it is called **twice**:

```
0x0100036c:  jal  0x01005430
0x01000370:  addiu s0, sp, 12             <- delay slot: descriptor stored at sp+12
0x01000374:  beq  v0, zero, 0x010003b8    <- FAILED -> second attempt
0x010003b8:  addiu a1, zero, 1           <- a1 = 1 : the "which device" argument
0x010003c0:  jal  0x01005430              <- SECOND attempt
0x010003c8:  beq  v0, zero, 0x01000404    <- failed again -> give up this round
0x01000404:  jal  0x01004fb8
0x0100043c:  beq  s1, zero, 0x01000358    <- LOOP BACK to 0x01000358
```

And at `0x01000358`, the gate that decides whether the loop runs at all:

```
0x01000358:  lw   v0, 0xdca0(s4)
0x0100035c:  bne  v0, zero, 0x01000404    <- non-zero -> EXIT the loop
```

## What this actually means — and it changes the target

So the guest **does** open `core.gt4`, and on failure it retries with `a1 = 1`, and if that also
fails it loops. **But the loop is gated on a word at `0x0102DCA0` being zero.** That is the thing worth
naming next: it is very likely a "have we already given up" or "retry budget" flag, and its value
decides whether this is *two* attempts or *two hundred thousand*.

**The corrected next step is therefore not `0x010057A0` / `0x010057F4` — those were the brief's
misdecoded addresses.** It is:

1. **Disassemble `0x01005430`**, the function actually called twice. It will name the kernel call.
   Its prologue is presumably a normal `addiu sp, sp, -N`; a quick scan for the prologue lands it.
2. **Read the word at `0x0102DCA0`** and find who writes it. That is the retry governor.

I have not done either yet, and I am not going to claim them. What is verified above is the
instruction stream, the addresses, and the ELF map; the decoder that produced it is at
`/tmp/mipsle.py` with the corrected `j`/`jal` region rule.

Also worth recording so nobody repeats it: `opcode 0x2B` words like `0x0000202D` scattered through
these functions are **`break` with an encoded code** (fn `0x2D`), i.e. compiler trap slots, not
mystery instructions. They appear at `0x01000320`, `0x01000350`, `0x01000368`, `0x010003BC`,
`0x01000444`.
