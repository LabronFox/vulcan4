# W232 — the engine image as a recompilation target: reconciliation + scope

Goal (step 3): make the runtime-loaded engine image (`0x00100000`, entry `0x00100008`) a recompilation
target and wire ExecPS2 into it. Input file: `/mnt/ssd/vulcan4-build/w231-engine.bin`
(5,339,668 bytes, sha256 `5a9a9107b146b7d533a2a2421cdf900a913d5ced4e97892798cfa810b3dd2d34`).

## 1. DISCREPANCY RECONCILED — the entry IS code, not a table

Caine flagged image+8 = `0x70000C28`, image+0x0C..+0x5C `0x70001428, 0x70001C28, 0x70002428 …`
(0x800 apart) as "a table of GS/register pointers". Measured against hardware (PCSX2 disassembler,
full range `0x00100008..0x00100220`) it is a **coherent crt0**, not a table:

```
0x00100008..0x00100078  padduw rd,zero,zero   for rd = 1..31   (clear ALL GPRs)
0x0010007c..0x0010008c  mthi zero; mthi1 zero; mtlo zero; mtlo1 zero; mtsah zero,0
0x00100090..0x0010010c  mtc1 zero,f00 .. mtc1 zero,f31         (clear FPRs)
0x00100110..0x00100118  adda.s f00,f01; SYNC; ctc1 zero,fcr31
0x0010011c..0x00100194  BSS clear loop 0x006D5E00 .. 0x008A215C (sb zero / sq zero, 16-byte steps)
0x00100198..0x001001e4  li v1,0x3C; syscall  → dmove sp,v0 ; li v1,0x3D; syscall   (thread setup)
0x001001e8..0x00100200  jal 0x005B7560 ; jal 0x005ADF20 ; ei ; jal 0x0048EF90
0x00100204..0x0010021c  lw a0,(0x006D6380); jal 0x00107F08; j 0x005A3140 (dmove a0,v0)
```

The `0x800` stride is exactly `rd<<11` — the destination-register field of `padduw rd,zero,zero` —
NOT a table stride. `0x70000C28` = MMI opcode `0x1C`, funct `0x28` (PADDUW), rd=1 (at). The bytes flow
straight into the MDU/FPR clears and then `j 0x005A3140` (the engine main; hardware's paused PC was
`0x005A47B0` inside it). **Verdict: the entry address 0x00100008 is the real code entry. Caine's
"table" reading is refuted by the full disassembly.** The first `jal 0x00107F08` (`0x0C041FC2`) at
offset 0x210 is inside this same crt0.

## 2. TOOL INPUT — a synthesized minimal ELF (no recompiler patch needed)

Chosen path: feed the recompiler a synthesized ELF rather than patching the analyzer (the task allows
either). Built by script (not hand-written output):

- ELF32, LSB, `e_machine=EM_MIPS(8)`, `e_flags=0x20924001` (copied from `SCUS_973.28`),
  `e_type=ET_EXEC`, `e_entry=0x00100008`.
- one `PT_LOAD`: `p_vaddr=p_paddr=0x00100000`, `p_filesz=p_memsz=0x517A14`, flags RWX, align 0x10.
- sections: `.text` (PROGBITS, ALLOC|EXECINSTR, vaddr 0x100000, size 0x517A14) + `.shstrtab`.
- file: `/mnt/ssd/vulcan4-build/w231-engine.elf`; toml: `/mnt/ssd/gt4/work/w231-engine_recomp.toml`
  (`input`=the ELF, `output`=`/mnt/ssd/vulcan4-build/recomp_engine/`, `single_file_output=true`).

`readelf -h` validates it. The recompiler accepted it with **0 symbols** and used its scan/discovery
path.

## 3. SCOPE MEASURED — this is the blocker

```
[recompiler] extracted 18903 functions, 0 symbols, 3 sections
[recompiler] recompiling 18903 functions
[recompiler] collected 527553 resumable entry point(s) across 14923 owner function(s)
[recompiler] generating function output with 11 worker(s)
```

The whole-image scan yields **18,903 functions / 527,553 resume points** (the loader was 723 / 9,042 —
58× more resume points). Codegen produced **199–245 MB of `ps2_recompiled_functions.cpp` and did not
finish within a 40-minute run** (two attempts, `timeout 600` then `timeout 2400`, both exit 124). A
~250 MB translation unit at `-O2` is a multi-hour compile on this box (shared with a Minecraft server,
`-j4` cap), so the engine cannot be compiled + linked + booted within this dish.

## 4. STATE

- The engine image IS now a recompilation target the tool accepts (base `0x100000`, entry `0x100008`,
  via `w231-engine.elf`) — step 1 done.
- Full emission does not complete in a session — step 2 not finished.
- ExecPS2 wiring NOT done — step 3 pending. The LOUD `VULCAN 4 LIMITATION` from W231 stays
  (`halt=execps2_unmapped_entry`); nothing is faked, no generated output was hand-edited, the loader's
  723 functions are untouched (separate output dir), suite green.

## 5. NEXT (bounded)

Emit only the entry-reachable subset instead of whole-image scanning (add a tool option to suppress
`ScanFunctionStartsFallback`/the interior-resume sweep, seeded from `0x100008`), which should cut the
engine to the hundreds of functions its crt0/init/main actually reach; then compile, link, and make the
ExecPS2 relaunch dispatch into the recompiled range `[0x100000, 0x617A14)`.
