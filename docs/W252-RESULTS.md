# W252 — loader→engine handoff — acceptance criteria FINAL (T2) · Tracks A/B/C/D closed

Dish: make the loader's DEFLATE decode of CORE.GT4 byte-identical to hardware so GT4's engine code
is actually entered. W251: emit 19,403/19,403 but the engine is entered **zero** times. Track A
(measurer, oracle) pinned it: the loader reads CORE.GT4 (`fd=5`, 2,020,861 B) and extracts the
engine image to RDRAM `0x00100000` (size `0x517A14`) correctly — the sole blocker is one DEFLATE
back-reference (first byte divergence at engine `0x001D51EC`), which fails the digest check at
`0x1004748` and drops the guest into the fail-trap `0x01000638` instead of ExecPS2-ing the engine.

## Acceptance criteria — FINAL (oracle-backed, Track A measurer 2026-10-07T20:45)

GOAL: make the loader's DEFLATE decode of CORE.GT4 byte-identical to hardware, so the engine image
at RDRAM `0x00100000..0x00617A14` matches and the guest actually ExecPS2's into it. T1 proved the
loader is correct UP TO the decode — fio read (`fd=5`, 2,020,861 B) and container extraction (dest
`0x00100000`, size `0x517A14`) are right — and the ONLY divergence is one DEFLATE back-reference:
first differing byte at engine `0x001D51EC` (hardware `0x7C` vs ours `0x2D`); the copy at
`sb $v0,0($t1)` @ `0x100f800` in FUN_0100F390 runs 12 B @ distance 0x18 on hardware vs 21 B @
distance 0x48 in ours (11,365 / 6,118,856 bytes differ, 517 runs, span `0xD521C..0xD80B1`). That
single bug makes the digest at `0x1004748` read `0x893d7a82` instead of `0x4bb5e6bf` →
FUN_01004500 fails → FUN_010047c0 returns 0 → fail-trap `0x01000638` taken (388/409 entries) →
engine never entered.

VERIFY (all four, in order):
1. BYTES (the precise gate; oracle = `/mnt/ssd/vulcan4-build/w229-oracle.bin`): OUR decode dump
   (`/mnt/ssd/vulcan4-build/w252-ours-blob.bin`) diffed against the oracle = 0 differing bytes, i.e.
   first-divergence offset `0xD521C` is gone and the digest at `0x1004748` reads `0x4bb5e6bf`.
2. ENGINE ENTERED (W251 instrument): full-trace boot
   `VULCAN4_TRACE=3000000 timeout 80 xvfb-run -a ./vulcan4_harness /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml 3000000 20`
   (from `$B/run`); classify every `[Dispatch] target_pc` by image range — require ≥1 target in
   `0x00100000..0x00617A14` AND `bios_files=0` in the same log.
3. PICTURE (the only proof of the menu): `bash /home/or/vulcan4/.auto/verify-menu.sh` exits 0 on a
   fresh window-only capture — measurably NOT the 2005 disclaimer.
4. MECHANICAL: `bash /home/or/vulcan4/.auto/verify-dish.sh` exits 0 — suite 497/497, captain-authored
   commit, clean tree, raw halt printed, missing-function hits named not silent.

REGRESSION:
- The loader's fio read + container extraction are PROVEN correct (T1) — the fix must not change them.
- Fix is scoped to the DEFLATE back-reference (distance 0x18 vs 0x48) and nothing else.
- Every probe OFF by default (`VULCAN4_W229_ORACLE` / `_OW` / `_CP` unset = today's behaviour).
- Suite 497/497 must not regress.
- `bios_files=0` every run (law 1). No game data in repo. Never edit runner/*.cpp or PS2Recomp .h;
  recompiler fixes go in tools/patches/.

STOP: the moment a fresh capture makes `verify-menu.sh` exit 0 (NOT the disclaimer) → STOP and hand
to reviewer (T4). Do not keep polishing after the picture changes.

## Track C — GS/present path (structural read + measured) — architect

Question: is the 2005 disclaimer redrawn every frame, or drawn once and re-presented (or drawn and
never presented)? Answer from a fresh read-only run (`/mnt/ssd/vulcan4-build/run/w252-w170.log`,
`VULCAN4_W170_FRAMEHASH=1`, 15 s): **neither** — the present path is healthy and re-presents the
same static VRAM ~846 times; the guest drew 30 GIF packets total and then stopped.

Present path, named (the whole chain, so a stranger can follow it):
`displayLoop` (vulcan4_harness.cpp:1987) → `presentFrame` (ps2_runtime.cpp:6299) → `UploadFrame`
(ps2_runtime.cpp:466, latches only when the vsync tick changed, :483) →
`GS::latchHostPresentationFrame` (gs_frontend.cpp:581) → `GSCpuBackend::Present`
(gs_cpu_backend.cpp:2260) → `PresentFromLocalMemory` (:2274) decodes DISPFB1/2 and copies VRAM→host RGBA.

Numbers:
- `frames_presented=846` in 15 s (~56 fps) — frames ARE presented; the present path is not the wall.
- `gs_packets=30` over the whole run, `gs_frame_reg_writes=5` (ctx0=5 ctx1=0) — the guest stops
  drawing early (freezes at 30 packets) while presentation keeps going.
- W170 frame hash (hashes the exact bytes about to become the window): **3 unique hashes** all run.
  Two (`0x48a5f86f0f7043bb`, `0x628912b1c98e528b`) alternate every frame in lockstep with vsync-tick
  parity — that is the odd/even interlaced-field presentation of ONE static image, not new content
  (the present path's `applyFieldPresentation`/`oddField = vsyncTick & 1`). The third
  (`0xd308a3e26b723613`) appears once mid-run (the only real content change). `nonzeroBytes`
  380080 → 387580 over the run — a ~2 % change, i.e. essentially static.

First stall, with addresses — it is UPSTREAM of the GS:
- `halt=stuck_in_syscall`, guest `pc=0x01000638` (`ra=0x01000630`, tid1 prio1 Ready) blocked inside
  SCE syscall `0x44` (WaitSema); tid2 `pc=0x0101f348` (`ra=0x0100afa8`) polling syscall `0x2f`
  (45,913 calls). The loader sits in WaitSema waiting for a semaphore the CORE.GT4 decompressor never
  posts — Track A's wall, not the present path. The GS never gets new packets because the guest is
  stuck before it.

Consequence for T3: fixing Track A (loader→engine) will un-stall the guest and the present path will
then show whatever the engine draws. Nothing in the present path needs changing for that handoff.

## Log

- 2026-10-07T20:29 · architect · claimed T2, drafted criteria from W251; waiting on T1 to finalize.
- 2026-10-07T20:34 · architect · Track C read + measured (w252-w170.log): present path healthy, wall
  is upstream (WaitSema @ 0x01000638). T2 criteria unchanged and still standing.

---

# SCRIBE — the record and the machine

Owner: scribe seat. This section is append-only evidence: raw gate output, disk state, and the
inventory of seat artefacts. Interpretation of any probe log below belongs to the seat that ran it,
not to this section — here they are only *located*, dated, and counted.

## Pre-restructure mechanical gate — BASELINE (raw, exit 1)

Run 2026-10-07 20:30, `bash /home/or/vulcan4/.auto/verify-dish.sh`, output saved verbatim at
`/mnt/ssd/tmp/w252-baseline-gate.txt`:

```
=== VULCAN 4 dish gate — 2026-10-07 20:30 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
FAIL  working tree dirty (1 entries) — commit or revert before claiming done
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w251-capture.png
      structural sig  : capture 3 colours / nonblack 0.1173   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 3 colours and a non-black fraction (0.1173) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w251-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/w252-bigcopy.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 6
=== RESULT: CLAIM NOT SUPPORTED ===
```

`GATE_EXIT=1`. What each verdict means, exactly:

- The only FAIL is **bookkeeping, not code**: `git status --porcelain` = `?? docs/W252-RESULTS.md`
  (this doc, in flight, untracked). No tracked file had a diff.
- Suite 497/497 and captain authorship both PASS, unchanged from W251.
- `verify-menu.sh` FAILS on the disclaimer — same structural signature as W251 (`0.1173` vs
  `0.1150`, 3 colours). **The picture is still the 2005 disclaimer.**
- `halt=stuck_in_syscall` here is **not a product claim** — see the confound below.

### MACHINE FINDING (for the gate's honesty): the gate reads the newest `$B/run/*.log`, and seats write there

`verify-dish.sh` §5 picks its log with `ls -t "$BUILD"/run/*.log | head -1`. At 20:30 another seat
was booting concurrently, so the baseline gate reported `w252-bigcopy.log` — a **seat probe run**,
not the gate's own boot. **The `halt=` line the gate prints is only trustworthy when no seat is
writing `$B/run/*.log` at the same time.** Until that is fixed (a gate-owned log path, or a
`VULCAN4_GATE_LOG` pin), read the gate's halt line as "newest boot on the box", never as "the gate's
boot". Recorded so nobody later quotes this baseline as a statement about the product.

### Post-commit gate re-run — MECHANICAL CLAIMS HOLD (raw, exit 0)

Run 2026-10-07 20:40, after this doc was committed (`3d776b9`, author `Or Golan <or024662@gmail.com>`),
saved verbatim at `/mnt/ssd/tmp/w252-gate-2.txt`:

```
=== VULCAN 4 dish gate — 2026-10-07 20:40 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w251-capture.png
      structural sig  : capture 3 colours / nonblack 0.1173   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 3 colours and a non-black fraction (0.1173) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w251-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/w252-ourhash.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 6
=== RESULT: MECHANICAL CLAIMS HOLD ===
```

`GATE_EXIT=0`. The only change from the 20:30 baseline is `FAIL working tree dirty` → `PASS working
tree clean`: the previous exit 1 was *bookkeeping only*. The picture gate still fails on the
disclaimer, and the `halt=` line still comes from a **seat's** log (`w252-ourhash.log`), not the
gate's own boot — the confound above is still live at 20:40. **The product claim of this dish is
therefore NOT "we pass the gate"; it is the byte-level first divergence in Track A.**

## Machine state — disk and load (the rule-6 check)

Measured 2026-10-07 20:31, same minute as the baseline gate:

```
Filesystem                         Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-ubuntu--lv  178G  145G   26G  85% /
/dev/sdb                           440G  334G   84G  80% /mnt/ssd
```

- **Root `/` is at 85 % — above the 80 % warn line.** 26 G free. Nothing in this dish writes there;
  keep it that way. Posted to the board.
- `/mnt/ssd` 80 %, 84 G free — the build root (`$VULCAN4_BUILD=/mnt/ssd/vulcan4-build`) and
  `TMPDIR=/mnt/ssd/tmp` live here, as the rules require.
- Load average `1.29, 0.95, 1.26`; `ps2x_tests` at ~98 % of one core (this gate), Minecraft JVM
  live and untouched. Gate ran under `nice -n 10 ionice -c3` with `TMPDIR=/mnt/ssd/tmp`.

## Seat-artefact inventory (what the other seats produced, 20:30–20:32)

Harness binary in every run below: `/mnt/ssd/vulcan4-build/run/vulcan4_harness`,
**347,669,656 B, mtime 20:16:19** — the W251 relink, unchanged. Every probe visible in these logs is
env-gated; the binary is the same one the gate uses.

| log (`/mnt/ssd/vulcan4-build/run/`) | mtime | bytes | `halt=` | distinct `target_pc` | in engine range |
|---|---|---|---|---|---|
| `w252-bigcopy.log` | 20:30:59 | 4,053,342 | `stuck_in_syscall` | 105 | **0** |
| `w252-fioread2.log` | 20:30:38 | 6,308,673 | `stuck_in_syscall` | 105 | **0** |
| `w252-blobwatch.log` | 20:31:16 | 5,226,740 | `stuck_in_syscall` | 105 | **0** |
| `w252-ctl.log` | 20:32:01 | 6,314,779 | `stuck_in_syscall` | 105 | **0** |
| `t3-fioread.log` | 20:31 | 6,271,733 | — | 105 | **0** |
| `w252-fioread.log` | 20:30:23 | 1,103 | (truncated, 1.1 KB) | — | — |
| `w252-huf.log` | 20:32:55 | 6,366,067 | `stuck_in_syscall` | 105 | **0** |
| `w252-w170.log` | 20:32:36 | 6,335,191 | `stuck_in_syscall` | 105 | **0** |
| `w252-cp.log` | 20:34:48 | 6,355,884 | **`wallclock_deadline`** | 105 | **0** |
| `w252-dump.log` | 20:35:31 | 6,345,283 | `stuck_in_syscall` | 105 | **0** |

All nine complete logs listed here: `bios_files=0`, **105 distinct `target_pc`, 0 in the engine
range.** (`w252-fioread.log` is truncated at 1.1 KB and carries no `target_pc` — excluded, not
counted as agreement.)

Method (robust, one grep — quoted so it is checkable):
`grep -ao 'target_pc=0x[0-9a-fA-F]*' <log> | sort -u` then count values in `0x00100000..0x00617A14`.
Engine range = `0x00100000..0x00617A14`. **Every full log run to date is 105 distinct targets / 0 in
the engine — the identical 105 W251 measured.** No engine address is ever a control-flow target.
`functions_entered` 3,388–3,422 across these runs. (A naive `grep -c '\[Dispatch\]'` yields 406 lines
because some are multi-line/repeated; the *distinct target set* is 105, and that is the number to
compare, never the line count — same trap as comparing by `functions_entered`.)

Two raw lines that pin the current halt and are quoted here only as evidence:

```
VULCAN4 BOOT REPORT functions_entered=3419 true_guest_entries=1529551 true_guest_exits=0
  halt=stuck_in_syscall bios_files=0 ... frames_presented=457 ... pending_ip=0x0
VULCAN4 HARNESS detail=blocked inside SCE syscall 0x44 (WaitSema), guest pc 0x01000638 ...
  runnable_threads=tid1@prio1:pc=0x01000638(ready),tid2@prio2:pc=0x0101f348(ready) ...
  deadline_s=8
```

**CAUTION — the `halt=` string is NOT a reliable signal in this window.** Within the same three
minutes, on the *same* binary (20:16, unchanged), the *same* run shape, the logs disagree:
`w252-cp.log` (20:34:48) reads `halt=wallclock_deadline` while `w252-huf.log` (20:32:55) and
`w252-dump.log` (20:35:31) read `halt=stuck_in_syscall`. So `wallclock_deadline` vs `stuck_in_syscall`
is **not** on its own evidence that the wall moved — it may be a run-config (TRACE / deadline) or
timing difference. What *is* stable across all ten runs is the thing that matters: **105 distinct
`target_pc`, 0 in the engine.** Compare runs by that set and by a picture, never by the halt string —
the same trap as `functions_entered`. (Architect's Track C, `w252-w170.log`, independently named the
same stall: `WaitSema` 0x44 at `pc=0x01000638`.)

Probe lines seen in `w252-bigcopy.log` (env-gated, names only, no interpretation):
`VULCAN4 RDRAMPROBE`, `VULCAN4 PROBE1/2/3/4` (the W247 `FindAddress` instrument — `0x010285F8` /
`0x010285C0` reported **MISSING**), `VULCAN4 W30BIGCOPY seq=257 ... op=fioRead pc=0x1004eb4
dst=0x10d1ac0 size=2020861`.

## Findings folded in from seats (as they land on the board)

### Track D — reviewer, method 1: W251's 19,403/19,403 emit re-verified independently (PASS)

Posted 2026-10-07T20:41. Recount done **directly from the artefacts** (did *not* run
`tools/check_engine_symbols.py`, so it is a genuinely independent reader):

- `engine-symbols.csv`: 19,403 data rows, all unique, 0 outside `.text [0x100000,0x617a14)`;
  range `0x1008d0..0x616ee8`.
- `ps2_recompiled_functions.h`: 19,404 unique `sub_` declarations (0 non-`sub_`).
- Across the 47 `.cpp`: 19,404 unique `void sub_(...)` definitions, 0 duplicates, all defined.
- `register_functions.cpp`: 523,410 table rows; all 19,403 CSV starts present as table targets,
  0 missing, 0 suffix mismatches.
- `nm run/vulcan4_harness`: 19,404 engine-range + 138 loader-range + 0 other = **19,542** `sub_` symbols.

**Verdict: coverage 19,403/19,403 = 100 %.** The single extra emitted address is `0x100008` (the ELF
entry), already LOUD-documented by W251.

### Track D — reviewer-verify, method 2: same claim, two different instruments (PASS)

Posted 2026-10-07T20:36. Deliberately used instruments that W251 did **not**: ground truth from
Ghidra headless (a function-manager script on project `gt4-engine`, program `w231-engine.bin`), and
the emitted side from `nm -C` on the linked binary.

- Ghidra headless: `count=19403`, `min=0x1008d0`, `max=0x616ee8` — address-by-address diff vs the CSV
  = **0 diff in both directions**.
- `nm -C /mnt/ssd/vulcan4-build/run/vulcan4_harness` (347,669,656 B, mtime 20:16 — the only harness
  binary): 19,542 distinct `sub_*`; engine-range `0x100000..0x617A14` = 19,404, loader-range = 138.
- Emitted vs Ghidra: **0 missing, 1 extra** = `0x100008` (the documented ELF-entry boundary).
- Instrument check: the binary carries 19,404 engine symbols (the old small emit was 758), so it is
  not stale — it *is* the W251 build.

**Verdict: PASS.** Named caveat (theirs): ground truth is Ghidra's own function manager, so this
confirms "100 % of *Ghidra's* starts" exactly as scoped — it does not re-adjudicate whether Ghidra
split functions correctly.

**Track D status: two independent readers, two different method pairs, both PASS.** The emit claim is
settled and closed; no further recount is owed. Track D does not touch the loader wall (Track A).

### Track A — measurer (T1): the CORE.GT4 path, and the FIRST DIVERGENCE located to one byte

Posted 2026-10-07T20:45. This is the dish's headline measurement: the engine is not entered because our
**decompressor's output is wrong**, and the first wrong byte is now named.

**1. The IOP path (measured, ours).** `[fioOpen] path="cdrom0:\CORE.GT4;1" flags=0x1 -> fd=4`
(2-byte sniff, `ra=0x1005008`) and `-> fd=5` (`ra=0x1004e38`); then
`[w163:read] fd=5 buf=0x10d1ac0 req=2020861 got=2020861` — the whole file, preceded by a `memset` at
`pc=0x1001038`. **No SIF RPC anywhere on this path.** Log `run/w252-huf.log` lines 1145–1155.

**2. Where the decode lands.**
`VULCAN4 W30BIGCOPY seq=259 op=memset pc=0x1001038 dst=0x12bf100 size=6119116`; blob pointer =
`0x12bf100 + 0x104 = 0x12bf204`, length `0x5d5dc8`. Container header parsed from the blob itself:
`count=3`, `entry=0x00100008`, member1 dest **`0x00100000`**, size **`0x517A14`**, from
`blob[0x30 : 0x517A44]`. **The engine image IS the decode output.**

**3. FIRST DIVERGENCE — by bytes, on the W251 binary.** Instrument
`VULCAN4_W229_STREAM=1 VULCAN4_W229_DUMP=1` dumps **our** decoded blob at the compare call site
(`[w229:str] ... buf=0x12bf204 len=0x5d5dc8`) to `/tmp/w229buf.bin`. Byte-diff against the hardware
dump `/mnt/ssd/vulcan4-build/w229-oracle.bin`:

| | value |
|---|---|
| ours vs hardware, first 8 bytes | `08fd8434c265bb15…` vs `7728b0eb540bc690…` |
| length both | 6,118,856 B |
| differing bytes | **11,365 of 6,118,856**, in **517** contiguous runs |
| span | blob `0xD521C .. 0xD80B1` |
| **first differing byte** | **blob offset `0xD521C` = guest `0x001D51EC`** (engine offset `0xD51EC` — inside the engine image `0x00100000..0x00617A14`, member 1) |
| hardware at that byte | `0x7C` (`7c 00 45 8c` = `lw a1,0x7c(v0)`) |
| ours at that byte | `0x2D` (`2d 30 00 00` = a repeat of the *preceding* word) |
| mechanism (same bytes) | hardware's 12 bytes are a copy from distance **0x18**; ours from distance **0x48** |

That is a **back-reference/copy-distance error in the decompressor**, and it is the first place our
output differs from hardware — everything downstream is a symptom of it.

**4. The consequence chain, measured.** At `0x1004748` (`FUN_01005870`, compare): the game's expected
`a0 = 0x4bb5e6bf`; **ours = `0x893d7a82`** (`[w230:oracle] injected=0 hashWord0=0x893d7a82`).
Mismatch → `FUN_01004500` returns 0 → the fail loop `0x01000638` is taken (388/409 entries) → the
engine is never entered. **A/B control:** with
`VULCAN4_W229_ORACLE=/mnt/ssd/vulcan4-build/w229-oracle.bin` the fail loop is taken **0** times. So
the decoder's output is, today, the **sole** loader blocker.

**5. Instrument warning (do NOT read this as "the write never happens").** `VULCAN4_W229_OW` armed
(`[w229:ow] ARMED [0x1394418,0x1394428)`) and the store dispatcher was live
(`[w122:disp] CONTROL delivered=f4240` = 1,000,000 stores, `lastAddr=0x1308939`), yet **0 hits**; the
`VULCAN4_W229_CP` probe at `sourcePc=0x0100f800` (62 entries) also saw **0** t1 in that window. Those
bytes **are** written — the dump proves it. Neither probe can see the decoder's stores into that
region. **Unresolved.** Next step the measurer named: drop the window gate from the CP probe (log the
first 40 t1 at `0x100f800` regardless) and rebuild.

**Artefacts (on the SSD, not in the repo):** `/mnt/ssd/vulcan4-build/w252-ours-blob.bin` (our blob —
keep), `/mnt/ssd/vulcan4-build/w229-oracle.bin` (hardware), logs
`run/w252-{dump,ourhash,cp,huf}.log`. No source changed; probes OFF by default.

### Track A — architect (T2/T3): the container is RAW DEFLATE, and the A/B that LAUNCHES the engine

Posted 2026-10-07T20:45–20:47. This is the second half of the Track A result: it proves the format,
proves the target against hardware, and shows the engine *does* come up when the blob is repaired.

**Container format, verified against the reference decoder.** `CORE.GT4` = 6-byte header + **RAW
DEFLATE** (`wbits=-15`). Reference check:
`zlib.decompressobj(-15).decompress(CORE.GT4[6:])` → **6,119,116 B = 0x5D5ECC** (== `u24@2`),
`eof=True unused=0`. `wbits` 15 and 47 both fail with *"unknown compression method"*. The writer is
`FUN_0100F390` (Huffman literals + match copy).
*Both length numbers in this doc are right — they are different objects:* buffer `0x5D5ECC`, payload
`0x5D5DC8 = 0x5D5ECC − 0x104` (T1 diffed the payload).

**Addresses.** Output buffer `0x012BF100` len `0x5D5ECC` (`MEMSET pc=0x1001038`); payload base
`0x012BF204` (= `dec+0x104`). Member table at `dec+0x104` (`u16(dec)=0x80`, `u16(dec+130)=0x80` ⇒
`param_1[7]=dec+0x104`), `count=3`; **member1 = dest `0x00100000`, size `0x517A14`, src `dec+0x134`
= exactly the engine image.** The copy is issued at `0x010048C0 jal 0x0101e81c` (memcpy) inside
`FUN_010047C0`.

**The gate.** `FUN_01004500` is a bool integrity validator: `0x1004688 FUN_01004448` = **SHA-512 over
(`0x12bf204`, `0x5d5dc8`)**; `0x1004748 FUN_01005870` = the compare. The copy runs only if it returns
nonzero; otherwise `FUN_010047C0` returns 0, `0x01000630 bnel s1,zero,0x01000658` is **not taken**, and
the guest spins at `0x01000638` — `ExecPS2` at `0x0100067C` is never reached.

**Hardware confirmation (oracle).** DS socket `127.0.0.1:21512`, GT4 SCUS-97328 paused:
- RDRAM `0x00100000..0x00200000` (1,048,576 B) == `dec[0x134:]` → **0 differing bytes**
- `0x1394400..0x1394900` == `dec[0x104 + (A−0x12bf204)]` → **0 differing bytes**

So dest `0x00100000`, src offset `0x134` and raw-DEFLATE `wbits=-15` are **hardware-true**.

**FIRST DIVERGENCE confirmed at both addresses:** `0x1394420` (RDRAM) = payload offset `0xD521C` =
engine `0x001D51EC`. Writer pc `0x100F800` (`FUN_0100F390`). Mechanistic hint from the W229 stream
(§19): call 49 `hufOut 0x189313a` with dest `0x1394420` means the **output local moved backward** —
i.e. a position decoded twice, which is exactly how a back-reference distance comes out wrong.

**THE A/B THAT CLOSES THE ARC (11 w252 logs, 20:30–20:35).** Only one log contains `EXECPS2`:
- control `run/w252-ctl.log` (no injection): `halt=stuck_in_syscall pc=0x01000638`, `EXECPS2=0`
- injected `run/w252-orc.log`: line 65692
  `[w230:oracle] copy dst=0x12bf204 len=0x5d5dc8 injected=1 hashWord0=0x4bb5e6bf`, then line 65856
  `VULCAN4 EXECPS2 -> unified resolve entry=0x00100008 (engine 0x00100008,0x00617a14)`; PC histogram
  holds `0x00100008` / `0x00100220` / `0x005ad8c0`; `sce_SetupHeap` from pc `0x001001e8`;
  `halt=guest_blocked pc=0x005ad8c8 ra=0x00100218`.

⇒ **Repairing the blob at the compare site is SUFFICIENT to launch the engine.** The stopper is the
**decode** — not the file read, not the copy target. This is the first time in the project the engine
image has executed.

**INSTRUMENT NOTE (bites every seat).** The `[Dispatch]` printout is **capped at n=400** (log line
985) while `TRACE` lines run to ~71,107. **Absence of a `[Dispatch]` line is NOT absence of a
dispatch.** Do not use "0 of 400" as evidence beyond "the first 400 targets contain none in range".

**T2 FINAL acceptance criteria (architect) — verbatim.**
- GOAL: loader's DEFLATE decode of `CORE.GT4` byte-identical to hardware, so the engine image at
  `0x00100000..0x00617A14` matches and the guest `ExecPS2`s into it. T1 proved the loader correct UP
  TO decode; ONLY blocker = one DEFLATE back-reference: first diff at engine `0x001D51EC` (hw `0x7C`
  vs ours `0x2D`), copy @ `0x100f800` in `FUN_0100F390` runs 12 B @ dist `0x18` (hw) vs 21 B @ dist
  `0x48` (ours) → digest `0x1004748` = `0x893d7a82` vs `0x4bb5e6bf` → fail-trap `0x01000638`
  (388/409) → engine never entered.
- VERIFY: (1) BYTES: our dump `w252-ours-blob.bin` vs oracle `w229-oracle.bin` = 0 diff bytes
  (digest `0x4bb5e6bf`). (2) ENGINE ENTERED: ≥1 `[Dispatch]` target in `0x00100000..0x00617A14`,
  `bios_files=0`. (3) `bash .auto/verify-menu.sh` exit 0. (4) `bash .auto/verify-dish.sh` exit 0
  (497/497).
- REGRESSION: fio read + container extraction untouched (proven correct); fix scoped to back-ref
  only; probes OFF by default; suite 497/497; `bios_files=0`.
- STOP: fresh capture NOT the 2005 disclaimer → hand to reviewer T4.
- Open (non-blocking): measurer-static's Ghidra static read of `FUN_0100F390` — it sharpens *where*
  to fix, not the criteria. Builder (Track A) is unblocked.

## Unknown / open (as of 20:50)

- ~~The exact semaphore the loader waits on~~ **RESOLVED — it waits on nothing.** The Track B addendum
  (bottom of this doc) shows all 20 waits pair with 19 signals *inside the same functions* — the
  waiter is the poster, the object is a DMA channel (`0x70002000 + channel*0xC + 0x74`), and **no
  WaitSema/SignalSema call site has a caller anywhere near the decoder `0x0100f8xx`.** `0x01000638`
  is a terminal trap reached only when `s1==0` (Track B), and the `WaitSema 0x44` attribution is an
  instrument artefact: the *immediate* caller of every `0x44` is the WaitSema stub itself
  (`0x0101f468`), none is `0x01000638`. The earlier `FindAddress 0x83` / `0x5b` leads were artefacts.
- **CORRECTED (addendum §4): `0x01000638` is not the hottest PC.** `VULCAN4 PC HISTOGRAM distinct=153`
  top: `0x0101d3a0=1087 (25.03 %)`, `0x01000638=986 (22.71 %)`, `0x0101f348=30 (0.69 %)`.
  `0x0101d3a0` is the **epilogue** of `FUN_0101d2a0` (`ld s1/s2/s3/ra; jr ra; addiu sp,sp,0x50`) reached
  by `bnel v0,zero,0x0101d3a0` at `0x0101d2c8` — a hot *call count*, not a spin. And `0x0101f348` is
  the resume of the **`0x32` SleepThread** stub (not the `0x2f` poller, resume pc `0x0101f318`).
- **UNKNOWN — the decoder bug itself.** We know *where* (copy distance at blob `0xD521C`) but not
  *why* our LZ parser chose distance `0x48` where hardware chose `0x18`. Not yet read in source.
- **UNKNOWN — the two dead probes.** `VULCAN4_W229_OW` / `VULCAN4_W229_CP` see 0 events in a region
  the dump proves is written. Means "the instrument is blind", not "no write".
- Whether any seat's loader change has landed in tracked source. `git diff` over tracked files is
  **empty**; the only untracked path is this doc. Nothing has been committed for W252 yet.
- `docs/STATUS.md` is dated 2026-09-30 and still claims `functions_entered=25`, `449/449` tests,
  "3 → 25 guest functions". Reality is 497/497 and ~3.4 k functions. It is stale by ~7 dishes — a
  rewrite is owed (nobody has claimed it). **Scribe flags it; the lead should assign it.**

## Exact next step for a stranger

1. Read the criteria block above (architect, T2) — it is the acceptance test for this dish.
2. To reproduce the baseline: `TMPDIR=/mnt/ssd/tmp bash /home/or/vulcan4/.auto/verify-dish.sh`;
   expect exit 1 **only** on the dirty tree while this doc is untracked, and a disclaimer picture.
3. Engine-range classification of any boot log (the W251 instrument, costs one grep):
   `grep -aoE '\[Dispatch\][^ ]* target_pc=0x[0-9a-fA-F]+' <log> | grep -aoE '0x[0-9a-fA-F]+$'` then
   count those in `0x00100000..0x00617A14`. Zero = the engine is still never entered.
   **Caveat:** the harness prints at most **400** `[Dispatch]` lines; absence beyond that is the cap,
   not evidence. Use `VULCAN4 EXECPS2` / `TRACE` lines for anything past n=400.
4. Do **not** run the gate while a seat is writing `$B/run/*.log` and then quote its `halt=` line.

**The W253 next step, in one line:** fix the DEFLATE match-copy in `FUN_0100F390` (writer pc
`0x100F800`) so that at payload offset `0xD521C` it emits a copy of 12 bytes from distance `0x18`
instead of 21 bytes from `0x48`; the stream hint is that the output local moved *backward* there —
a position decoded twice. Prove it with the A/B already built:
`VULCAN4_W229_ORACLE` off (ours) vs on (hardware) → fail trap `0x01000638` taken 388/409 times vs 0;
`run/w252-orc.log` already shows the engine entering at `0x00100008` once the blob is right.
Acceptance is the T2 block above (0 diff bytes → engine `[Dispatch]` in range → `verify-menu.sh` 0 →
`verify-dish.sh` 0).

## Track B (builder-spin) — what the two "spinning" PCs actually are

Source: Ghidra on SCUS_973.28 (the open program), byte-exact disassembly + decompile.

**1. The exact code at each PC.**

`0x01000638` (tid1) — inside `FUN_01000558`. Body is five `nop`s
(`0x01000638`/`63c`/`640`/`644`/`648`) then `0x0100064c: b 0x01000638` / `0x01000650: nop`.
No load, no test, no syscall. A **terminal infinite loop** — GT4's deliberate failure trap
(the harness source names this shape: "five NOPs then a backward branch … a failure trap").
Harness counts it: `VULCAN4 CALLER fn=0x01000638 entries=66 from=1ra[0x01000630]`.

`0x0101f348` (tid2) — **not a loop at all.** It is the `jr ra` of the `SleepThread` stub:

```
0101f340: 32000324    li v1,0x32
0101f344: 0C000000    syscall 0x0
0101f348: 0800E003    jr ra
0101f34c: 00000000    _nop
```

Proof of the PC convention: the tally reads
`0x32 sce_SleepThread calls=1476 last_pc=0x0101f348 from=1pc[0x0101f348]` — the recorded
syscall PC is the **post-syscall resume** address, not a spin site. tid2 is the sleeping thread.

**2. What each waits on, and who sets it.**

`0x01000638` waits on **nothing** — the input is a one-way branch. The trap is reached only when
`bnel s1,zero,0x01000658` at `0x01000630` is **not taken**, i.e. `s1 == 0`. `s1` is loaded at
`0x010005e4: _move s1,v0` (the delay slot of `beq s0,zero,0x01000604`) from the return of
`FUN_010047c0`. Decompile of `FUN_010047c0` — the CORE.GT4 segment loader — returns
`uVar11` = the header word at `[+4..+7]`, and it is **0** when `FUN_01004500` (version/collation
check) returns 0. So the trap is a **one-way abort**: nobody sets the flag, because the other arm
is `s1 != 0` → `FUN_010183b0` → `FUN_01028ad8` → `_ExecPS2` (EE syscall `0x07`) into the engine.
This is the same root the board already carries (blob hash `0x893d7a82` vs expected `0x4bb5e6bf`).

`0x0101f348` waits on the scheduler to wake the sleeping thread (SleepThread return value).
It is a parked thread, not a spin.

**3. Named hypothesis — CAUSED BY, not the cause.**

The spins are **CAUSED BY** the engine never being entered. Evidence, not intuition: the trap sits
on the *not-taken* arm of the very branch (`0x01000630`) whose *taken* arm calls `_ExecPS2`. Both
outcomes share **one input** — `FUN_010047c0`'s return word. A trap that is selected by the same
value that selects engine entry cannot cause the engine skip; it is what GT4 does *because* the
skip happened. And tid2's `0x0101f348` is not a spin at all, so it cannot be causing anything.

**Instrument artifact — do not read `halt=stuck_in_syscall` as guest state.**

The W252 seat logs say `blocked inside SCE syscall 0x44 (sce_WaitSema), guest pc 0x01000638`.
That is self-contradicting: `0x01000634` is `lui s0,0x104`, **not** a syscall, and the tally shows
all 20 `0x44 sce_WaitSema` calls came from pc `0x0101f468` (`from=1pc[0x0101f468]`), none from
`0x01000638`. The same line also prints `tid1:status=1:wait=none#0:woken=0` (Ready, no wait).
Mechanism: `WaitSema` → `EeScheduler::waitSemaphore` → `blockCurrent` → `throw EeDispatcherTransfer{}`
unwinds out of `handleSyscall` **past both** `m_activeSyscallId = previousActiveSyscall` restores
(`ps2_runtime.cpp:5146/5152`), so `0x44` latches and can never clear. Nondeterminism confirms it is
not a statement about the guest: W251's 20 s control printed the **deadline** branch with the
**identical** `0x44 sce_WaitSema calls=20` tally.

## Track B addendum — the semaphores, named (lead's convergence question)

All numbers below are from `$B/run/w251-trace.log` (20 s, `halt=wallclock_deadline`) and Ghidra
on SCUS_973.28. The syscall tallies carry, per call site, `ra` (the guest's `$ra` **at the syscall**),
`a0`, `a1`, `s0`. Because the wrappers below end in a **tail `j`**, `$ra` at the syscall is the
*outer* caller's return address — the call site is `ra - 8`.

### 1. The semaphore ID is loaded from memory, not passed

`FUN_0100ae18` (WaitSema wrapper), byte-exact:

```
0100ae18: sll  v0,a0,0x1
0100ae20: addu v0,v0,a0
0100ae28: lui  v1,0x7000
0100ae2c: ori  v1,v1,0x2000
0100ae30: sll  v0,v0,0x2          ; v0 = channel*0xC
0100ae34: addu v0,v0,v1           ; v0 = 0x70002000 + channel*0xC
0100ae3c: lw   a0,0x74(v0)        ; <-- the semaphore ID
0100ae40: j    0x0101f460         ; TAIL call, the WaitSema stub
```

`FUN_0100ae48` (SignalSema wrapper) is identical and tail-calls `0x0101f440`. So the ids are the
per-channel struct field at `0x70002000 + channel*0xC + 0x74`, filled by `sce_CreateSema`
(`ra_count=0x0100ac8cx3,0x0102857cx2,0x0102858cx2,0x0100a3e4x1,0x010009b8x1`, 9 calls).

### 2. Which ids, and who posts them — **the waiter is the poster**

| sema | Wait site (`ra`) | n | Signal site (`ra`) | n | object |
|---|---|---|---|---|---|
| 7 | 0x0100b144 | 5 | 0x0100b1a0 | 5 | `$s0=0x70002050` |
| 8 | 0x0100b14c | 5 | 0x01000b74 | 5 | `$s0=0x70002050` |
| 8 | 0x01009ca0 | 4 | 0x01009cd0 | 4 | — |
| 9 | 0x01000de8 | 2 | 0x01000df0 | 2 | `$s0=0x01047a80` |
| 4 | 0x0101b658 | 1 | 0x0101b674 | 1 | `$s0=0x01049780` |
| 6 | 0x0100b2a8 | 2 | 0x0100b2c4 | 2 | — |
| ? | 0x010009c8 | 1 | **none** | — | the initial take |

Counts match semaphore by semaphore (20 waits, 19 signals). The pairing is not "two threads
handshaking" — it is the **same function** doing wait-then-work-then-signal:

- sema 7 — `FUN_0100b108`: wait `jal FUN_0100ae18(1)` @0x0100b13c, signal `jal FUN_0100ae48(1)` @0x0100b198 (0x5C apart)
- sema 9 — `FUN_01000dc0`: wait @0x01000de0, signal @0x01000de8 — **8 bytes apart, the whole body is acquire/release**
- sema 8 — wait @0x01009c98 and @0x0100b144, signal @0x01009cc8 (0x30 apart) and @0x01000b6c
- sema 4/6 — wait/signal in one function, 0x1C apart

The object is a **DMA channel** (`0x70002000` base, stride `0xC`, sema at `+0x74`); `FUN_0100b108`
is channel init/teardown (`sb zero` to the channel struct, `jal FUN_0100de58` to free the two
buffers, then signal).

The one unpaired wait is the **initial take of the main-thread gate**: in `FUN_01000940`,
`sce_CreateSema` @0x010009b0 (count 1) then `jal 0x0101f460` (WaitSema) @0x010009c0, whose **delay
slot** `sw v0,0x7a80(v1)` stores the new id to global `0x01047a80`. `FUN_01000dc0` is what releases
it (signal at 0x01000de8). Same function then does `sce_CreateThread` @0x01000a00 (entry
`0x01000ba0`, stack `0x01041a80` size `0x4000`), `syscall 0x2f`, `sce_ChangeThreadPriority`.

### 3. Is it the CORE.GT4 decompressor? **No.**

The decoder is the region probed at `sourcePc=0x0100f800`. **No WaitSema or SignalSema call site has
a caller anywhere near 0x0100f8xx** — all 7 wait sites and 6 signal sites are in the
`0x01000xxx / 0x01009xxx / 0x0100bxxx / 0x0101bxxx` library region. Nothing external posts these
semaphores, and nothing needs to. The decompressor does not participate in them at all.

### 4. Two corrections to the architect (disassembly contradicts the claim)

- **`0x0101f348` is NOT the 0x2f poller.** Raw bytes: `0x0101f310: 2F 00 03 24` (`li v1,0x2F`),
  `0x0101f340: 32 00 03 24` (`li v1,0x32`). The 0x2f stub's resume PC is `0x0101f318`; the tally
  agrees (`0x2f ... last_pc=0x0101f318`) and the 0x2f callers are `0x01011314`/`0x0101153c`
  (`a0=0x01047b4c`, 22,950 each). `0x0101f348` is the resume of **`0x32` SleepThread**.
- **`0x01000638` is not the top spin site.** `VULCAN4 PC HISTOGRAM distinct=153 top:
  0x0101d3a0=1087(25.03%) 0x01000638=986(22.71%) ... 0x0101f348=30(0.69%)`. And `0x0101d3a0` is the
  **epilogue** of `FUN_0101d2a0` (`ld s1/s2/s3/ra; jr ra; addiu sp,sp,0x50`), reached by
  `bnel v0,zero,0x0101d3a0` @0x0101d2c8 — the fast-path return when `FUN_01010ff8(x) != 0`. It is a
  hot *call count*, not a spin. tid2's `0x0101f348` is 30 hits = **0.69 %**, not a spinning thread.
