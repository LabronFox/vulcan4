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

---

## Track A — builder-1: the divergence is a VALID COPY at the wrong (L,D) — instrument armed

Appended 2026-10-07 by builder-1 (TRACK A). All numbers below are re-measured in this session from
the two blobs on disk; nothing here is inherited without a check.

### The measurement (byte level, reproduced)

- `CORE.GT4` = `/mnt/ssd/gt4/work/CORE.GT4`, 2,020,861 B. Header `01 01 cc 5e 5d 00`, declared size
  LE u32 at +2 = **6,119,116**; payload from +6 is **raw DEFLATE**; 43 dynamic-Huffman blocks.
  `zlib.decompressobj(-15).decompress(f[6:])` = 6,119,116 B and `ref[0x104:]` is byte-identical to
  the hardware blob. So **blob offset X = decode-buffer offset X + 0x104**.
- Diff of hardware blob (`/mnt/ssd/vulcan4-build/w229-oracle.bin`, 6,118,856 B) vs ours
  (`/mnt/ssd/vulcan4-build/w252-ours-blob.bin`, same size):
  **11,365 differing bytes, 517 runs, first 0xD521C, last 0xD80B1, all inside the single 64 KB
  bucket 0xD0000–0xDFFFF, and identical from 0xD80B1 to EOF** (5,796,331 bytes identical).
- Our bytes at the divergence are `orc[B-0x48 : B-0x48+21]` **exactly**
  (`2d3000000000b0ff2d8080000800bfff2d38000000`): a self-consistent **L=21, D=72** copy of the
  (identical) prefix. Best backward matches at B are `(len,dist) = [(21,72),(6,5164)]` — 72 is
  unique.
- Reference token stream (independent DEFLATE walker, validated token-for-token against zlib): the
  token that writes decode-buffer `0xD5320` is global token **132659**, starting at bit **1616375**,
  `sym=265 L=12 dsym=8 D=24`, code lengths `6/1/5/3` = **15 bits**. A hypothesis of `sym=269 /
  dsym=12` costs `6/2/5/5` = **18 bits**.
- Both the divergence (0xD5320) and the heal (0xD81B5) are inside **one** dynamic block (blk4:
  buffer 0xd4003..0x109c83, bits 1596742..2010452). A 3-bit desync inside a single dynamic block
  cannot re-sync — yet the bytes do re-sync, byte for byte, for 5.8 MB.

**Those two facts cannot both be true.** The token hypothesis (wrong symbols, same bit budget) and
the desync hypothesis (wrong bit position) each contradict one of the measurements. Inference from
the blob is exhausted; the decoder has to be observed directly.

### The instrument (added this session, inside the allowed edit surface)

`tools/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp` — new probe `vulcan4W252CopySub`, installed by
`ps2AddStoreSubscriber` **only if `VULCAN4_W252_COPY` is set**; unset ⇒ no subscriber registered, no
branch taken, today's behaviour byte for byte. Range/limit via `VULCAN4_W252_LO`/`_HI` (hex, default
`0x01394400`/`0x01394480`) and `VULCAN4_W252_N`.

Why it can settle the question where W229's `VULCAN4_W229_OW`/`_CP` could not:
`VULCAN4_W229_CP` filters on `sourcePc == 0x0100F800u`, which is the **direct**-copy `sb` only. The
emitted code (`recomp/ps2_recompiled_functions.cpp:77897-78470`) has **two** dest stores:
`0x100f780` (`sb $v1,0($t1)`, window loop) and `0x100f800` (`sb $v0,0($t1)`, direct loop). And
`VULCAN4_W178_COPY` has a **30,000-line cap** on all WRITE8 from `0x100f390..0x100f8c8`, so it is
exhausted long before the divergence at output byte 873,248. The new probe filters by **address**,
not by a global counter.

The `beql $t5,$zero,0x100f7f8` at `0x100f758` decides which register means what, and the annulled
delay slot is the key: taken (t5==0, direct) ⇒ delay slot `subu $s1,$t1,$s1` **executes** ⇒ `s1` =
source pointer. Not taken (t5!=0, window) ⇒ **delay slot is nullified** ⇒ `s1` keeps its original
value = **the distance**. So at either `sb`: `t1` = out, `t4` = out end, `s3` = bytes remaining
(== L on the first iteration), `s1` = distance (window) or source (direct), `t3` = window index,
`t5` = window base. Registers `a2/a3/t0/t2` (bit count / bit buffer / input ptr / input end) are
**not touched by the byte loops**, so they still carry the live bit state at the store — that is the
absolute bit position the reference says should be 1616375.

Expected outcomes, both decisive:
- observed `s1`(dist) = 0x18 and `s3` = 12 with bit position 1616375 ⇒ our decoder took the RIGHT
  token; the defect is in the copy arithmetic or the window.
- observed dist = 0x48 and `s3` = 21 ⇒ our decoder took a DIFFERENT token, and the bit position
  will say whether the stream desynced (≠1616375) or the tables were mis-built (=1616375).

Instrument file/line: `ps2_runtime.cpp` `vulcan4W252CopySub` (definition) + its install block beside
the W178/W181 installers.

---

## Track A — MEASURED ANSWER (measurer/oracle, 2026-10-07T21:43:10+03:00)

The probe above was run and the question it posed has an answer. **Both outcomes occurred, in that order.**

### Deliverable 3+4 — the first divergence, triple-sourced

| value | source | at guest `0x1394420` (= decode `0xD5320` = blob `0xD521C`) |
|---|---|---|
| **0x7C** | **HARDWARE** — PCSX2 DebugServer `read_memory 0x1394400` → `2d3000002d3800007c00458ca651110c...` | oracle |
| **0x7C** | python `zlib.decompress(CORE.GT4[6:], -15)` at `0xD5320` | byte oracle |
| **0x2D** | our runtime, `/mnt/ssd/vulcan4-build/w252-ours-blob.bin` | ours |

- **Hardware = 0x7C.** Ours = 0x2D. First differing byte of our blob vs the reference: **blob `0xD521C`**
  (computed by scan, not asserted).
- **First disagreeing instruction: `sb $v0,0($t1)` at guest pc = `0x0100F800`**, `ra=0x1010a70`,
  inside the inflate core `FUN_0100F390` (0x100f390..0x100f8c8), called from driver `FUN_0100F8C8`
  at its call site `0x1010a70`.
- Address map, confirmed against the oracle read: `decode_offset = blob_offset + 0x104`;
  `guest = 0x12bf100 + decode_offset`. (`0x1394400 → 0xD5300 → blob 0xD51FC`, which is exactly the
  probe's printed `out-off-in-blob=0xd51fc`.)
- Harness: `/mnt/ssd/vulcan4-build/run/vulcan4_harness`, 347,674,720 B, sha256
  `c95ac587c0f013b78261ec9f38fbaca054df6001b35d52622069a13c10dc5519`, mtime 21:30.

### The mechanism — measured, and it is NOT the token arithmetic

The doc above predicted: `dist=0x18, s3=12` ⇒ right token, copy arithmetic is at fault;
`dist=0x48, s3=21` ⇒ wrong token. **We observe the wrong-token values** — but the sharp part is that
the *same runtime, at the same instruction, with the same bit state, produced the right token first.*

Raw `[w252:copy]` lines (n, seq are DECIMAL; `run1.log` lines 1528/1529/1706ff):

- **n=64 seq=75 `pc=0x100f800 dst=0x1394420 val=0x7c s1=0x1394408 s3=0xc a2=0x0a a3=0x47281f44 t0=0x110300c`**
  → L=12 D=0x18 — **the hardware/reference token, and the hardware byte 0x7C.** Our first pass is CORRECT.
- **n=161 seq=172 `pc=0x100f800 dst=0x1394420 val=0x2d s1=0x13943d8 s3=0x15 a2=0x08 a3=0x51ca07d1 t0=0x110300c`**
  → L=21 D=0x48 — the wrong token, which **overwrites** the correct byte with 0x2D.

Between those two stores the out pointer went **backwards**, twice, and the bit state went backwards with it:

- **rewind #1 — n=32 seq=43**: out `0x139441e → 0x1394400` (−30). The replay n=32..62 reproduces n=1..31
  **bit-for-bit, every register** (n=1 and n=32 are both
  `val=0x2d s1=0x138fcd0 s3=0x4 a2=0x11 a3=0x19872e41 t0=0x1103000`). A bit-exact replay of the same
  instruction stream.
- **rewind #2 — n=160 seq=171**: out `0x139447f → 0x139441f` (−96). n=160 is **exactly** n=63
  (`val=0x00 s1=0x1392ff7 s3=0x6 a2=0x19 a3=0x0fa276c6 t0=0x110300c`), and the next store should be n=64 —
  but it is n=161 `a2=0x08 a3=0x51ca07d1 L=21 D=0x48`. **Identical inputs into the token step, different
  token out.**
- Both passes take the **DIRECT** path (`t5=0`, `winBase=0`) at `beql $t5,$zero,0x100f7f8`
  → the window path (0x100f760..0x100f7f0) is excluded from the divergence.
- 200 in-range stores total, `n=1..200`, `seq=12..211`, consecutive; pcs `{0x100f4ec, 0x100f800}`;
  dst `0x01394400..0x0139447f`; every store `path=DIRECT`. (`0x100f4ec` is `sb $s3,0($t1)` — a different
  store site; its `s1≈0x1ffd0xx`/`s3` are not source/length and must not be read as a match-copy.)

### What this excludes, and the next measurement

The **flush callback cannot be the rewind.** At `0x100f810` (mirrors `0x100f7b0`, `0x100f530`) the code
does `sw $t4,0x20($s0)` (writes outEnd into the job's out slot) then `jalr` the callback
`*(job+0x44)+0x18`, then `lw $t1,0x20($s0)`. It is reached only when `bne $t1,$t4,0x0100f864` is
**not** taken. In every logged store `t4 = 0x12bf0ff` = buffer base − 1 — the same sentinel the
`[w229:huf]` lines print as `f24(outend)=0x12bf0ff` (with `f1c=0x12bf100` the base). `t1` (0x01394xxx)
can never equal it, so **the flush never fires inside FUN_0100F390 and cannot move `out` backwards.**
The rewind is therefore a **re-entry of the inflate core** — either the driver `FUN_0100F8C8` calling it
again with a reset window, or our runtime re-executing the block with restored CPU state but
**un-restored memory** (which is exactly why replay #1, which never reads the bytes it just wrote, is
bit-exact, while replay #2, whose token is a distance-24 self-referential copy, is not).

**NEXT MEASUREMENT (one probe, decisive):** log the job struct — `s0 = 0x1ffce10` in every line —
fields `+0x20` (out), `+0x24` (outEnd), `+0x30`/+`0x34` (lit/len and distance table bases),
`+0x38`/+`0x3c` (masks), `+0x44` (callback ptr) — at each of the two passes, and log the return
address that re-entered FUN_0100F390. That distinguishes "driver re-entered" from "our runtime re-executed"
and names the fix target.

### Instrument trap (for the next reader of `run1.log`)

`[w252:copy] n=` and `seq=` are **DECIMAL**; `[w229:huf] n=` is **HEX**. Parsing the copy probe's
`n`/`seq` as hex fabricates a phantom 512-line pass with impossible jump structure. Verified by grep
against the raw file: text ranges are `n 1..200`, `seq 12..211`.

## Track A — MECHANISM (measurer/oracle, 2026-10-07T18:51:00Z): the damage is a REWRITE, not a desync

Run: `/mnt/ssd/tmp/w252meas/ref2.log` (REF mode, Xvfb :102, n=2,000,000, timeout 220).
Probe prints `MIS=<k> w=<n> dst=...` only when the stored value != `ref.bin`. REF mode widened
LO/HI to the whole blob `[0x12bf204, 0x12bf204+0x5D5DC8)` (the LO/HI env is overridden there).

### Hard facts (no inference)

| # | Fact | Number |
|---|---|---|
| 1 | Blob length | 6,118,856 B (0x5D5DC8) |
| 2 | `w` at the FIRST wrong store (MIS=1) | **6,120,817** |
| 3 | MIS=1 dst / got / exp | `0x1394420` / `0x2d` / `0x7c` |
| 4 | MIS=1 pc / ra / path | `0x0100F800` / `0x1010a70` / DIRECT |
| 5 | MIS dst span (first 80, cap) | `0x1394420..0x1394477` (88 B), strictly ascending, all unique |
| 6 | MIS pc mix | 67 × `0x100f800`, 13 × `0x100f4ec` |
| 7 | Final `mis` (wrong STORE EVENTS) | **48,944** at w=6,172,000 |
| 8 | Distinct damaged BYTES (blob vs ref.bin) | **11,365** |
| 9 | `loopStores − w` | constant **118,420** (out-of-blob mirror stores) |

**Fact 2 is decisive.** `w` counts in-blob stores in emission order. A single monotonic pass
reaches blob offset `0xD521C` at `w = 872,989`. MIS=1 arrives at `w = 6,120,817 > 6,118,856`,
so **the byte at 0xD521C was already written (correctly) at least one full pass earlier and was
then OVERWRITTEN with 0x2d.** This is not "our decoder diverges at 0xD521C"; it is "our decoder
finishes a pass, starts another, and the second pass corrupts a bounded slice".

Fact 8 vs 7: 48,944 wrong store events for 11,365 distinct wrong bytes => the damaged window is
overwritten **~4.3x** on average.

### Measured backward jumps (STAT every 1000 stores, `lastdst` = live t1)

```
w 872000->873000   off 0xd4e3f->0xd4a80  delta -959
w 6120000->6121000 off 0x5d5a98->0xd52d3 delta -5244869   <- END OF PASS 1, restart
w 6122000->6123000 off 0xd56bb->0xd52cc  delta -1007
w 6134000->6135000 off 0xd7dc4->0xd5aec  delta -8920
w 6136000->6137000 off 0xd5ed4->0xd5ae5  delta -1007
w 6146000->6147000 off 0xd7e0d->0xd630c  delta -6913
w 6148000->6149000 off 0xd66f4->0xd6302  delta -1010
w 6156000->6157000 off 0xd7e5a->0xd6b33  delta -4903
w 6158000->6159000 off 0xd6f1b->0xd6b1f  delta -1020
w 6164000->6165000 off 0xd7ea7->0xd7364  delta -2883
w 6166000->6167000 off 0xd774c->0xd7365  delta -999
w 6170000->6171000 off 0xd7f1d->0xd7ba9  delta -884
```

Shape: pass 1 runs **0 -> 0x5D5A98** (~blob end), then the pointer jumps back 5,244,869 to
**0xD52D3** (= first-divergence offset 0xD521C + 0xB7). From there the pointer sawtooths:
it advances to ~`0xD7DC4..0xD7F1D` and rewinds, landing progressively higher
(0xD52CC, 0xD5AEC, 0xD630C, 0xD6B33, 0xD7364, 0xD7BA9). **The rewrite region
0xD52CC..0xD7F1D sits exactly inside the measured damage window 0xD521C..0xD80B1, and the
last rewind (0xD7F1D) is just before the window's clean tail 0xD80B1.** After that the output
is correct to the end of the blob.

### What this kills

- "one wrong token at 0xD5320 cascades" — false: the byte was correct first.
- "the flush callback (vtbl+0x18) causes the backward movement" — already ruled out; t4 is the
  sentinel 0x12bf0ff = base-1 and is never reached, so `0x100f810` cannot run.
- "our decoder is desynced from 0xD521C onward" — false: it re-converges at 0xD80B1 and the
  whole rest of the blob (5.1 MB) is byte-identical to hardware/zlib.

### Instrument caveats (do not read these as signal)

- `seq = n + 11` always: `ps2NextTraceSequence()` is called only AFTER the in-range check
  (`ps2_runtime.h:284`), so `seq` carries no independent information.
- REF mode silently overrides `VULCAN4_W252_LO/HI` to the whole blob, so `w` counts every
  in-blob store, not stores inside the damage window.
- `t0` printed at MIS=1 is `0x110300c` — that is inside the fio read buffer `0x10d1ac0`
  (= +202,060, i.e. 10% into the 2,020,855-byte compressed body), while the output pointer is
  14% into the decoded image. Do **not** yet conclude "the input pointer is inconsistent": t0 is
  a register, and the live inPtr lives in the job struct at `s0+0x0c`. Measure the struct.

### NEXT MEASUREMENT (proposed, narrow)

At each backward jump log the job struct `s0 = 0x1ffce10` fields — `+0x0c` inPtr,
`+0x10` inEnd, `+0x14` winBase, `+0x18` winIdx, `+0x20` out, `+0x24` outEnd,
`+0x30`/`+0x34` Huffman bases, `+0x38`/`+0x3c` masks, `+0x44` vtbl — plus `ra`.
That separates "the driver (`FUN_0100F8C8`) re-entered the core" from "the core's own loop
restarted", and names the fix target: a core restart with live out/in pointers is a driver bug;
a restart with a stale window/Huffman base is a core bug.

## Track A — the Track A CONTRADICTION IS DISSOLVED (2026-10-07T18:51:15Z)

The header of `ps2_runtime.cpp` recorded a contradiction that stalled Track A verbatim:
*"Our diverging bytes are exactly a valid 21-byte copy of the (identical) prefix at distance 72;
the reference token stream says hardware emitted a 12-byte copy at distance 24 there. 15 bits vs
18 bits of Huffman budget cannot both be true inside one dynamic block that never re-syncs, yet
the bytes DO re-sync."*

The rewrite finding removes it: **the two token streams are not in the same pass.**

- Pass 1 matches hardware/zlib exactly for the whole blob (MIS=1 is at w=6,120,817, i.e. after a
  full pass; a single pass reaches offset 0xD521C at w=872,989). Pass 1 emitted the reference
  token at 0xD521C: the 12-byte copy at distance 24, producing 0x7C.
- A later sweep emits a 21-byte copy at distance 72, producing 0x2D. The 80 captured MIS rows
  show it byte by byte: `t1`/`s1` both +1 per store, `s1 = t1 - 0x48` (distance 72),
  `s3 = 0x15` (21) on entry then `s3 = 0x4`, values `2d 30 00 00 ...`.

There was never a 15-bit-vs-18-bit conflict inside one dynamic block. There are **two decodes**.
The contradiction was produced by comparing a pass-1 token budget against a pass-N token budget.

### Register state, constant across all 80 MIS rows (n=80, ref2.log)

```
t2 (inEnd) = 0x12bf0bd   constant    <- sentinel, base-1, "unbounded"
t4 (outEnd)= 0x12bf0ff   constant    <- sentinel, base-1, "unbounded"
t5 (winBase)= 0x0        constant    <- no window copy; direct path only
t3          = 0x0        constant
ra          = 0x1010a70  constant    <- same call site inside FUN_0100F8C8 every time
s0          = 0x1ffce10  constant    <- the CORE.GT4 job
t0 (inPtr)  0x110300c -> 0x1103028   +28 bytes across 88 output bytes
s1 (src)    0x13943d8 -> 0x1394402   +1 per store, = dst - 0x48
```

**Both end pointers are sentinels (base-1 = "unbounded").** Nothing in the core can terminate a
sweep on an end-of-buffer condition. That is the structural reason a second sweep is possible at
all, and it makes the driver (`FUN_0100F8C8`) the prime suspect: only the driver decides to run
again, and the core has no way to refuse.

Also note `ra = 0x1010a70` on every MIS row — the core is (re)entered from the SAME call site,
so this is a driver-level re-entry, not a stray jump.

## Track A — the block table, and the damage is MID-BLOCK (measurer/oracle, 2026-10-07T21:55+03:00)

Tool: `/mnt/ssd/tmp/w252meas/blocks.py` (pure-python raw-DEFLATE block walker, wbits=-15, no zlib).
Input: `CORE.GT4[6:]`. Runtime 3.3 s. Output is byte-exact against `zlib.decompress(..., -15)`.

### F1 — the reference stream is 43 blocks, ALL dynamic, last one BFINAL
```
blocks=43  total_out=0x5D5ECC (6,119,116)  match_zlib=True
blk1  startbit=0         BFINAL=0 BTYPE=DYNAMIC out 0x0..0x22595
blk2  startbit=389561    BFINAL=0 BTYPE=DYNAMIC out 0x22595..0x5CA24
blk3  startbit=791273    BFINAL=0 BTYPE=DYNAMIC out 0x5CA24..0xAE8A1
blk4  startbit=1206770   BFINAL=0 BTYPE=DYNAMIC out 0xAE8A1..0xD4003
blk5  startbit=1596742   BFINAL=0 BTYPE=DYNAMIC out 0xD4003..0x109C83   <-- 220,288 B
blk6  startbit=2010452   BFINAL=0 BTYPE=DYNAMIC out 0x109C83..0x136BBE
...   (blk7..blk37 omitted, all DYNAMIC BFINAL=0)
blk38 startbit=14483705  BFINAL=0 BTYPE=DYNAMIC out 0x50E869..0x54E0D0
blk39 startbit=14819494  BFINAL=0 BTYPE=DYNAMIC out 0x54E0D0..0x591799
blk40 startbit=15179812  BFINAL=0 BTYPE=DYNAMIC out 0x591799..0x5B1CA9
blk41 startbit=15562495  BFINAL=0 BTYPE=DYNAMIC out 0x5B1CA9..0x5BB4EC
blk42 startbit=15825067  BFINAL=0 BTYPE=DYNAMIC out 0x5BB4EC..0x5D403A
blk43 startbit=16145495  BFINAL=1 BTYPE=DYNAMIC out 0x5D403A..0x5D5ECC
```
**This falsifies the long-standing premise "one dynamic block that never re-syncs."** There is no
single block; there are 43, and the stream terminates exactly where zlib says it does.

### F2 — the damage window lies ENTIRELY INSIDE block 5, mid-block
All offsets DECODE space (`= blob_offset + 0x104`); blk5 spans `0xD4003..0x109C83`.

| event | decode offset | offset into blk5 |
|---|---|---|
| blk5 start | `0xD4003` | `+0x0` |
| **first divergence** | `0xD5320` | **`+0x131D`** |
| **second-sweep restart** | `0xD53D7` | **`+0x13D4`** |
| damage end (re-converged) | `0xD81B5` | `+0x41B2` |
| blk5 end | `0x109C83` | `+0x22000` |

- Damage = `0xD81B5 − 0xD5320` = **11,925 of blk5's 220,288 bytes = 5.41 %**.
- Enter/exit are `0x131D` and `0x41B2` bytes in from the block start/…: **neither endpoint is a
  block boundary.** blk6 begins at `0x109C83`, which is 0x38732 bytes past the re-convergence.
- **Restart − first-divergence = 183 bytes.** The rewind is LOCAL to the same block, 183 output
  bytes downstream of the first wrong byte — not a jump to a block header.
- ⇒ The damage is **not** a block-loop artifact, **not** a boundary misalignment. It is a
  mid-block event inside one core invocation.

### F3 — pass 1 stops 6,750 bytes into the FINAL block
`0x5D5A98` (blob) = `0x5D5B9C` (decode). blk43 = `0x5D413E..0x5D5FD0` (7,826 B). Pass 1 therefore
stopped **86.2 % through the last block, 816 bytes short of the true output end** — then the store
pointer went back to decode `0xD53D7` (blk5) and re-walked forward. First measured store after the
turn lands at decode `0xD5320`, 183 bytes BEFORE the restart point.

### F4 — the driver IS a block loop, verbatim (Ghidra, `FUN_0100F8C8`)
```
01010a68:  jal 0x0100f390        ; <-- core inflate; ra = 0x01010a70 (matches every MIS row)
01010a6c:  (delay slot)
01010a70:  lw s3,0x0(s0)         ; reload core state from job after the call
01010a74:  lw s2,0x8(s0)
01010a78:  lw s4,0xc(s0)
01010a7c:  lw s6,0x10(s0)
01010a80:  lw a1,0x14(s0)
01010a84:  lw t4,0x18(s0)
01010a88:  lw s7,0x20(s0)
01010a8c:  lw s8,0x24(s0)
01010a90:  lw a0,0x588(sp)       ; <-- BFINAL, stored by the core at 0x0100fdc8
01010a94:  beq a0,zero,0x0100fdb4 ; BFINAL==0 -> read NEXT block header, re-enter core
01010a98:  andi v0,s3,0x1        ; (delay slot, runs on BOTH paths: BFINAL read for next blk)
01010a9c:  lw v0,0x44(s0)        ; BFINAL!=0 -> vtbl+0x18 = output flush, then epilogue
01010aa8:  addiu v0,v0,0x18
01010ac8:  jalr v1
01010af8:  jr ra                 ; 01010afc addiu sp,sp,0x650
```
Block-header reader inside the core (verbatim):
```
0100fba0:  lw v1,0x580(sp)       ; CMF/flags byte
0100fba4:  andi v0,v1,0x8
0100fba8:  beq v0,zero,0x0100fdb4 ; FDICT==0 -> straight to block header
0100fdb0:  andi v0,s3,0x1        ; BFINAL bit
0100fdb4:  dsrl s3,s3,0x1
0100fdc8:  sw v0,0x588(sp)       ; <-- BFINAL saved here
0100fe38:  andi v0,s3,0x3        ; BTYPE
0100fec0:  li v0,0x2
0100fec4:  bne s1,v0,0x01010620  ; BTYPE != DYNAMIC -> stored/static path
0100fecc:  daddiu v0,v0,0x101    ; HLIT = 5 bits + 257
0100fed4:  sw v0,0x594(sp)
```

### What this kills / what it leaves
- **Kills**: "one dynamic block that never re-syncs" (43 blocks, F1); "the driver re-entered the
  core at a block boundary" (F2 — restart is mid-block, 183 B after the first bad byte);
  "the flush callback outputs garbage" (F4 — flush only runs when BFINAL, once, at the very end).
- **Leaves**: a mid-block, single-invocation event. The core, inside ONE call to
  `FUN_0100F390` for block 5, produced 183 bytes of correct output, then a wrong byte, then
  rewound its store pointer by ~5.0 MB and re-walked forward, re-converging 11,925 bytes later.
  A DEFLATE match cannot move the output pointer backward — distance is capped at 32,768 and
  blk5's restart is 4,999,589 bytes back. So the rewind is a **restore of the out pointer from
  memory**, not a match copy.

### NEXT MEASUREMENT (one relink, decisive)
Capture, per store event, BOTH pointers: input pointer (s4 / job `+0x0c`) and output pointer
(`t1`). One relink, one boot.
- inPtr advances monotonically while dst jumps back ⇒ the core's own out-pointer bookkeeping is
  at fault (job `+0x20` reloaded stale) — fix target is the core.
- inPtr ALSO jumps back at MIS=1 ⇒ something re-enters the decode with a saved checkpoint;
  fix target is the driver / flush path.
This is the exact fork the previous NEXT MEASUREMENT named; the block table above now says the
answer is inside one invocation, so the fork is between the core's `+0x20` reload and a caller
checkpoint.

---

## W252 Track A · builder-1 · 2026-10-07 · PASS 1 IS BYTE-PERFECT TO THE LAST BYTE, THEN THE DRIVER LOOPS

All numbers below are from one boot each, measured, with paths. Instrument: `VULCAN4_W252_WIN=LO:HI`
(prints every in-blob store whose ordinal is in `[LO,HI]`, uncapped, with the previous destination
so a rewind is one line) and `VULCAN4_W252_CKW=LO:HI` (the same ordinal window read at the
scheduler checkpoint). Both OFF by default, both in
`tools/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`.

### 1. The whole blob is decoded correctly, then overwritten

`[w252:copy] STAT` lines in `/mnt/ssd/tmp/w252-b1/refA.log` (12,343 samples, w=500..6,172,000):

```
w=500      mis=0  lastdst=0x12bf3f7 (blob 0x1f3)
w=5645000  mis=0  lastdst=0x1821c9c (blob 0x561a98)
w=5912000  mis=0  lastdst=0x186201c (blob 0x5a2e18)
w=6120500  mis=0  lastdst=0x1894e90 (blob 0x5d5c8c)
w=6121000  mis=170 lastdst=0x13944d7 (blob 0xd52d3)
```

`mis` is 0 at every sample from w=500 to w=5,912,000 — i.e. over the whole 6,118,856-byte blob.
The first store that differs from the hardware blob happens only **after** the decoder has already
produced the complete image:

```
w=6120815 pc=0x100f800 blob=0x5d5dc7 dst=0x1894fcb t0=0x12bf0c0   <- blob END (len 0x5D5DC8)
w=6120816 pc=0x100f800 blob=0xd521b  dst=0x139441f REWIND t0=0x110300c a2=0x19 a3=0x0fa276c6
w=6120817 pc=0x100f800 blob=0xd521c  dst=0x1394420   FIRST MIS=1 got=0x2d exp=0x7c
```

So the blob is written correctly **in full**, and then a later pass rewinds to blob `0xD521B` and
rewrites it wrong. Nothing earlier is wrong. "Fix the earliest divergence" therefore means
"stop the second pass", not "fix a decode".

### 2. It is not one rewind — it is a sawtooth

12 STAT-visible decreases (lower bound creeps forward, upper bound is pinned at the damage-window end):

```
lastdst blob 0x5d5c8c -> 0xd52d3   mis 0    -> 170
blob 0xd58af -> 0xd52cc            mis 1565 -> 2026
blob 0xd7fb8 -> 0xd5aec            mis 12993-> 13466
blob 0xd60c8 -> 0xd5ae5            mis 14899-> 15376
blob 0xd8001 -> 0xd630c            mis 24481-> 24953
blob 0xd68e8 -> 0xd6302            mis 26377-> 26854
blob 0xd804e -> 0xd6b33            mis 34048-> 34533
blob 0xd710f -> 0xd6b1f            mis 35976-> 36463
blob 0xd809b -> 0xd7364            mis 41747-> 42232
blob 0xd7940 -> 0xd7365            mis 43678-> 44161
blob 0xd7f1d -> 0xd79b5            mis 47038-> 47509
```

Lower bounds: `0xd52cc 0xd5aec 0xd630c 0xd6b33 0xd7364 0xd79b5`. Upper bounds cluster at
`0xd7fb8..0xd809b`, i.e. at `0xD80B1` — the end of the measured damage window. The earlier harmless
decrease at w=873000 (`blob 0xd5033 -> 0xd4a80`, mis unchanged at 0) shows the same mechanism
operating **inside** pass 1 without producing an error.

### 3. NOT a DEFLATE block boundary — measured with an independent scanner

`/mnt/ssd/tmp/w252-b1/blocks.py` is a from-scratch bit-level raw-DEFLATE block scanner, validated
against python `zlib` on the same bytes (`match=True`, `final_out=6,119,116`, `final_bitpos` consumes
2,020,855 of the 2,020,855-byte body; `/mnt/ssd/tmp/w252-b1/zlibout.bin` is byte-identical to the
hardware blob at every offset, `ndiff=0`).

43 blocks, all `btype=2`, `bfinal=1` on #43. The block containing the damage:

```
#5  out=[0xD4003,0x109C83)  blob=[0xD3EFF,0x109B7F)  len=220288
```

`0xD521C` (first divergence) and `0xD80B1` (last) are **both strictly inside block 5**. Neither is a
block boundary. The nearest boundaries are `blob 0xD3EFF` (block 5 start) and `blob 0x109B7F`
(block 5 end). The set of rewind targets `0xD52CC..0xD79B5` is likewise all interior to block 5.
**Block-boundary handling is exonerated.**

### 4. The stream is exhausted and the decode is complete

At the last correct store, `t0=0x12bf0c0` vs `t2=0x12bf0bd` (`inEnd`): the input pointer has already
passed the end (`t0 = t2 + 3`, the last partial word). The scanner independently confirms the entire
2,020,855-byte compressed body was consumed and every block closed. The driver loops anyway, and the
second pass begins with `t0=0x110300c` — 202,060 bytes into the compressed body (10.0%), a different
address entirely, not a re-read of the same position.

### 5. NOYIELD is not a usable A/B — it starves

`VULCAN4_W229_NOYIELD=1` (suppress checkpoints while `pc` is in `[0x100f390,0x100f8c8)`) gives
`exit=124` (timeout at 200 s) and `NO BLOB DUMPED`
(`/mnt/ssd/tmp/w252-b1/abNoyield.txt`). Without yields the decoder never returns to its caller, so
the rewind loop is **unbounded** and the run cannot reach the dump site. That is evidence the loop
is a real infinite loop in guest terms, and it means this switch cannot separate yield from decode.
The `VULCAN4_W229_NOCOPY` arm (`[0x100f7f0,0x100f870)`) is running.

### 6. Reproduction is stable (unchanged from the previous record)

`winA`: `blob len=6118856 ref=6118856 ndiff=11365 first=0xD521C last=0xD80B1`, `exit=0`.

### Instrument caveats

- `VULCAN4_TRACE=1` sets `traceFirst=1`, not `traceAll` — it cannot be used to see the decoder.
- The `[w127:copyloop]` and `[w229:huf]` probes are keyed on `targetPc` in `dispatchGuestBranch`
  and are therefore blind to resumptions, which is where this bug lives.
- The blob dump is gated on `targetPc == 0x01003700` reaching `dispatchGuestBranch` with
  `VULCAN4_W229_STREAM` set; it is the correct 6,118,856-byte artefact (`len=0x5D5DC8`) but shares
  the same blind spot.

### NEXT

`[w252:ck]` + `[w252:win]` on the same ordinal window (`VULCAN4_W252_WIN=6120780:6120860
VULCAN4_W252_CKW=6120780:6120860`) names whether a due checkpoint — and therefore
`servicePendingEventsAtCheckpoint()`, which runs guest code — sits between the last correct store
and the first wrong one. Second boot in the same batch logs the w=873000 benign rewind window.

## Track A — MECHANISM FOUND: the flush callback is a full-state restore (measurer/oracle, 2026-10-07T22:02+03:00)

Source: Ghidra decompile + verbatim disassembly of `FUN_0100F390` (the inflate core,
`0x0100F390..0x0100F8C8`, 287 instructions). Register map confirmed on the normal return path
(`0x0100f878`: `sw t4,0x24(s0)` then `sw t1,0x20(s0)` ⇒ **t1 = out pointer, t4 = out-end**).

### The two copy loops, verbatim
LITERAL path (store at `0x0100F4EC`):
```
0100f4ec:  sb s3,0x0(t1)        ; STORE literal byte
0100f4f0:  beq t5,zero,0x0100f508 ; window base t5==0 -> no ring update (MATCHES t5=0 in our dump)
0100f4f4:  addiu t1,t1,0x1        ; out++
0100f508:  bne t1,t4,0x0100f418   ; out != out-end -> main loop
; --- out == out-end: FLUSH ---
0100f510:  lw v0,0x44(s0)         ; vtbl
0100f514:  sd a3,0x0(s0)          ; save bitbuf   -> job+0x00
0100f518:  sw a2,0x8(s0)          ; save bitcnt   -> job+0x08
0100f51c:  addiu v0,v0,0x18
0100f520:  sw t0,0xc(s0)          ; save inPtr    -> job+0x0c
0100f524:  sw t2,0x10(s0)         ; save in-end   -> job+0x10
0100f528:  sw t3,0x18(s0)         ; save win idx  -> job+0x18
0100f52c:  sw t4,0x24(s0)         ; out-end := t4
0100f530:  sw t4,0x20(s0)         ; *** job+0x20 (OUT) := t4 (OUT-END) ***
0100f534:  lh a0,0x0(v0)          ; argOffset = *(short*)(vtbl+0x18)
0100f538:  lw v1,0x4(v0)          ; fn        = *(vtbl+0x1c)
0100f53c:  jalr v1                ; *** FLUSH CALLBACK ***
0100f540:  addu a0,s0,a0          ; arg = job + argOffset
0100f544:  ld a3,0x0(s0)          ; reload bitbuf
0100f548:  lw a2,0x8(s0)          ; reload bitcnt
0100f54c:  lw t0,0xc(s0)          ; reload inPtr
0100f550:  lw t2,0x10(s0)         ; reload in-end
0100f554:  lw t5,0x14(s0)         ; reload window base
0100f558:  lw t3,0x18(s0)         ; reload win idx
0100f55c:  lw t1,0x20(s0)         ; *** RELOAD OUT FROM job+0x20 ***
0100f560:  b 0x0100f418           ; main loop
0100f564:  lw t4,0x24(s0)         ; (delay) reload out-end
```
MATCH path (store at `0x0100F800` — the pc on 67 of the 80 MIS rows):
```
0100f7f8:  addu s2,s1,s3          ; s2 = src + length (match end)  <-- PRESERVED across the flush
0100f7fc:  lbu v0,0x0(s1)
0100f800:  sb v0,0x0(t1)          ; STORE match byte
0100f804:  addiu t1,t1,0x1        ; out++
0100f808:  bne t1,t4,0x0100f864   ; out != out-end -> continue
0100f80c:  addiu s1,s1,0x1        ; (delay)
; --- out == out-end: FLUSH (identical save/call/reload sequence) ---
0100f810:  lw v0,0x44(s0)  ..
0100f830:  sw t4,0x20(s0)         ; *** job+0x20 (OUT) := t4 (OUT-END) ***
0100f83c:  jalr v1                ; *** FLUSH CALLBACK (vtbl+0x1c) ***
0100f844:  ld a3,0x0(s0)  .. 0100f858: lw t3,0x18(s0)
0100f85c:  lw t1,0x20(s0)         ; *** RELOAD OUT FROM job+0x20 ***
0100f860:  lw t4,0x24(s0)         ; reload out-end
0100f864:  bnel s1,s2,0x0100f800  ; loop the match
```

### What this means — the divergence is a CALLBACK-CAUSED STATE RESTORE
Both flush sites are a **full-state save → external callback → full-state restore**:
`bitbuf, bitcnt, inPtr, in-end, window base, window idx, out, out-end` all round-trip through the
job struct at `0x1ffce10`, addresses `+0x00,+0x08,+0x0c,+0x10,+0x14,+0x18,+0x20,+0x24`.

The core **unconditionally trusts `job+0x20` after the callback** (`0x0100f55c` / `0x0100f85c`).
So the flush callback is a **state-injection point**: whatever our runtime writes to `job+0x00…
+0x24` becomes the decoder's new (bitbuf, bitcnt, inPtr, out) — silently, mid-invocation, with no
validation. The job is `0x1ffce10`; `vtbl = job+0x44`; the flush fn ptr is `*(vtbl+0x1c)` and its
argument is `job + *(short*)(vtbl+0x18)`.

This explains every measurement we already had:
- **MIS pcs are only `0x100f800` (67) and `0x100f4ec` (13)** — the two store sites *downstream* of
  a reload. Wrong bytes appear at the store, but the wrong decision was made at the reload.
- **ra = `0x1010a70` on all 80 rows, one core invocation** — the whole event is inside one
  `jal 0x0100f390`, consistent with a mid-invocation callback.
- **183 bytes of correct output before the first bad byte** — a restored bitbuf/bitcnt resumes the
  bit stream at a *nearby but not identical* position; wrong tokens only surface once the stale
  bits are consumed.
- **wrong token = 21 B @ distance 0x48 vs hardware's 12 B @ distance 0x18** — a few bits of stream
  offset changes both LB/LE and DB/DE of the same token.
- **the ~5 MB backward "rewind"** — a DEFLATE match cannot move out backward (cap 32,768); a
  `job+0x20` write from the callback can move it anywhere.

### Correction to the earlier record
The vtable slot for the output flush is **`vtbl+0x1c`** (function pointer), with its signed 16-bit
argument offset at **`vtbl+0x18`**. Earlier notes said "`+0x18` output flush" — that is the argument
offset, not the function pointer. Input refill is symmetrically `vtbl+0x14` (fn) / `vtbl+0x10`
(argument offset). Both callbacks are called as `fn(job + argOffset)`.

Also note: the "sentinel" reading of `t2`/`t4` is now explained. Both flush sites write
`job+0x20 := t4`; if the callback leaves it alone, `t1` becomes `t4` and the out pointer is
permanently out-end. The measured `t4 = 0x12bf0ff` and `t2 = 0x12bf0bd` sit *before* the blob base
`0x12bf204` (by `0x105` and `0x147`), so a forward-walking `t1` can never equal them — **on the
hardware side no flush ever fires.** Exactly one side firing that callback is all it takes.

### NEXT MEASUREMENT (one relink, decisive — A/B, no hardware needed)
Instrument `vtbl+0x1c` at `0x0100f83c` / `0x0100f53c`: log (a) entry count, (b) `job+0x20` and
`job+0x24` before and after the callback, (c) `bitbuf`/`bitcnt`/`inPtr` before and after.
Then run the SAME probe with the flush callback forced to a no-op that leaves `job+0x20` alone.
- damage vanishes with the callback neutralised ⇒ the callback is the whole cause; fix is in the
  runtime's flush implementation.
- damage persists ⇒ the callback is not reached in our run either, and the desync is upstream in
  the Huffman/bit-read path (then bisect on `t0`/`a3`/`a2` at `0x0100f418`).

## Track A — RETRACTION + the real shape of the divergence (measurer/oracle, 2026-10-07T21:58+03:00)

**The previous section ("MECHANISM FOUND: the flush callback is a full-state restore") is RETRACTED.**
The disassembly in it is correct; the conclusion is not. The flush block is **unreachable in this
job**, so it cannot be the cause. Evidence, all from `/mnt/ssd/tmp/w252meas/ref2.log`:

| fact | value | source |
|---|---|---|
| `t4` (out-end) over 80 MIS rows | `0x12bf0ff`, **constant, 80/80** | `grep -o 't4=0x[0-9a-f]*'` |
| `t1` (out) over the same rows | `0x1394420` … rising | MIS=1..10 |
| store pcs ever logged | `0x100f4ec`×14 (literal), `0x100f800`×67 (match) | `grep -o 'pc=0x100f...'` |
| flush-block pcs (`0x100f81*`,`0x100f51*`) | **0 hits** | `grep -cE 'pc=0x100f(81\|53\|55\|83\|85)'` |

`0x12bf0ff = 0x12bf100 − 1` = **decode-buffer base − 1**. `t1 ≥ 0x12bf204` always, so `bne t1,t4` at
`0x0100f808` is always taken and the flush block at `0x0100f80c` / `0x0100f510` never executes.

Two corrections to earlier notes, both from the same log:
- `t2` is **not a sentinel**. `0x12bf0bd` is exactly the **end of the compressed CORE.GT4 data**:
  input buffer `0x10d1ac0` + 2,020,861 (`0x1ED5FD`) = `0x12BF0BD`. It is a real bound — but `t0`
  (`0x110300c`) never reaches it while MIS rows are being logged, so the refill at `vtbl+0x14` also
  never fires during the damage.
- `t5` (window base) = `0x0` on 80/80 rows ⇒ the ring-buffer store `sb s3,0x0(v0)` at `0x0100f500`
  is dead code here; matches read straight back out of the output buffer (MIS rows: `src=0x13943d8`,
  `out=0x1394420`, dist `0x48`).

Buffer layout, now exact and self-consistent:
`compressed in [0x10d1ac0, 0x12bf0bd)` 2,020,861 B · `decode buf [0x12bf100, 0x1894fcc)` 6,119,116 B
(memset, ref2.log:1156) · `ref data at 0x12bf204 = buf+0x104`, 6,118,856 B = `VULCAN4_W252_REF` armed
range (ref2.log:70–71) · `out-end = buf−1` (unreachable).

### The real shape: not a mid-stream desync — a full correct decode, then a bogus REWRITE of one band

From the 6,172 `STAT` rows in ref2.log:

- `w=1000 → lastdst off = 0x3E7 = w−1` — stores are **sequential from offset 0**.
- `mis` stays **0** through `w=6,120,000`, `off=0x5D5A98` = **6,118,040 = 816 bytes short of the whole
  6,118,856-byte output** — i.e. **our decoder produced the entire image correctly.**
- Store **#6,120,817** lands at off `0xD521C` (= blob `0xD5320`) with the wrong byte — MIS=1,
  `got=0x2d exp=0x7c`, `pc=0x100f800`, `ra=0x1010a70`. That is the rewind: 6,120,816 → 872,988 =
  **−5,247,828 bytes**.
- Every one of the remaining 52 sampled rows sits inside `[0xD52CC, 0xD7F91]` — an **11,461-byte
  band** — with `mis` climbing 170 → 48,944. The run **ends inside the band** (last STAT
  `w=6,172,000 mis=48,944 off=0xD7F91`).

So: hardware emits 6,119,116 correct bytes and stops. Ours emits the whole thing correctly, then
**re-enters the copy loop with out ≈ buf+0xD521C and spins re-writing one 11.5 KB band until the
harness deadline cuts it.** That is a livelock, not a desync, and it is consistent with the
`stuck_in_syscall` / `wallclock_deadline` halt.

KILLED by this measurement: (a) the flush callback — unreachable, table above; (b) the refill
callback — `t0` < `t2` throughout; (c) "a mid-block desync knocks the bit reader out of sync" — the
first 6,120,816 stores are byte-perfect.

### Instrument blind spot (name it before chasing it)

`ra = 0x1010a70` on **80/80** MIS rows. That is the single driver call site
(`0x01010a68 jal 0x0100f390`), so **`ra` cannot distinguish the 43 per-block core entries, nor any
extra re-entry.** Any "we only entered the core 43 times" reasoning is unfounded until a call ordinal
exists. NEXT: add a counter incremented at core entry (`0x0100f418` prologue) and at the copy-loop
head, and log `(ordinal, t1, t0, a3, a2)` at the first mismatch — that settles whether the rewind is a
fresh entry (ordinal +1 with `job+0x20` lowered) or an intra-call pointer restore.

## W252 Track A · builder-1 · 2026-10-07 · INSTRUMENT CORRECTIONS + the engine image is never written

### A1. Two probe parameters were fed the wrong kind of number (measurement void, not a result)
`ps2_runtime.cpp:3795-3830` parses **`VULCAN4_W252_WIN` and `VULCAN4_W252_CKW` as DECIMAL**
(`strtoul(...,10)`) while `LO`/`HI`/`REFBASE` are **hex** (`strtoul(...,16)`).
Run `p1B` passed the *store-ordinal* value `874700` to the **address** window `WIN` →
`win=0 ck=0`, i.e. it measured nothing. Blob `0xD521C` is guest address
`0x12BF204 + 0xD521C = 0x1394420 = 20545568` decimal. Corrected run is `p1BB`
(`WIN=20545568:20571829` = blob `[0xD521C, 0xD80B1]`, both passes' writes to the damaged range).

### A2. `w=0 .. w=6120860` is the GLOBAL store ordinal; the CK probe's registers are NOT stale
Re-reading ckA's `[w252:win]` lines shows the printed register file is coherent at every store
(`t0` tracks the in-ptr, `s1`/`s2` track the match-copy source, `ra=0x1010a70` always). The
"frozen registers" seen in the `[w252:ck]` lines are the fields the loop **reloads from the struct
each iteration** (`ld a3,0x0(s0)`, `lw a2,0x8`, `lw t0,0xc`, `lw t2,0x10`, `lw t1,0x20`) and never
writes back — frozen is correct there, not a probe artefact. End of pass 1 measured at the store
(`ckA w=6120780..6120781`): `t0=0x12bf0b4/12bf0b8` against `inEnd t2=0x12bf0bd` — i.e. **pass 1
really does consume the input to its end.**

### A3. THE ENGINE IMAGE IS NEVER WRITTEN — `win=0`
`./winrun.sh engineA VULCAN4_W252_COPY=1 VULCAN4_W252_WIN=1048576:6384660`
(= exactly `0x00100000..0x00617A14`) → `exit=0 tag=engineA win=0 ck=0`.
**Not one store in the whole run lands anywhere in the engine image range.** The decode→blob
path writes only `0x12BF204+` (and the loader's own struct at `0x1FFCE10`); the copy that is
supposed to place the engine at `0x00100000` is never reached at all. Consistent with
`vulcan4_harness.cpp:2706` ("GT4's loader passes entry=0x100008 (low RDRAM, the 0x100000 page)")
and with W251's "no engine address is ever a control-flow target".

### A4. The decoder has TWO exits, and the "stop" one is `s2 == 15`
`FUN_0100F390` disassembly (`0x0100f390..0x0100f8c8`):
- `0x0100f86c: b 0x0100f418` — **unconditional**, loops the symbol-decode head. Not a return.
- `0x0100f56c: beq s2,v0,0x0100f878` with `li v0,0xf` at `0x0100f568` — the ONLY branch to the
  return epilogue (Ghidra: 1 xref to `0x0100f878`, CONDITIONAL_JUMP from `FUN_0100f390`).
  So **the decoder returns when `s2 == 15`**; `s2` is rebuilt at the top of the symbol loop and is
  the code-length/limit it compares against `s1` in `bnel s1,s2,0x0100f800`.
- The entry converts struct fields `+0x38`/`+0x3c` from **bit widths to masks in place**
  (`sllv v1,v0,s5; addiu s5,v1,-1; sw s5,0x38(s0)`), so those two fields are destroyed by every
  call and must be re-armed by the caller before the next one.

### A3-CORRECTION (same session, minutes later): A3 as written was WRONG — retracted
A3 claimed "the engine image is never written" from `engineA win=0`. That is a **void measurement**.
`vulcan4W252CopySub` (`ps2_runtime.cpp:2268-2290`) returns early unless ALL of:
`op == "WRITE8"` **and** `pc ∈ [0x100f390,0x100f8c8)` **and** `dst ∈ [g_w252Lo,g_w252Hi)`.
`WIN` is applied only *after* the `LO/HI` gate, so a `WIN` window outside `[LO,HI]` can never print
by construction. `engineA` set only `WIN=1048576:6384660` and left `LO/HI` at the blob default →
the intersection is empty → `win=0` was guaranteed before the run started. It says nothing about
low RDRAM. **A3 stands retracted; the question "is `0x00100000..0x00617A14` ever written?" is
STILL OPEN** and needs a probe that is not filtered by pc-in-decoder and not filtered to `WRITE8`
(a bulk copy into the engine image would be `sw`/`ld`, invisible to this subscriber).

Lesson (same class as W229 §16): before reading a zero, prove the instrument can produce a non-zero.

### Store-stream geometry, refined (same STAT rows)

`delta(w) = (lastdst − 0x12bf204) − (w−1)`:

| w | mis | lastdst off | delta | reading |
|---|---|---|---|---|
| 1000 … 872000 | 0 | `0x3E7 … 0xD4E3F` | **0** | one in-range store per output byte, strictly sequential |
| 873000 | 0 | `0xD4A80` | **−1959** | one **backward** move of 1,959 B inside this window |
| 874000 … 6168000 | 0 → 170 | rises by +1000/window | **−1959 (constant)** | climbs normally again; the 1,959 B is never made up |
| 6120000 | 0 | `0x5D5A98` = 6,118,040 | — | 816 B short of the whole 6,118,856-B image |
| 6121000+ | 170→48,944 | band `0xD52CC…0xD7F91` | −5,285,490 | **5.25 MB rewind**, then livelock in an 11,461-B band |

Two distinct anomalies, both now with numbers: (1) a **1,959-byte backward move** at w≈872,000–873,000
(decode off ≈`0xD4A80`, i.e. 2,685 B into blk5); (2) a **5,247,828-byte rewind** at w=6,120,817 to
off `0xD521C`, after which every store lands in the band. The 1,000-store STAT granularity is too
coarse to resolve either one further — **the probe must log per-store**, not per-1000.

### The zlib oracle is only valid below 0x5D5BCC

Hardware read at guest `0x1894C00` (refoff `0x5D59FC`) vs `ref.bin`: identical up to refoff
`0x5D5BCC`, then hardware holds **zero-fill plus a relocation table** — `fe ffffff 04000100` + absolute
pointers into the engine image (`0x5C1668`, `0x5C1780`, `0x5900D8`, … all inside
`0x00100000..0x00617A14`), repeated records ending `ffffffff`. That is the loader **post-processing
its own scratch buffer after decompression**, not a decompressor divergence: it starts at refoff
`0x5D5BCC` = 508 B from the buffer end, i.e. **later** than the first divergence, so by doctrine it is
a symptom, not the bug. But it bounds the oracle: **`ref.bin` must not be used as ground truth for
refoff ≥ `0x5D5BCC`.** Hardware at the band (`0x13944D0`, refoff `0xD52CC`) is byte-equal to `ref.bin`
(256/256) — the band is correct on hardware.

## Track A — the vtable is closed STATICALLY: refill and flush are `jr ra; nop` (measurer/oracle, 2026-10-07T22:20+03:00)

No more inference from registers. The three callers of the driver are named, the job is *identified*,
and both callbacks are disassembled.

### 1. `FUN_01010B10` is the job init — and it names the vtable

```c
void FUN_01010B10(int job) {          // decompiled; a0 = job
  FUN_0100ed08();                     // a0 still = job at the call (decompiler emits no arg)
  *(int *)(job + 0x28) = job + 0x48;
  *(undefined **)(job + 0x44) = &DAT_01036ac0;   // <<< the vtable
  return;
}
```

`DAT_01036ac0` read as bytes (`memory_read 0x01036ac0 64`), 32-bit LE words:

| off | value | meaning |
|---|---|---|
| +0x04 | `0x0102CDA8` | (unused by the core) |
| +0x0C | `0x01010BD0` | the fini |
| **+0x10** | **`0`** | input-refill argOffset |
| **+0x14** | **`0x01010B00`** | **input-refill fn** |
| **+0x18** | **`0`** | output-flush argOffset |
| **+0x1C** | **`0x01010B08`** | **output-flush fn** |
| +0x2C | `0x0102CD68` | |
| +0x34 | `0x0100ED28` | |
| +0x3C | `0x01010B00` | |

Because argOffset is **0**, the core calls `fn(job + 0)` — `fn(job)`, not `fn(job+0x48)`.

### 2. Both callbacks are four bytes of nothing

`memory_read 0x01010B00 32` → `08 00 E0 03 00 00 00 00 | 08 00 E0 03 00 00 00 00 | F0 FF BD 27 ...`

```
0x01010B00:  jr ra ; nop      <- REFILL callback
0x01010B08:  jr ra ; nop      <- FLUSH  callback
0x01010B10:  addiu sp,sp,-0x10   <- the init above (confirms the 2 x 8-byte split)
```

**Both hooks are empty stubs.** They cannot read, write, restore, or corrupt anything.

Consequence, from the static side and independent of every register argument in the retraction:
the "flush callback is a full-state restore" mechanism is not merely unobserved — it is
**arithmetically impossible**. There is no state to restore; the callee body is `jr ra`.

### 3. The second table is the same pair

`FUN_0100ED08(job)` (also decompiled) sets `job+0x14=0`, `job+0x40=0`, `job+0x28=0`, and
`job+0x44 = &DAT_01036AE8`. `0x01036AE8 = 0x01036AC0 + 0x28` — i.e. the *idle* table is a window
into the same blob, and its callbacks are the identical pair: `+0x14 = 0x01010B00`,
`+0x1C = 0x01010B08` (verified at `0x01036AFC` and `0x01036B04`). `FUN_01010BD0(job, flags)` is the
fini: it re-points `job+0x44` at `0x01036AC0`, calls `FUN_0100ED28(job, 0)`, and, if `flags&1`,
`FUN_0101D430(job)`. The loader calls it as `FUN_01010BD0(auStack_3070, 2)`.

### 4. Our job is now IDENTIFIED, not assumed

`FUN_0100ED78(job, in, len)` is the input setter, and it is exact:

```c
void FUN_0100ED78(ulong *job, uint in, int len) {
  uint a = in & 3;
  uint w = *(uint *)(in - a);           // the aligned containing word
  job[2] = in + len;                    // job+0x10 = in-end
  job[1] = a * -8;                      // job+0x08 = bitcnt  = -(in & 3) * 8
  *(uint **)((int)job + 0xc) = (in - a) + 1;   // job+0x0c = inPtr = align4(in) + 1
  *job = w >> (a * 8);                  // job+0x00 = bitbuf primed from the aligned word
}
```

The CORE.GT4 caller `FUN_01004308` invokes it as

```c
FUN_0100ED78(auStack_3070, *(int *)*param_1 + 6, ((int *)*param_1)[1] + -6);
```

— `input = file+6`, `len = size-6`, i.e. the container's 6-byte header is skipped exactly as the
`01 01 CC 5E 5D 00` header requires. Predicted in-end = `0x10d1ac0 + 0x1ED5FD` = **`0x12BF0BD`**,
which is **byte-identical to the `t2` in `ref2.log` (80/80 MIS rows)**. This is not a resemblance;
it is the same value derived from static code and from the live job struct.

### 5. `out-end = base - 1` is written by the CALLER, in all three callers

`FUN_01004308` (our job) sets, immediately before `FUN_0100F8C8(job)`:

```c
iStack_304c = iVar3 + -1;   // job+0x24  out-end  = base - 1
iStack_3054 = iVar3;        // job+0x1C  = base
iStack_3050 = iVar3;        // job+0x20  out      = base
```

and the other two callers of the driver do the same thing with their own argument:

```c
// FUN_01010B48 (0x01010bc8)                // FUN_0102CE98 (0x0102ced0)
FUN_0100ED08();                             (no ed08)
job->0x40 = param_4;                        job->0x40 = param_5;
job->0x44 = &DAT_01036AC0;                  FUN_0100ED78(job, param_3, param_4);
FUN_0100ED78(job, param_3, 0xFFFFFFFFFFFFFFFF);   // len = -1  => in-end = in - 1
job->0x1C = param_2;                        job->0x1c = param_2;
job->0x20 = param_2;      // out = base      job->0x20 = param_2;
job->0x24 = param_2 - 1;  // out-end = base-1 job->0x24 = param_2 - 1;
job->0x28 = job + 0x48;
job->0x14 = 0;
FUN_0100F8C8(job);                          FUN_0100F8C8(job);
```

**`out-end = base - 1` is house style — all three callers write it.** The `0x12bf0ff` we measured is
therefore exactly what the game's own loader asks for, and the flush path is unreachable **on
hardware too**, by construction. The retraction stands on three independent legs now: dynamic
(80/80 unreachable), static (caller writes the sentinel), and disassembly (`jr ra; nop`). The
three callers also kill the sentence "the driver runs once": there are three entry points, each
building its own job on its own stack frame.

### 6. One label left open (no conclusion depends on it)

`out` measured `0x12BF204`; the caller's formula gives `base = 0x12BF100 = measured out-end + 1`.
The two differ by exactly `0x104` = the same `0x104` by which the probe's reference blob is offset
into the decode buffer. Either the core advances `out` by a 260-byte stream header before the first
literal, or the probe armed `0x104` above the true base and the first 260 stores are uncounted.
**Every number in §5 of the 22:05 entry is a delta between two `lastdst` values, so both readings
give the same geometry**; only the absolute label of "decode offset 0" moves. Flagged, not resolved
— and not worth a probe, because the first-divergence address `0x1394420` is absolute and unaffected.

### 7. What this does and does not touch

Does **not** touch deliverables 1-4: hardware `0x7C` vs ours `0x2D` at blob `0xD521C` = guest
`0x1394420` = engine `0x001D51EC`, first disagreeing instruction `sb $v0,0($t1)` at pc `0x0100F800`
(`ra = 0x1010a70`, inside `FUN_0100F390`). Still the answer.
Does **not** touch the 22:05 geometry (delta `0` through decode `0x5D5A98`; one `-1959` step at
`w=873000`; a `-5,285,490` rewind at `w=6,120,817`; livelock in `[0xD52CC,0xD7F91]`).
**Does** remove the last alternative explanation for the rewind other than a genuine re-entry into
`FUN_0100F8C8` with a lowered `job+0x20` — which is what the core-entry ordinal probe is for.

NEXT MEASUREMENT (unchanged, now the only one left): core-entry ordinal at the `0x0100F418`
prologue + per-store `(w, ordinal, t1, t0, a3, a2, pc, byte)` around the `w=873000` move and the
`w=6,120,817` rewind. That single probe decides fresh-entry-with-lowered-`job+0x20` vs intra-call
pointer restore.

---

## Track A — MEASURER (oracle), session 2 · the core is CLEAN; the wall is the harness

Instrument note: **`pcsx2_disassemble` (DebugServer :21512, PCSX2's own native decoder) is
authoritative and reliable.** `ghydra memory_disassemble` returns a bare "Done" and
`ghydra functions_decompile` returns "Function not found" for functions reached only by dynamic
call — do not use ghydra for disassembly on this path.

### A1. `FUN_0100F390` decode verified CLEAN, exhaustively

| Check | Scope | Result |
|---|---|---|
| Independent word→opcode decoder vs emitted comments | 360/360 insns `0x0100F390..0x0100F8C8` | **0 real mismatches** |
| Mnemonic + operands vs PCSX2 native decode | 130/130 insns `0x0100F568..0x0100F760` | **match** |
| Branch-likely delay-slot gating | `beql 0x100F758`, `bnel 0x100F864`, `bgez 0x100F6E4` | **correct (taken path only)** |
| Vtable stub emission + registration | `0x01010B00`/`0x01010B08` = `jr ra; nop` | **correct, `register_functions.cpp:3353/3354`** |

The 14 rows my first pass flagged were my decoder's own gaps (`beqz`/`bnez`/`b` pseudo-names for
`beq`/`bne $zero`; opcodes `0x0b` = `sltiu`, `0x0c` = `andi`). Every branch *target* my decoder
computed agreed exactly with the recompiler's comment. Tool: `/mnt/ssd/tmp/w252meas/mipsdiff.py`.

Primitives on the divergence path, all correct in the generated C++:

```c
// 0x100f4c0  lwu $v0,0($t0)      SET_GPR_ZE32(ctx,2, READ32(...))            zero-extend ✓
// 0x100f6d0  lhu $a0,4($s1)      SET_GPR_ZE32(ctx,4,(uint16_t)READ16(...))  ✓
// 0x100f6dc  subu $a2,$a2,$s2    SET_GPR_S32(ctx,6, SUB32(...))             ✓
// 0x100f6e0  addu $s1,$a0,$v1    SET_GPR_S32(ctx,17, ADD32(...))            ✓
// 0x100f6e4  bgez $a2            (GPR_S64(ctx,6) >= 0)                      signed ✓
// 0x100f4c8  dsllv $v0,$v0,$a2   GPR_U64 << (GPR_U32 & 0x3F)                 ✓
```

**⇒ "our recompiler mis-decodes the core / mishandles branch-likely" is FALSIFIED, not merely
unproven. The remaining divergence space is DATA (the Huffman tables the core consumes) or a
RUNTIME PRIMITIVE — not the core's decode.**

### A2. wsize / windowed-path hypothesis FALSIFIED

I formed and then killed this in-session. `FUN_01010B10` sets only `job+0x28` and `job+0x44`; the
contrast loader `FUN_01010B48` explicitly writes `0x01010BB0: sw zero,0x14(s0)`. So `job+0x14`
(`t5`, the fast-vs-windowed selector) looked uninitialized on the CORE.GT4 path. The oracle kills it:

```
0x0100ED08: 3c020103  lui    v0,0x0103
0x0100ED0C: ac800014  sw     zero,0x14(a0)     <-- wsize IS zeroed, by the reset fn
0x0100ED10: 24426ae8  addiu  v0,0x6AE8
0x0100ED14: ac800040  sw     zero,0x40(a0)
0x0100ED18: ac820044  sw     v0,0x44(a0)       ; vtable = &DAT_01036AE8 (idle)
0x0100ED1C: 03e00008  jr     ra
0x0100ED20: ac800028  sw     zero,0x28(a0)     (delay)
```

`FUN_01010B10` `jal`s `FUN_0100ED08` at `0x01010B1C`, so `t5 == 0` on both loader paths ⇒ the core
takes the FAST copy path (`0x0100F758: beqzl t5 -> 0x0100F7F8`). Consistent with hardware ==
python zlib. **Same for the other loader: `0x01010BB0: sw zero,0x14(s0)`.**

### A3. The job-init contract, now exact (oracle bytes, not inference)

```
FUN_0100ED08(job)         +0x14=0  +0x40=0  +0x28=0  +0x44=&DAT_01036AE8
0x0100ED5C..0x0100ED74    +0x00(64)=0  +0x08=-0x20 (bits=-32)  +0x0C=0  +0x10=0   [stream start]
FUN_0100ED78(job,in,len)  job+0x0C = (in - (in&3)) + 4
                          job+0x10 = in + len
                          job+0x08 (bits) = -(in&3)*8        <-- NEGATIVE bit count
                          job+0x00 (hold,64) = lw(in-(in&3)) >> ((in&3)*8)
FUN_01010B10(job)         +0x28 = job+0x48 ; +0x44 = &DAT_01036AC0   (the CORE.GT4 path)
FUN_01010B48(job,..)      +0x40 ; +0x44 ; FUN_0100ED78(job,in,-1) ;
                          +0x1C = size ; +0x20 = size ; +0x24 = size-1 ;
                          +0x28 = job+0x48 ; +0x14 = 0 ; then `j 0x0100F8C8`
```

Init chain for CORE.GT4: `FUN_01000FD8` (set size) → `FUN_01010B10(job)` → `FUN_0100ED78(job,
file+6, size-6)` → `FUN_0100F8C8(job)`. The job is a **stack frame** (`auStack_3070` in
`FUN_01004308`), not a static struct — so it cannot be read out of a savestate.

**Sharp consequence:** the core's refill test is `bne t0,t2` — comparing the **aligned+4** `inPtr`
against the **unaligned** `in+len`. If `job+0x0C` were ever set to `in` instead of `align(in)+4`,
the refill fires one token early, `lwu` loads a different 32-bit word, and exactly one symbol goes
wrong while the bit position stays correct — which is the signature we measured (one contiguous
~11.4 KB wrong region that RE-MERGES). Candidate, unproven; only a run discriminates.

Also: the table bases the core *consumes* — `fp = job+0x30`, `sp+0 = job+0x34`, masks `s5 = job+0x38`,
`s7 = job+0x3c` — are written by the **driver `FUN_0100F8C8` (1,166 instructions, NOT yet verified
instruction-by-instruction)**. That driver is the largest unverified body on the path.

### A4. First divergence — unchanged and re-affirmed

| | value |
|---|---|
| hardware (truth) | **`0x7C`** |
| ours | **`0x2D`** |
| where | blob off `0xD521C` = guest `0x1394420` = engine `0x001D51EC` |
| first disagreeing instruction | **`0x0100F800: 0xa1220000  sb $v0,0($t1)`** (ra `0x1010A70`, inside `FUN_0100F390`) |

Hardware is the truth: `python zlib.decompress(CORE.GT4[6:], -15)` produces `0x7C` there, and PCSX2
DebugServer reads `0x7C` at guest `0x1394420`, byte-equal to `ref.bin`. Hardware copies 12 B at
distance `0x18`; ours 21 B at distance `0x48`. 11,365 differing bytes, span `0xD521C..0xD80B1`.
Digest at `0x1004748`: ours `0x893d7a82` vs game-expected `0x4bb5e6bf` → fail loop `0x01000638`.

### A5. BLOCKER — the instrument is gone

```
$ stat /mnt/ssd/vulcan4-build/run/vulcan4_harness
-rw-rw-r-- 1 or or 0 Oct  7 20:55 .../vulcan4_harness
$ sha256sum ...  e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
```

`e3b0c442…b7852b855` is the **empty-file** hash. `tasks.md` still records 347,669,656 B (W251
relink 20:16:19). Every remaining Track-A question needs a run; no run is possible. Per the standing
rule — *a breakpoint that never hits is evidence about the instrument first* — this goes on the
board as a blocker, not as a game finding.

**Builder ask (minimal, sharpened):**
1. relink `$B/run/vulcan4_harness`;
2. enable `VULCAN4_W203_PARSE` (`ps2_runtime.cpp:3396`) and **add `job+0x00` (hold), `job+0x08`
   (bits), `job+0x0C` (in) and `job+0x14` (wsize) to its core-entry line** — today it prints
   `[0x10][0x18][0x20][0x34][0x38][0x3c][0x44]` and **none of the four that matter have ever been
   logged by any probe**;
3. **add a `vulcan4W130StoreWatch` guard to the BYTE store at `0x100F800`** — the exact diverging
   instruction — it has no guard while every neighbouring `sw` does;
4. per-store log `(w, ordinal, t1, byte, pc)`.

**Retraction:** the "−959 correction" to the first rewind is itself the error. The original figure
**−1,959 output bytes** stands (single backward event inside block 5, cumulative delta −1,959 held
to w=6,120,000 with `mis=0`).

## Track A — MEASURER (oracle), session 3 · the first divergence is BIT-EXACT, and the whole inflate path is clean

### A6. First divergence, at symbol resolution
Destination is engine `0x001D51EC` = blob off `0xD521C` = ref.bin `0xD521C` = full-decode `0xD5320`.
- two values: hardware **0x7C**, ours **0x2D**.
- `0x7C = decode[0xD5308] = out[dest-24]`; `0x2D = decode[0xD5300] = out[dest-32]`. Hardware is the truth
  (python `zlib.decompress(CORE.GT4[6:], -15)` produces 0x7C there, and PCSX2 memory at guest 0x1394420 is byte-equal).
- Hardware's symbol pair, read from the bit stream: length symbol **265** (li=8, LB=11, LE=1, 1 extra bit = 1) => **L=12**;
  distance symbol **8** (DB=17, DE=3, 3 extra bits = 111) => **D=24**. Ours behaves as **L=21, D=72** (li=12, ds=11).
- The length symbol begins at **input bit 1,616,375** of `CORE.GT4[6:]`.
- First disagreeing instruction: the length/literal Huffman decode feeding the match store, whose store is
  `sb $v0, 0($t1)` at guest PC **0x0100F800** (word 0xa1220000), return address 0x1010a70, inside `FUN_0100F390`.
- Divergence is LOCALISED: first diff 0xD521C, **re-sync at 0xD80B2** (+11,926 B), 11,365 differing bytes. Not a shift, not a
  stream-length change (both blobs are 6,118,856 B; the decode is 6,119,116 B = 0x5D5ECC on both sides).

### A7. The entire inflate code path is a faithful translation — 2,014 instructions
Independent decoder (`mipsdiff2.py`, comment-parse + from-scratch decode, run per function):
- `FUN_0100F390` (core) **360/360** clean.
- `FUN_0100F8C8` (driver) **1,284/1,284**, 0 decoder gaps.
- `FUN_0100EDC8` (=`inflate_table`) **370/370** — every flag a pseudo-name (`b`, `beql`, `negu`) or a gap/naming bug in MY
  table (op 0x2C is `sdl`, I had it as `swr`; missing `movn`/`ldl`/`ldr`). The emitted C++ for 0x100F1B4 / 0x100F298 is correct
  64-bit unaligned `sdl` semantics.
- **Correction to A4/my board note:** the driver never stores job+0x30/0x34/0x38/0x3C. It passes `&job+0x34` and `&job+0x3C`
  as out-parameters to `FUN_0100EDC8` at 0x01010A34 / 0x01010A38, so the table builder writes them through the pointer.

### A8. The static tables are correct DATA in both worlds
`inflate_table` is handed `0x01033180` and `0x010331C0` (lui/addiu pairs at 0x01010A20 / 0x01010A30). Oracle memory at
0x01033180 = `01 00 02 00 03 00 04 00 05 00 07 00 09 00 0d 00 ...` = zlib's distance-base array (1,2,3,4,5,7,9,13,...24577),
byte-identical to `SCUS_973.28` at file offset 0x34180. `PS2Runtime::loadELF` copies every `PT_LOAD` segment into guest RAM,
so our runtime carries the same bytes. **Data is not the bug.**

### A9. Therefore (the redirect)
Code clean (2,014 insns), tables clean, input clean up to the divergence => the divergence is a **runtime primitive on the
symbol-decode path at input bit 1,616,375** — bit/hold accounting, not the inflate translation. Everything downstream
(the 12 B / dist 0x18 match at decode 0xD5308, the −1,959 window event) is a symptom.

---

## W253 — builder-spin: the four loader probes are IN the harness, and the first run already caught the rewind

Appended 2026-10-07 by builder-spin. Artifact: `/mnt/ssd/vulcan4-build/run/vulcan4_harness`,
**347,702,776 B**, mtime 22:23 — the FULL harness, not the 43,897,024 B loader-only partial the seat
before me left (that one had 1,107 `T sub_`; this one has **20,111 = 19,404 engine + 707 loader**).

### What was added (all OFF unless `VULCAN4_W253` is set)

Patch: `tools/patches/ps2recomp-linux-w253-loader-core-probes.patch` (711 insertions, 4 files).
Generated output was REGENERATED, never hand-edited: `ps2_recomp` rebuilt, then
`cd /mnt/ssd/gt4/work && ps2_recomp gt4_recomp.toml` → `$B/recomp/ps2_recompiled_functions.cpp`
(9,785,500 B). Diff against the pre-regen file is **exactly** the probes: 2 modified lines
(the `sb` at 0x0100F800 and the `dsll32` at 0x0100F418), 23 added jump-log lines, 1 core-enter line,
1 byte-store line.

- **(1) core-entry ordinal** — `vulcan4W253CoreEnter` prefixed to the instruction at **0x0100F418**
  (`dsll32 v0,a3,0`, the retry head of `FUN_0100F390`; five of that function's eight back-edges land
  on it). It IS a resume case in the switch, but the back-edges reach it by direct `goto`, so the
  counter has to live at the instruction.
- **(2) rewind log** — `vulcan4W253Jump` emitted at all **23** backward conditional branches in
  `[0x0100F390, 0x01011000)`; prints ordinal + pc→target + t1/t0/a3/a2.
- **(3) W203 parse fields** — `ps2_runtime.cpp` core-entry line now prints **[0xC] (job in ptr) and
  [0x14] (wsize)** first, the two that were missing.
- **(4) the invisible byte store** — `vulcan4W253ByteStore` at **0x0100F800** `sb v0,0(t1)`, emit
  scoped by compile-time address so no other `sb` in either image pays.

### The measurement (12 s gated run, `VULCAN4_W253=1`)

252 probe lines, all four probes fire. The rewind, caught live:

```
[w253:jump] n=8800000 ordinal=1395647 pc=0x0100f86c -> 0x0100f418 taken=1 t1=0x018907fe t0=0x012bdb54 a3=0x749a67d8 a2=0x00000004
[w253:jump] n=8900000 ordinal=1411212 pc=0x0100f86c -> 0x0100f418 taken=1 t1=0x01396d10 t0=0x01103dd8 a3=0xb724965c a2=0x0000001b
```

Between two consecutive logged edges (~100k jumps apart) the out pointer in **t1 falls from
0x018907FE to 0x01396D10** — ~0x4F9AEE bytes backwards. **The rewind edge is 0x0100F86C → 0x0100F418**,
i.e. the `bnel s1,s2` exit of the copy loop in `FUN_0100F390` — the bound W129's own comment already
called "THE REAL EXIT". Ordinal (`vulcan4W253CoreEnter` count) is 1,395,647 at that moment.

Also measured: the copy-loop byte store writes **0x00** for its first 64 hits at addresses
0x01051A45.. (n=1..8 all `value=0x00`), and the first core entries run with
`a3=0x2fe07a5f a2=0x00000001`, `t1=0x01051a40` — the job is entered from the payload tail, not from
the CORE.GT4 blob base.

### Probe-OFF control (the law: unset = today's behaviour)

Same command, `VULCAN4_W253` unset: **0** `w253` lines, `halt=stuck_in_syscall`,
`missing_functions=0`, `MISSING-BOUNDARIES n=0`, `distinct_pcs=227`, `total_syscall_calls=73014`,
`total_mmio_accesses=3858` — identical to the gated run on every one of those. `functions_entered`
and `frames_presented` differ by run-to-run jitter (wall-clock deadline), which is why they are not
the comparator.

Logs: `/mnt/ssd/tmp/w253-probe-run.log` (ON), `/mnt/ssd/tmp/w253-off-run.log` (OFF),
`/mnt/ssd/tmp/w253-build.log`.

### Unknown

The rewind is bracketed to a 100,000-jump window by the current `%100000` bound. Tightening it to a
per-rewind print (log only when t1 DECREASES) is a one-line change and is the obvious next probe if
the measurer needs the exact jump.
