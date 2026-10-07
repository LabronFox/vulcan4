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

## Container map (W231c) — MEASURED, not guessed

`FUN_0101E81C` is a plain `memcpy(dest=param1, src=param2, len=param3)` (Ghidra decompile).
`FUN_010047C0` walks the decoded blob as a member table:

    u32 count = LE32(blob+0)        = 3
    u32 entry = LE32(blob+4)        = 0x00100008
    p = blob
    repeat count times:
        dest = LE32(p+8);  size = LE32(p+0xc);  src = p+0x10
        memcpy(dest, src, size)
        p = p + 8 + size

Emulating that over `/mnt/ssd/vulcan4-build/w229-oracle.bin` (6,118,856 bytes) yields:

| # | dest | src (blob off) | size | blob data range |
|---|------|----------------|------|-----------------|
| 0 | 0x006179FC | 0x10 | 0x18 | 0x10..0x28 |
| 1 | **0x00100000** | **0x30** | **0x517A14** | **0x30..0x517A44** |
| 2 | 0x00617A80 | 0x517A4C | 0xBE37C | 0x517A4C..0x5D5DC8 |

`0x100000 + 0x517A14 = 0x617A14`, matching the header end field `0x006179FC` to within 0x18 (member 0
is a 24-byte footer written at `0x6179FC..0x617A14`, i.e. the image's very end). Member 2 loads a
second region at `0x617A80` (after the image). So the whole 6,118,856-byte blob is consumed exactly
(final `p = 0x5D5DC0`, blob length `0x5D5DC8`).

## Hardware verification — GATE PASSED, 100.0000%

PCSX2 (same disc) via the DebugServer socket (`127.0.0.1:21512`, `{"cmd":"read_memory",...}`), with the
guest paused **after** the loader had run. Dumped RDRAM `0x00100000` length `0x517A14` in 64 KB chunks
(5,339,668 bytes) and compared byte-for-byte with `blob[0x30:0x30+0x517A14]`:

```
match 100.0000%   num diff bytes: 0   IDENTICAL
```

So **we hold the exact engine image**: `blob[0x30:0x517A44]` → `0x100000`, entry `0x100008`.

## The image is CODE (measured on hardware)

Disassembly at the entry and beyond (DebugServer, hardware memory):
```
0x00100008  padduw at,zero,zero      ; clear ALL GPRs (at..t8 ...), 16+ instructions
0x00100080  mthi1 zero / mtlo zero / mtlo1 zero / mtsah / mtc1 f00..f31
0x00100200  nop; lui v0,0x006D; addiu v0,0x6380; lw a0,(v0); jal 0x00107F08;
            addiu a1,v0,4; j 0x005A3140; dmove a0,v0
```
i.e. a crt0 that zeroes registers/MDU/FPU then jumps into the engine (`j 0x005A3140`, and hardware's
paused PC was `0x005A47B0` inside that region). The SCUS ELF `.text` at `0x01000000` is meanwhile all
zeros — confirming the ELF is a loader and the engine runs from low RDRAM.

## Step 3 (next, bounded) — the engine as a second recompilation target

The gate is green, so the engine image qualifies as a second target: **base `0x00100000`, entry
`0x00100008`**, disjoint from the loader ELF (`0x000100000..0x0617A14` vs `0x01000000..0x0102DC54`),
so one function table can hold both. Work required (not done in this review): wrap
`blob[0x30:0x517A44]` in a synthetic ELF (PT_LOAD vaddr 0x100000, `e_entry` 0x100008) on the SSD,
recompile it with `ps2xRecomp` (symbol-less → scanner/discovery path), emit its functions into the
same generated unit, and make the ExecPS2 driver relaunch dispatch into the recompiled engine instead
of raising `execps2_unmapped_entry`. Until that lands, the **loud `VULCAN 4 LIMITATION` stays** —
we hold the image but do not yet execute it. No code was changed in this review.
