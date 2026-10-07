# W251 — authoritative engine entry points (Ghidra ground truth) — IN PROGRESS

Dish: replace the recompiler's *guess* of the engine's function entry points with ground truth —
the 19,403 addresses in `/mnt/ssd/vulcan4-build/engine-symbols.csv` (Ghidra's export of
`w231-engine.bin`). Fix the TOOL (TOML / analyzer input under `tools/patches/`); never hand-edit
generated output. Keep the loader byte-for-byte; every probe OFF by default.

## Measured baseline (before any change), 2026-10-07

- Current emitted engine set (`recomp_engine_small/register_functions.cpp`, W249 state):
  **758 distinct generated functions**; of those, **547 start at a Ghidra address** and 211 do not.
  Ghidra addresses **not** emitted: **18,856**. Coverage = 547 / 19,403 = **2.82 %** — the dish's
  "2.8 %" number, confirmed by measurement.
- Source of the guess: `ElfParser::extractFunctions()` whole-image prologue scan →
  `[recompiler] extracted 18903 functions` (W233 emit.log), then reachable-only keeps 6484, then the
  frontier cap yields the 758 actually emitted. Ghidra found 19,403.
- Engine image: single `.text` `PROGBITS` at vaddr `0x00100000`, size `0x517A14` (file offset
  `0x1000`), ELF entry `0x00100008`. One `LOAD` segment, RWE, filesz == memsz == `0x517A14`.
- Ghidra CSV shape: `address,name,size,thunk,external`, 19,403 data rows, all `thunk=false
  external=false`, addresses `0x1008D0 .. 0x616EE8`. **`size` is not usable as a boundary**:
  17,815 of 19,403 rows have `size <= 8`. Boundaries must come from the *next* address (a partition).
- The existing `ghidra_output` TOML field loads a **`name,start,end,size`** CSV and *skips*
  auto-generated (`FUN_*`) names — so it neither accepts this CSV's shape nor honours `FUN_` rows.
  Feeding the CSV therefore requires a tool change either way.

## Decision

Add a new, purpose-built, env-gated TOOL input: `PS2RECOMP_ENTRY_ADDR_CSV=<path>` — a list of
authoritative function *starts* (address-first CSV). The recompiler replaces its prologue-guessed
`m_functions` with the exact partition of those addresses (`end = next start`, last = section end).
OFF by default → the loader path is byte-for-byte unchanged.

Implementation: `tools/PS2Recomp/ps2xRecomp/src/lib/ps2_recompiler.cpp`, patch
`tools/patches/ps2recomp-linux-w251-authoritative-entries.patch` (+229 lines, **purely additive** —
no existing line is touched, which is what makes the OFF-by-default claim checkable). Three pieces:

1. `w251EntryAddrCsv()` — the single definition of "the probe is on": **unset OR empty** → nullptr → off.
2. In `initialize()`: parse the CSV's first field, sort+unique, drop out-of-`.text` addresses with a
   LOUD line each, add the ELF entry as an extra boundary, and build a **partition** —
   `end = next start`, last = section end. `m_functions` is *replaced* with that partition; the
   prologue guess is not merely filtered, it is discarded.
3. In `recompile()`: after the pass, walk `m_functions` and emit a LOUD `VULCAN 4 LIMITATION` per
   function that is neither recompiled nor stub nor skipped, then print the coverage line. The
   recompiler is not allowed to *report* success it did not achieve.

## Emit result (measured, `w251-emit.log`)

`/mnt/ssd/vulcan4-build/recomp_engine_w251/` — 6.2 s wall, 46 part files + `register_functions.cpp`,
402 MB. Recompiler's own lines:

```
[recompiler] extracted 18903 functions                      <- the guess, now discarded
[recompiler] W251 authoritative entry points: replaced 18903 guessed function(s) with 19404 from
             '/mnt/ssd/vulcan4-build/engine-symbols.csv' (requested 19403, outside section 0,
             elf-entry-boundary added)
[recompiler] collected 507576 resumable entry point(s) across 18846 owner function(s)
[recompiler] W251 coverage: 19404 function(s) in the emitted set, 0 undecoded
```

One LOUD line, and it is the one honest gap — the ELF entry `0x00100008` is not a Ghidra start:

```
VULCAN 4 LIMITATION: the ELF entry point 0x100008 is not one of the authoritative CSV starts — it
was added as an extra function boundary so the guest's first instruction remains reachable
```

`outside section 0` = no CSV address fell outside `.text`. Emitted set = **19,404**; **zero** `entry_`
slices (the 758-function frontier cap and the standalone-`entry_` synthesis are both gone — a dense
partition *covers* every in-image target, so interior entries became
`case 0x5b73c8u: goto label_5b73c8;` arms inside their owning function).

## Independent check (does NOT read the recompiler's own report)

`tools/check_engine_symbols.py <csv> <emit-dir>` reads the two artifacts — the CSV, and the emitted
`ps2_recompiled_functions.h` + `register_functions.cpp` — and diffs both directions:

```
W251 CHECK: csv=19403 emitted=19404 matched=19403
W251 CHECK: emitted function 0x00100008 is not a CSV start (boundary the emit added)
W251 CHECK: PASSED -- every authoritative entry point is an emitted function
```

**Coverage 19,403 / 19,403 = 100 %**, up from 547 / 19,403 = 2.82 %. The single extra address is the
ELF entry, already named LOUD above. Exit 0.

Gotcha worth recording: the naming is **`sub_%08x_0x%x`** — the prefix is zero-padded to 8, the suffix
after `0x` is *not* (`sub_00100008_0x100008`). A regex expecting 8 hex digits after `0x` matches
**zero** declarations and reports a clean-looking "0 emitted". Assumed width, wrong answer.


## Compile, relink, boot (measured)

- Compile: `tools/harness/build_engine.sh` with `VULCAN4_ENGINE_DIR=$B/recomp_engine_w251`,
  `-O0 -j4`, `nice -n 10 ionice -c3`, `TMPDIR=/mnt/ssd/tmp` — **47 units** (46 parts +
  `register_functions.cpp`), **449 MB** of objects, exit 0. Every `.cc.log` holds only the same
  pre-existing `-Wformat` warning in `ps2_runtime_macros.h:1132` (`vulcan4W139XorPrint`), unrelated
  to W251.
- Relink: `VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash tools/harness/build_harness.sh`
  → `$B/run/vulcan4_harness`, 347,669,656 bytes, mtime 2026-10-07 20:16. The linked binary carries
  ~19,542 distinct `sub_*` symbols. Loader objects are untouched — the W251 emit is a separate
  directory behind an env var, and unset means `recomp_engine_small`, byte for byte.

## Boot — what W251 changed and what it did not

`VULCAN4_TRACE=3000000` full-entry run, 20 s budget, `nice -n 10 ionice -c3`, Xvfb :102
(`$B/run/w251-trace.log`, 6,696,572 bytes):

```
VULCAN4 BOOT REPORT functions_entered=4342 true_guest_entries=1530474 true_guest_exits=0
  halt=wallclock_deadline bios_files=0 intr_queued=71 intr_run=76 gs_packets=30
  frames_presented=1095 gs_frame_reg_writes=5 (ctx0=5 ctx1=0) pending_ip=0x0
VULCAN4 MISSING-BOUNDARIES n=0 :
VULCAN4 CALLKIND ... syscalls=27 total_syscall_calls=73014 ... missing_functions=0
VULCAN4 HARNESS detail=elapsed=20s pc=0x01000638 distinct_pcs=227 ... vblanks_processed=547
  frames_presented=1095 ... runnable_threads=tid1@prio1:pc=0x01000638(ready),tid2@prio2:pc=0x0101f348(ready)
```

The pre-W251 walls are gone: no `pc_outside_generated_table` (W229), no `guest_blocked` (W250),
`missing_functions=0`, `MISSING-BOUNDARIES n=0`. The guest now runs a full 20 s and hits the
**deadline**, with both EE threads `Ready` — a spin, not a block.

### THE MEASUREMENT THIS DISH EXISTED TO MAKE: the engine image is entered ZERO times

Instrument: the harness prints every dispatcher transfer (`[Dispatch] ... target_pc=0x...`) and
every outer-loop entry (`VULCAN4 TRACE ... pc=0x...`). Over the whole run:

| set | count | in `0x00100000..0x00617A14` (engine) | in `0x01000000..0x01051A00` (loader) |
|---|---|---|---|
| distinct `[Dispatch] target_pc` | 105 | **0** | 105 |
| `VULCAN4 TRACE` entries | 4,343 | **0** | 4,343 |
| any engine-range hex literal anywhere in the log | 1 (`0x00100000`, ×3) | — | — |

`0x00100000` appears three times and every one is the harness's own table-bounds report line
(`engine (0x00100000,0x00617a14)`). **No engine address is ever a control-flow target.** The 19,404
authoritative engine functions are emitted, linked, reachable and *unreached*.

Root cause, and it is upstream of the emit: **the loader never populates the engine's RDRAM.** The
guest opens `cdrom0:\CORE.GT4;1` (fd=4, fd=5, `[fioOpen]` at trace lines 1207/1216) but the
decompressor path that would write the image to `0x00100000` never completes; the two threads sit
Ready in the loader's own spin at `0x01000638` (986 of 4,342 entries, 22.7 % of the PC histogram)
and `0x0101f348`, and the run ends on the wall-clock deadline with the guest still working.
That is a *different* wall from "the emitted set is incomplete" and it is the honest boundary of
this dish: **an emit can be 100 % correct and still change nothing, because nothing jumps into it.**

### The one LOUD limitation, still standing (not new)

```
VULCAN 4 LIMITATION: syscall 0x5b override handler 0x80075000 has no generated function in either
image — not invoking (was silent KE_ERROR).            (×6)
```

`sce_SetSyscall` sets `0x5b → 0x80075000` and `sce_Copy` writes a blob to `0x80075000`. That address
maps to RDRAM `0x00075000`, which is **below** the engine image's `0x00100000` base — a third code
region, built at run time by the loader, that is in neither image. W250 named this exact handler; it
is not a new wall and W251 neither fixes nor worsens it.

## Gate: the picture is STILL the disclaimer

`bash .auto/verify-menu.sh` on the fresh capture `$B/run/w251-capture.png`:

```
newest capture : /mnt/ssd/vulcan4-build/run/w251-capture.png
structural sig  : capture 3 colours / nonblack 0.1173   vs reference 14 / 0.1150
GATE FAIL: STRUCTURAL MATCH to the disclaimer
GATE_EXIT=1
```

**The dish's headline deliverable is NOT achieved.** Per the STOP RULE, the stop is at the first new
named wall, and it is the one measured above: the loader never enters the engine image.
`docs/STATUS.md` needs no change — it already claims no menu.

### Gotcha, for the next dish

`0x80075000` is not "the engine". The engine is `0x00100000..0x00617A14`; `0x800xxxxx` is the
uncached KSEG0 alias of RDRAM, and `0x80075000` is 0x75000 — 0x8B000 bytes *below* the engine base.
Two different low-RDRAM code regions share this boot and only one of them is a recompilation target.
