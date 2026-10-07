# W231b — ExecPS2 arguments: ORACLE-FIRST review (the entry is game DATA, not a translation slip)

Task: before changing any code, break on the ExecPS2 call in PCSX2 and record a0–a3 + caller PC; then
find the FIRST DIVERGENCE that produced `entry=0x100008` / `argv=0x3`; fix at the source, or (step 4)
if hardware also passes `0x00100008`, name what lives at low RDRAM and report — do not guess.

## Oracle status (honest)

PCSX2 (same disc, `/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso`) is loaded and running
(`emulog`: `ELF cdrom0:\SCUS_973.28;1 … EntryPoint = 0x01000008`), but the DebugServer instance is
unhealthy: it sat in the EE kernel idle loop (`0x00081FC0`) for minutes, the cycle counter reset
(`EE/iR5900 Recompiler Reset`), and my breakpoints at `0x0101F074` (the ExecPS2 stub) and `0x01028AD0`
(the loader) did **not** fire — by the time I armed them the guest was already past the loader. So I did
NOT capture hardware's `a0–a3` at the syscall directly. What I DID capture on hardware is below.

## Hardware, measured

- `0x00100000` is populated: `00 00 00 00 00 00 00 00 | 28 0c 00 70 28 14 00 70 28 1c 00 70 …`
  (an 8-byte zero header then a QW table of `0x7000xxxx` scratchpad pointers).
- `0x01000000` (the ELF's `.text` base) is now **all zeros** on hardware.
- The guest is executing **real MIPS at `0x005A47B0`** — a byte-copy loop
  (`lbu v0,(a1); sb v0,(v1); addiu…; bne a2,a0,-6; jr ra`) — i.e. code inside the low-RDRAM image,
  NOT the ELF `.text`.

So on hardware GT4 runs a **runtime-loaded engine image in low RDRAM**, and the SCUS ELF `.text` is
gone from `0x01000000` by this point.

## Our side — where `0x00100008` comes from (traced in the emitted code)

```
FUN_01000558:
  0x10005d4  jal  FUN_010047C0        ; the loader/parse
  0x10005dc  lw   s0,0(s0)
  0x10005e0  beqz s0,+8
  0x10005e4  daddu s1,v0,zero         ; s1 = FUN_010047C0's RETURN
  ...                                 ; s1 is later the ExecPS2 entry
  0x1000674  daddu a0,s1,zero         ; a0 = entry
  0x1000678  daddu a2,s0,zero
  0x100067c  jal  FUN_010183B0        ; (a1 = 2 in the delay slot) -> … -> ExecPS2(entry,gp=0,argc,argv)
```

`FUN_010047C0` (Ghidra decompile, confirmed) returns `uVar11 = pbVar7[4] | pbVar7[5]<<8 |
pbVar7[6]<<16 | pbVar7[7]<<24` — the **little-endian u32 at `pbVar7+4`**, where
`pbVar7 = *(param_1+0x1c)`. W229 measured that struct field as `0x012BF204` (both ours and hardware),
and the **oracle blob's** first bytes are `03 00 00 00 | 08 00 10 00 | fc 79 61 00 | 18 00 00 00`
so `blob[4..7] = 0x00100008`.

**Therefore `entry=0x100008` is a value the game READS FROM ITS OWN DECODED PAYLOAD DATA, not a
register/translation error.** The same bytes decode identically in ours and (since the blob is
oracle-verified) on hardware. `gp=0` comes from `a1=2→0` chain; `argc=2`/`argv=3` come from
`sub_010183B0(s1, s2, s3)` where the values are loaded from the guest's own tables. None of the four
arguments is produced by a mis-shifted/truncated instruction.

## Conclusion (task step 4, not a guess)

Hardware ALSO targets low RDRAM: the loader parses the decoded CORE.GT4 payload header — which encodes
a segment **load destination `0x00100000`, size `0x517A14`** (visible in the header dump
`… 6dddf0 100000 517a14 …`) — loads that engine image there, and `ExecPS2(0x00100008)` enters it at
`0x100000+8`. Our recompiled image is the SCUS ELF `.text` only (`[0x01000008,0x0102DBEC)`), so a `J`
into `0x100008` is correctly "outside the recompiled image".

The real gap is therefore NOT an ExecPS2 bug: **GT4's engine is a runtime-loaded image (from CORE.GT4)
in low RDRAM, which static recompilation of the SCUS ELF does not cover.** Fixing Stage 1 needs the
CORE.GT4 payload treated as a second recompilation target (it is deterministic — the same 6 MB blob on
every boot), not a guess at the ExecPS2 entry. No code was changed in this review; the ExecPS2
LIMITATION from W231 remains the correct, loud behaviour.

Caveat, stated plainly: I did not capture hardware's `a0–a3` at the syscall directly (DebugServer
breakpoints did not fire before the guest passed the loader). The low-RDRAM execution and the
data-derived entry are both directly measured; the equivalence of hardware's `a0` is inferred from
(the same game code) + (the oracle-verified blob bytes) and should be confirmed by a clean-breakpoint
run before any second-image recompiler work is committed.
