# VULCAN 4 — NEXT SESSION PROMPT (W229)

Point the new session at THIS file in plan mode. Everything needed to continue is below.

---

## THE PROMPT (paste this as the first message of the new session)

```
VULCAN 4 — continue from W228. Read docs/W228-RESULTS.md and docs/HANDOFF.md (W228 entry, top) first.

GOAL: get GT4 past the 2005 Sony disclaimer to the menu.
GATE: bash /home/or/vulcan4/.auto/verify-menu.sh (capture must be measurably != the disclaimer).

STATE: the wall is the parser/merge. The parser's input stream struct (0x01FFFD10 on hardware)
is EMPTY in ours and POPULATED on hardware, so our merge builds a 1-element stream and spins
forever. W228 proved the struct FILLER is correct in our emitted code:

  - the writer of the struct = FUN_01004308, called only from FUN_01004500 at 0x0100451c
  - it stores +0x14/+0x18/+0x1c/+0x20 from the buffer at s1 (0x012BF1xx)
  - our emitted sub_01004308_0x1004308 (line 18248 of
    /mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp) is byte-for-byte identical to
    hardware (its stores are at lines 18488 / 18503 / 18515 / 18533)
  => the divergence is UPSTREAM of FUN_01004308.

NEXT (one measurement, then branch):
1. Add a probe at our FUN_01004308 ENTRY (targetPc == 0x1004308) dumping a0/a1/a2/a3/s0/s1/s2/ra/sp
   and the struct contents. Compare to hardware:
     s1=0x01FFFD14  s2=0x01FFFD10  a3=0x80  s0=0x012BF100  v1=0x012BF182  ra=0x010043AC
2. If it NEVER fires -> ours never reaches the filler -> chase the control-flow derail
   (halt=pc_outside_generated_table, corrupted ra; campaign W148: writer-watch the saved-ra slot
   sp+88 in sub_0100F390).
   If it FIRES with different inputs -> walk the caller chain
   FUN_01004500 -> FUN_010047c0 -> FUN_01000558 and find where s1/v1/a3 diverge.
3. Fix in the runtime/recompiler ONLY (never runner/*.cpp, never PS2Recomp .h; upstream patches go
   in tools/patches/). Rebuild -j4 + nice -n 10 / ionice -c3. Re-run the harness, then
   verify-menu.sh. Write docs/W229-RESULTS.md and add a W229 entry to docs/HANDOFF.md.

ENVIRONMENT (already up):
- PCSX2 running via systemd --user unit pcsx2-gt4; DebugServer 127.0.0.1:21512 (Pine 28011 off).
  MCP tools: pcsx2_pcsx2_* . Watchpoints/pause/registers all work.
  NOTE: watchpoint hits report last_PC=0 -- read the PAUSED PC instead.
- Ghidra via GhydraMCP port 8192, ELF SCUS_973.28 loaded (870 funcs). MCP: ghydra_*.
- Harness build: bash /home/or/vulcan4/tools/harness/build_harness.sh
  Harness run:  cd /mnt/ssd/vulcan4-build/run && VULCAN4_W227_MERGE=1 DISPLAY=:0 \
                nice -n 10 ionice -c3 timeout 70 ./vulcan4_harness \
                /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml 300000 50
- ISO: /mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso ; ELF: /mnt/ssd/gt4/work/SCUS_973.28
- PCSX2 BIOS: ~/.config/PCSX2/bios/ps2-0200a-20040614.bin
- Emitted code: /mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp

CONSTRAINTS: no BIOS in the product path; no game data in the repo; commits author as
"Or Golan <or024662@gmail.com>", do NOT push; keep the suite green; runs are nondeterministic
(derail vs spin); cap builds at -j4 (a live Minecraft server runs on this box).
```

---

## BACKING DOCS (read if the prompt is not enough)

- `docs/W228-RESULTS.md` — this dish's full write-up (the write-watch, the writer, the exoneration).
- `docs/HANDOFF.md` — W228 entry at top; W227 entry below it (the A/B ground truth).
- `docs/W227-RESULTS.md` — how PCSX2 A/B was stood up; the merge-spin side-by-side.
- `docs/LIMITATIONS.md` — known GS defects.
- `docs/GOALS.md` / `docs/CAMPAIGN.md` — the stage ladder and gate definitions.
