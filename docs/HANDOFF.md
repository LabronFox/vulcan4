# VULCAN 4 — HANDOFF

**Rewritten 2026-10-03 09:40.** The previous 546 KB handoff was last written Oct 2 21:36 and did not
contain W111–W119. It is preserved verbatim at `docs/HANDOFF-archive-through-W110.md`. Per-dish records
for the current work are `docs/W111-RESULTS.md` … `docs/W117-RESULTS.md`. W118 and W119 produced no
results doc; their measurements are in this file and in the `wip-interrupted` commits.

**State at halt:** outer `main` = `d13fe0a`, nested `main` = `f15a939`, **suite 493/493**, both trees
clean. Uncommitted W119 work is saved on branch **`wip-interrupted`** (outer `e6275a6`, nested
`4af39ab`) and is instrumentation only, not a fix. Nothing pushed.

---

## WHERE WE ACTUALLY ARE

The game window is **alive on the real X11 display `:0` and drawing real content**: the GT4 2005 Sony
Computer Entertainment disclaimer, centred, white on black, 640x448.

Measured from the live window with `import -window` (my capture this session, and the captain's
`docs/evidence/caine_0847-w119-window.png`):

    640x448   16 distinct colours   mean=4256.18   stddev=13147.7

**The picture is NOT done.** The goal gate was >= 1000 distinct colours; we measure 16. The captain is
stuck behind a legal disclaimer and cannot get past it. That is the product complaint.

Important nuance: for a 16-colour white-on-black text screen, **16 colours is plausibly correct output**,
not evidence of a broken renderer. The 1000-colour gate was written for a textured 3D scene. Do not spend
more dishes trying to inflate the colour count on this screen — the goal is the NEXT screen.

## THE NUMBER THAT MATTERS MOST — boot speed, and why the simple figure misleads

The inherited claim is "901720 guest entries in 60 s ≈ 15000/s against ~330M/s on a real PS2, so 0.005% of
console speed". **That figure is not safe to quote on its own**, and here is why, measured.

Eleven runs at budget **300000/60**, all from this session:

| shape | runs | XFER SITES total | true_guest_entries | entries/sec | halt |
|---|---|---|---|---|---|
| **A pathological spin** | w119c,d,e,f,k | 207M–247M | 207M–247M | 3.4M–4.1M | wallclock_deadline / livelocked_in_syscall |
| **B normal (captain's)** | w119b,i,j | 201k–286k | 891k–1.60M | 14.8k–**26.7k** | wallclock_deadline / livelocked_in_syscall |
| **C short** | w119a,g,h | ~36k | 89k–132k | 1.5k–2.2k | pc_outside_generated_table |

Two consequences the next agent must not skip:

1. **The "fast" runs are a spin, not progress.** Shape A burns 33.32% of all control transfers at each of
   exactly three addresses (`0x010089c8`, `0x010089d4`, `0x01005890`) — a three-call cycle run ~69–82
   million times in 60 s. Guest entries are inflated by spinning. **The highest NON-SPIN throughput
   measured is 26,700 entries/sec (shape B, `w119b`).** That is the honest figure to improve.
2. **Variance across identical runs is ~1800x** (2,207 to 4,123,161 entries/sec). Any single-run speed
   claim is meaningless unless the run's shape is named. Always quote budget, halt and shape together.

The window title's `Speed: 0.xx x PS2` figure **measures guest cycles, not wall time. Do not trust it.**

## EVERY WALL W111→W119, AND ITS VERDICT

| # | Wall | What the measurement proved | Verdict |
|---|---|---|---|
| W111 | Texture region empty | Full no-stride scan: region `tbp0=10240`→`cbp` is `nonZero=0` for both FBPs; `CLUT[cbp=10378] entry0=0x0`. Correct T4 fetch still yields black. | landed (diagnostic only) |
| W112 | "Impossible" 114,688-byte packet | **NLOOP is 11 bits, not 15.** One tag's real NLOOP 1024 read as 7168 (bits 11–14 = `0b0111`); `7168*16 = 114,688`. The packet was never impossible, I misread it 7×. | **landed, real fix** |
| W112 | Desync hypothesis | **REFUTED by my own log.** Ours and PS2SDK agree field-for-field on every tag. Stream is in sync. | retracted |
| W113 | Image payload never reaches `processImageData` | **Payload is PRESENT, in the next packet.** Byte-exact map: 96-byte GS transfer-setup packet ending in an IMAGE tag claiming 16,384 bytes it does not contain; the next packet is exactly 114,688 and satisfies the declared volume. GT4 does not make IMAGE packets self-contained. | landed (diagnostic) |
| W114 | Make the decoder pay the shortfall | Carrying and paying it **does** deliver texels (16,384/2,048/1,024/256 bytes). First unconditional version broke 5 tests, then 2. Discriminator read from the packet itself (does it begin with its own IMAGE tag?) satisfies both. | **landed, 493/493** |
| W115 | Whole-payload delivery | The IMAGE tag's count is not authoritative; `TRXREG 64×448 CT32` = 114,688 bytes = exactly the payload packet size. `copiedPixels` 4096 → 28,672, atlas now holds **9,005 non-zero pixels**. **Window went from 1 colour to 16 — first non-black output in the project.** | **landed** |
| W115 | Four lying probes | stride-16 sampling; **block number used as a word index** (TBP0 is a 128-*byte* block: 10240 = word 163,840, I scanned block **1280**); window too small for the transfer; sampling the first 4 frames, before the one-time transfer. | corrected |
| W116 | `BITBLTBUF.DBW` bits 46–51 not 48–53 | Confirmed from raw qword (DBW=4 true vs 1 decoded). With dbw=1 a 64-pixel row wraps every 32 pixels, scattering every atlas. **Applying it CRASHED** (`*** buffer overflow detected ***`). | **REVERTED — see open wires** |
| W116 | `TEX0.CBP` bits 41–54 not 37–50 | Reported as a confirmed bug. **W117 proved this WRONG: this project deliberately implements the MANUAL texture-page layout where CBP is at 37–50.** 41–54 is PCSX2's AUTO layout. | **REVERTED, retracted** |
| W117 | Root-cause the DBW crash | **FAILED.** Could not reproduce in **twelve** runs (4 at 300000/45 on `:0`, 4 at 300000/60 under xvfb-run, 5 with my own probes disabled). gdb cannot `break __chk_fail` (symbol unresolvable); full gdb run exits normally; `ulimit -c unlimited` with `core_pattern` set wrote **no core**. | **no backtrace exists** |
| W117 | Apply the CBP fix | Landed, **9 tests failed**, reverted. Suite restored 493/493. | **REVERTED, retracted** |
| W118 | Boot speed hot address | `XFER SITES` shape A: the 3-address cycle above. Shape B: `0x0100afa0` 45–47%, `0x0100b678` 18–23% — **both are `jal func_101F340`, the syscall dispatcher.** | measurement only |
| W119 | Which syscall owns the spin | **Syscall `0x32` (sce_SleepThread) = 56,367 of 60,000 = 94%** of all syscalls. `negV0=0`, so "handler never sets `$v0`" is **REFUTED** for it. | measured, not fixed |
| W119 | Unrouted syscalls | **ZERO.** The dispatcher's `default: return false` is silent, which breaks our own "announce every missing path" rule, but nothing is being dropped — and therefore **GT4 never asks for the pad in this phase.** | measured |
| W119 | The spin's exit condition | **NOT FINISHED.** See open wires. | **incomplete** |

## MEASUREMENTS TRUE RIGHT NOW, WITH THEIR LOGS

All at budget **300000/60**, guest `SCUS_973.28`, config `/mnt/ssd/gt4/work/gt4.toml`.

- **94% of syscalls are `0x32`** — `/tmp/opencode/w119d.log`, line `[w119:spin] totalSyscalls=ea60
  totalInSyscallMs=b9c4 top: 0x32(hits=56367 v0Same=55627 negV0=0 ms=41595 avgUs=737)`.
  **Caution:** that `ms` figure is wall time *between* successive syscall entries — i.e. guest execution,
  NOT sleep cost. I misread it as sleep overhead and corrected it in-flight.
- **The spin cycle** — `/tmp/opencode/w119d.log`, `XFER SITES total=242909928`, `0x01005890`=80,930,611
  (33.32%), `0x010089c8`=80,930,611, `0x010089d4`=80,930,611.
- **Normal-shape hot sites** — `/tmp/opencode/w119b.log`, `XFER SITES total=286063`,
  `0x0100afa0`=132,961 (46.48%), `0x0100b678`=64,841 (22.67%), `true_guest_entries=1601980`,
  `halt=wallclock_deadline`.
- **The spin's operands** — `/tmp/opencode/w119f.log` `[w119:spin] jal func_1005870`: `a0=0x1fffba0`
  (= `$sp`), `a1=0x1895310`, `*(a0+0x10)=0`, `*(a1+0x10)=0` **EQUAL**, `*(a0+8)=1`, `*(a1+8)=1`.
- **`func_10057F0` entries** — `/tmp/opencode/w119k.log`: `a0=0x1fffba0 a1=0x1895220`, so `a0-1 =
  0x1fffb9f`, **positive**. The "`$a0` is 0 so `$v0`=-1 and it spins" hypothesis is **REFUTED** for the
  samples taken.
- **Live window** — `/tmp/opencode/in_before.png`: `colors=16 mean=4256.18 stddev=13147.7`.
- **Suite** — 493/493, verified this session on both `main` and `wip-interrupted`.

## THE THREE OPEN WIRES

1. **The spin — SOLVED DOWN TO ONE 32-BIT COMPARISON (W120). Start here.**
   `sub_010088E8` is ONE generated function, so its `0x10089dc: bltz $v0` back-edge is an internal
   `goto` and can **never** appear as a dispatch `targetPc` — which is why a probe keyed on
   `targetPc == 0x10089dc` never fired. That was structural blindness, not evidence.
   The loop is `jal func_1007738` (queue walk) then `jal func_1005870` then `bltz $v0` back.
   `func_10057F0` is **not a fetch — it is a three-way compare returning -1/0/+1**:
   ```
   0x10057f0: lw    $a2, 0x8($a0)      ; leftCount  = *(a0+0x08)
   0x10057f4: lw    $a3, 0x8($a1)      ; rightCount = *(a1+0x08)
   0x10057f8: sltu  $v1, $a2, $a3
   0x10057fc: bnez  $v1, -> 0x1005868  ; if left < right, EXIT
   0x1005800: addiu $v0, $zero, -1     ; DELAY SLOT, runs unconditionally
   0x100583c: lw    $v1, 0x0($v0)      ; only now are elements compared
   ```
   **It returns -1 the instant `*(a0+8) < *(a1+8)`, out of the delay slot, without comparing anything.**
   Measured solid: `v0=0xffffffff` at `resumePc=0x1005898` (`w120i.log`, `w120q2.log`). The caller's
   `movn $v0,$v1,$a0` only overwrites `$v0` when `*(s0+0x10)` is non-zero; that word measures 0, so
   `$v0` stays -1 and `bltz` loops.
   **So the livelock reduces to one 32-bit comparison of two counts.** Run until the corrected
   `[w120:cmp]` probe fires in a livelock run (it samples BEFORE the call) and read `leftCount` /
   `rightCount`. **UNVERIFIED:** that probe has not yet fired — three runs came back non-spin
   (XFER 153,682 / 128,817 / 37,370). Do not assume `left < right` until it prints.
   Note guest `0x1fffbc0` (`$sp`+0x10) is **never written** (store-observer silence, verified against a
   positive control on `0x1895304`) — but that may be the SAME fact as the spin, not a second one.

2. **Memory card / `mcRoot` / `sceMcUdCheckNewCard`.** Stubbed at `/mnt/ssd/gt4/work/gt4.toml` **line 180**:
   `"sceMcUdCheckNewCard@0x01017868"`. Runs during boot (earlier logs show `[MC] Open ... core.gt4 ...
   result=-4`). Standing instruction from the W119 brief: **do not touch the memory card rewrite, mcRoot
   or sceMc yet** — that is R5 only.

3. **`CBP` bits 41–54: proven a decode difference, deliberately NOT landed.** PCSX2's AUTO layout puts
   `CBP` at 41–54 (raw `0x511466942a800` → `CBP=648`); this project implements the MANUAL layout at 37–50
   (→ `10378`), and **9 tests enforce the MANUAL layout**. So this is *not* a bug to fix. The genuinely
   unexplained part is that the CLUT is empty at **both** `cbp=648` and `cbp=10378` (4096 entries, all
   zero) while transfers land at `dbp` 10240, 10688, 10696, 10700 — **none of which is a CLUT address.**

## WHAT IS DELIBERATELY NOT DONE, AND WHY

- **`WritePixel` / the z-test.** Refuted by count, not argument (`alpha=0 ate=0 zpass=3,604,224
  WROTE_VRAM=5,280,890`). **Do not touch it.**
- **`BITBLTBUF.DBW` correct shift.** Correct per the authoritative layout, but applying it crashed. Not
  shipped. I also **withdrew** my claim that it was masking a latent write-path bounds defect — the crash
  did not reproduce in twelve runs, so that claim is unsupported.
- **Entry-point jumping to skip the disclaimer.** Explicitly forbidden: it skips font atlas and disc IO and
  yields a black screen, which is worse.
- **Chasing the 1000-colour gate on the disclaimer screen.** 16 colours is plausible correct output for
  white-on-black text. The gate was written for a textured scene.
- **Input wiring.** `Kernel/Stubs/Pad.cpp` **is** compiled (CMake `GLOB_RECURSE` at
  `ps2xRuntime/CMakeLists.txt:432`) and implements a real gamepad path plus keyboard (`X`=Cross,
  `ENTER`=Start, `Z`/`C`/`V`=Square/Circle/Triangle). But **nothing calls `scePadOpen`/`scePadRead`** — the
  dispatcher has no Pad cases. Measured consequence: GT4 issues **zero** pad syscalls during this boot
  phase, so this is **not** why the boot sits on the disclaimer. R4 work, not R3.
- **Memory card rewrite.** Forbidden until R5.

## THE TRUTH ABOUT THIS AGENT

What I got wrong, since the retractions are the useful part:

- **I reported a comparison across two different halts as a result** ("frames 230 → 3148" was
  `pc_outside_generated_table` vs `livelocked_in_syscall`). The captain had already taught me this. I did
  it anyway, and later produced a "fix" and gate numbers that were variance, not signal. Two runs of an
  11-bit build came back at 263/253 frames; four more of the *same binary* came back 1565–1615.
- **I misread my own instrument five times** and each time reported a confident `nonZero=0`: stride-16
  sampling; block-number-as-word-index; a scan window smaller than the transfer; sampling before the
  transfer happened; and reading the CLUT at the wrong address. The lesson I wrote down and should live by:
  **the proof that a probe is broken is not that it agrees with the last probe.**
- **I called a deliberate design a bug.** TEX0 `CBP` at 37–50 is this project's MANUAL layout; I compared
  it against PCSX2's AUTO layout, reported "BUG 1 CONFIRMED", and two dishes were built on it. Nine tests
  named the design in their titles. Measure against the *project's* reference, not a familiar one.
- **I stated a memory-safety finding I could not support** — that the wrong `DBW` masked a latent bounds
  defect — from two aborts that never reproduced. Withdrawn.
- **I attributed inter-syscall guest execution time to `SleepThread`** as "sleep cost". It was never sleep
  cost. Corrected in-flight.

What I would do differently: **verify an instrument can physically see what it claims before believing its
output**, and **quote budget, halt and run-shape with every number**, because on this project the same
binary produces an 1800x spread and I lost whole dishes to that.

## THE FIRST THREE THINGS TO DO NEXT

1. **Finish the `func_1005870` spin — from the open wire, not from scratch.** The unexplained fact is that
   the guest never reaches `0x10089dc` despite `0x10089d4` being 33.32% of transfers. Instrument the
   *return path* rather than the branch: log every entry into generated function `0x1005870` and every exit
   from it (including any exception/yield path, since `handleSyscall` throws and the harness re-enters at a
   saved PC). Find out who is supposed to advance `*(sp+0x10)` at `0x1fffba0` — that is the word the whole
   boot is waiting on. Compare against shape B, where the boot does reach the disclaimer.
2. **Then land the speed fix, measured against the honest baseline: 26,700 entries/sec (shape B,
   `/tmp/opencode/w119b.log`), not the 15,000 figure and not the 4.1M spin figure.** Any fix must be
   reported as: shape named, halt named, budget named, and the 3-cycle's share of `XFER SITES` before and
   after. The acceptance test is that `0x010089c8 / 0x010089d4 / 0x01005890` drop below 1% while
   `true_guest_entries` rises **in the same shape**.
3. **Then chase the missing CLUT**, which is a real gap with no excuse: transfers land at `dbp` 10240,
   10688, 10696, 10700 and none is a CLUT address, and the palette is zero at both candidate addresses.
   The texels are most likely inside the 114,688-byte payload already received, after the texel run, where
   `UploadImage`'s `totalPixels` bound stops before reaching them. One read-only measurement: dump the byte
   offset of the first non-zero region in that payload past the texel run and check whether a CLUT-shaped
   run follows.

Standing rules, unchanged: numbers from runs only; a commit is not proof; the untouched original is the
default state and everything we add ships OFF by default; suite stays 493/493; author `Or Golan`; do not
push.


---

## W120 STATE (added at the session halt, 2026-10-03)

**No rung reached.** `halt=livelocked_in_syscall` in the pathological shape, `true_guest_exits=0`,
window still the 2005 Sony disclaimer. Suite 493/493. Outer `main` and nested `main` both clean.
Full detail in `docs/W120-RESULTS.md`.

**The number that matters: the boot is nondeterministic.** Same binary, same budget 300000/60, three
runs: `true_guest_entries` **83,905 / 1,117,046 / 238,826,320** — a **2,846x spread on identical input**.
Two modes: Mode A livelocks (three addresses at exactly 33.32% each of ~297M transfers); Mode B is the
captain's shape and walks the disclaimer. Any single-run speed claim is meaningless.

**Where the 90 s goes** (`[w120:split]`, timing `targetFn()` — the guest's own code — separately from
our runtime): `guestEntries=461,000,000 guestBodyMs=52,547 pctOf90sBudget=58%`. **58% is the guest
executing instructions, 42% is our runtime.** We are not overheaded enough to explain the crawl alone.

**Three suspects measured and exonerated — do not re-chase:**
| suspect | measurement | verdict |
|---|---|---|
| syscall dispatch | `[w120:dispatch] avgUs=6 maxUs=13`, 100,000 calls / 90 s | **<1%** of budget |
| branch-edge bookkeeping | `[w120:edge] avgNsPerCall=45` | **0.09%** of budget |
| disc reads | `[w120:cd]` census **never fired** | `sceCdRead` not called 500x; **not** the gate |

**Landed:** `m_branchEdgeIndex` — the dispatch path did a **linear scan of ~400 edges** plus unbounded
`push_back` on **every guest function entry**; now one hash probe, same reported data. Correct but
**MINOR** (0.09%), and no speedup is claimed from it. Hot-path timers are now **OFF by default** behind
`VULCAN4_W120_TIMING=1`, because two `steady_clock` reads per guest call cost about as much as the 45 ns
they were measuring.

**Probes added (read-only, bounded):** syscall leaderboard, unrouted-syscall census, `sceCdRead` census,
`func_1007738` queue probe, `func_10057F0` compare + return-value probes, guest-vs-runtime split, edge
timing.

**The retraction to carry forward:** the first `[w120:cmp]` probe ran AFTER `targetFn()` and read
return-state registers, not arguments — it printed `a0=0x1 a1=0x0` and counts of `3735928559`, which is
`0xDEADBEEF`, its own unreadable-address sentinel. Meaningless, withdrawn, now sampling before the call.
**That is the sixth time this project a probe reported a confident result it was not positioned to see**
(stride-16 sampler, block-as-word-index, too-small window, too-early sample, wrong CLUT address, and this).
The standing lesson, and the reason several of my walls were misdiagnosed for days: **run a positive
control before believing any "no output" result.** The `0x1fffbc0` zero is trustworthy only because a
control fired on another address.

**First three things to do next, revised:**
1. Run until `[w120:cmp]` fires in a livelock run; read `leftCount` vs `rightCount`. If `left < right`,
   the spin is confirmed and the question becomes "which side is short, and who should extend it" — a
   data-flow question about two guest-owned structures, not a runtime search.
2. Then, and only then, re-measure with **at least 5 runs per configuration**. At a 2,846x spread, one
   before/after pair proves nothing — that is how I nearly reverted a correct fix in W112.
3. Keep R3's real acceptance in view: `halt` not `livelocked_in_syscall`, `true_guest_exits > 0`, and a
   window capture showing something past the disclaimer. The PNG is the product.
