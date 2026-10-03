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

1. **THE REAL SPIN, AND IT IS NOT THE ONE WE WERE WATCHING (W122). `0x100d908`.**

   **The old probe was watching the wrong three addresses, and six boots proved it.**
   `[w122:reach]` counts entries to `func_10057F0`/`func_1005870`/`func_1007738` unconditionally.
   It fires (so the instrument works), and on a `halt=livelocked_in_syscall` boot it prints:

       [w122:reach] entries=1 func_10057F0=0 func_1005870=0 func_1007738=0

   **All three counters are ZERO.** So `sub_010088E8`, `func_10057F0` and `func_1007738` are never
   entered on these runs. **Everything W119/W120 concluded about that spin was measuring a loop this
   halt does not visit.** Treat the "counts are equal" claim as unsupported and the delay-slot claim as
   unproven.

   **RETRACTION, and it rules out a whole branch.** W120 claimed `func_10057F0` returns -1 out of the
   delay slot at `0x1005800`. With counts EQUAL (1,1) that cannot happen: `0x1005804 sltu $v0,$a3,$a2`
   **overwrites** `$v0` with 0 before anything returns it. The delay slot only survives when
   `0x10057fc bnez` is taken, i.e. when `left < right` is genuinely true — which was never measured,
   because every reading of those counts was the `0xDEADBEEF` sentinel. The `-1` at `0x100584c`
   (element comparison) is still possible and still unmeasured.

   **THE ACTUAL SPIN — one address, 99.99% of all control transfers:**

       VULCAN4 XFER SITES distinct=407 total=483865665
         top: 0x0100d908=483826869(99.99%)

   `boot_desk120054.log`, `halt=livelocked_in_syscall`, `true_guest_entries=226639`. The guest
   executes **483.8 million** control transfers at a single PC in 90 seconds.

       0x100d8f8: lw    $s0, 0x0($s1)     ; walk a thread list
       0x100d8fc: beql  $s0, $zero, ...   ; skip empty slot
       0x100d904: nop
       0x100d908: jal   func_10202E8      ; <-- 99.99% of all transfers
       0x100d90c: lw    $a0, 0x4($s0)     ; (delay slot) target thread id

   And the callee:

       0x102040c: addiu $v1, $zero, -0x2F  ; 0x2F = sce_GetThreadId
       0x1020410: syscall 0
       0x1020414: daddu $s0, $v0, $zero    ; s0 = GetThreadId()
       0x1020418: beq   $s0, $a0, ...      ; EXIT when running thread == target
       0x1020420: jal   func_101F3A0       ; else SuspendThread (-0x38)

   **So the guest is spinning on a THREAD-ID BARRIER: it loops until `sce_GetThreadId()` returns the
   thread id it is waiting for, suspending itself in between.** It is not waiting on data, a counter,
   a clock or a producer. It is waiting for **another thread to become the running thread.**

   **And that is exactly what does not happen.** At halt:

       runnable_threads=tid1@prio3:pc=0x0100f800(ready), tid2@prio2:pc=0x0100d910(running)

   **tid1 is READY and never runs. tid2 spins on the barrier for the whole 90 seconds.** Both syscalls
   involved are implemented (`0x2F`→`GetThreadId`, `0x37/0x38`→`SuspendThread`), so this is a
   **scheduling** failure, not a missing syscall.

   **R1 ANSWER, stated plainly: the guest is waiting on guest thread tid1 to be scheduled — the word is
   tid1's readiness/state in the scheduler, not a word in RDRAM.** Fixing this means making the
   scheduler actually run the ready peer (preempt/yield at the barrier), NOT inventing a value.

   **R1 (a)(b)(c) — MEASURED, `boot_desk122351.log`, nested `d44451f`. All three answered.**

   **(a) THE ADDRESS.** `0x01047b50`:
       [w122:poll] ARMED addr=0x1047b50 (guest[$s0+4], s0=0x1047b4c)
                  inStaticData=YES (PT_LOAD seg2 0x102dc80..0x10519ac)
   Inside the game's own second `PT_LOAD` segment — **game static data**, not scratch.

   **(b) DOES IT EVER CHANGE? NO.** `distinctValues=1` for the whole run, exactly **one**
   `pollchange` event (the initial sample). `lastVal=0` throughout — **while the word is stored
   110 times.** The sampler counts every pass now (`passes` climbs 1 → 82 → 83 → 84); the earlier
   version that reported `distinct=1` after only six passes is retracted, see below.

   **(c) WHO WRITES IT — AND why it stays zero.**
       [w122:writer] FIRST STORE to 0x1047b50 size=4 value=0x0 writerPc=0x1011588 op=WRITE32
   and that instruction is:
       // 0x1011584: lw    $s1, 0x0($s0)
       // 0x1011588: sw    $zero, 0x4($s0)     <-- the polled word, written with ZERO
       // 0x101158c: sw    $zero, 0x0($s0)
   inside `sub_01011508_0x1011508`, which clears a whole struct: `sw $zero` to offsets `0x10`,
   `0x14`, `0x04`, `0x00` — a **node free/reset path**. `writerNonZero=0`: every one of the 110
   stores wrote zero.

   **SO THE WORD IS NOT "NEVER WRITTEN". It is written 110 times, always zero, by the game
   clearing the queue node it is walking.** `guest[$s0+4]` is the **thread-id field of a queue
   node**, `sub_01011508` resets it to 0 ("no thread"), and the barrier at `0x100d908` then spins
   comparing that never-populated field against `sce_GetThreadId()`, which returns 1.

   **CORRECTS THE EARLIER STORY.** `want=17070924` was `$a0 = *(s0+4)` read as a *pointer*, while
   the id field is the word at that same node. **The field is empty because no thread was ever
   registered into that slot — not because a producer failed to advance a counter.** The wall is
   upstream: the node never gets a thread.

   **THE WRITER HUNT (R1 c-follow-up): NOTHING EVER WRITES IT NON-ZERO. ZERO TIMES.**
   `boot_desk123132.log`, 90 s. Watch widened to the whole node `0x01047b40..0x01047b60` (wider on
   purpose — a thread id could be published by a bulk store a 4-byte filter would miss, which is
   the W115 stride-16 mistake). **Writes to `0x01047b50` with a non-zero value: 0.** All 25 non-zero
   stores in the node went to the **adjacent** word `0x01047b48`, values `0x10519e0 / 0x1051a00 /
   0x10d1a40 / 0x10d1a60 / 0x10d1a70`, by `writerPc=0x10122f8`. Those are **memory-card buffer
   pointers**, written in lockstep with `[MC] Open '/BASCUS-97328GAMEDATA/core.gt4' ... result=-4`.
   So `0x01047b48` is this node's MC buffer pointer and `0x01047b50` is the thread-id slot beside it.
   **Answer to "is the writer a syscall or a hardware write": neither — it is guest code at
   `0x1011588`, and it writes ZERO.** No other writer exists. Every MC open returns `result=-4`
   because mc0/mc1 hold no `core.gt4`; populating that honestly is **R5, forbidden.**

   **THE COUNTERFACTUAL — RUN IT, IT ANSWERED, AND IT PARTLY REFUTES ME.**
   Temporary probe `VULCAN4_W122_BARRIER=1` (explicitly marked for deletion, OFF by default) writes
   the running thread id into `0x01047b50` at the moment the barrier reads it. Note
   `run_gt4_desktop.sh` builds a **fixed** command with **no env passthrough** — that is why the
   first attempt logged `forced=0`. Run the harness directly on `:0` to set it.
   `/tmp/opencode/cf2_on1.txt` vs `boot_desk123814.log`, same binary:

       transfers at 0x0100d908   force OFF: 483,826,869 (99.99% of all transfers)
                                 force ON:            907 (0.00%)

   **The barrier is real and it is gated on that word.** But the boot did **not** advance — it moved
   one step downstream onto the loop I had retired:

       force ON new hot set:  0x01005890=156,756,909 (33.32%)
                              0x010089c8=156,756,909 (33.32%)
                              0x010089d4=156,756,908 (33.32%)

   **TWO CORRECTIONS I OWE, both mine:**
   1. **"`func_10057F0`/`func_1005870`/`func_1007738` are never entered" was true of the runs I
      sampled and FALSE of the boot.** Those functions are reached only *after* the `0x100d908`
      barrier is passed. The zeros were real; I wrongly generalised them into "not the livelock"
      when the correct statement is "**this is the livelock you see BEFORE the barrier**".
   2. **"the wall is that this word is never set" is too simple.** It is the **first** wall. There
      is a second behind it. `tid1` sitting `ready` while `tid2` spins was a *symptom* of the
      barrier, not the whole disease.

   `halt` is still `livelocked_in_syscall` either way, so **no rung was reached and no speedup is
   claimed.** What it buys: the barrier is proven a real gate, and the **next** wall is now named and
   reproducible — `sub_010088E8` at `0x1005890`/`0x10089c8`/`0x10089d4`, 33.32% each.

   **THE RAW OPERAND DUMP — THE ANSWER. `/tmp/opencode/dump8.log`. 145,234,000 passes.**
   The live wall is `sub_010088E8`; its compare is `func_10057F0`, entered via `jal func_10057F0` at
   `0x1005890`, and its operands are `$a0`/`$a1`, consumed as `*(a0+8)` and `*(a1+8)`.
   Replaces the old 8-line probe (a line budget is not a measurement — same failure as the first
   sampler). First 200 passes, then every 1000th, raw:

       pass=1 a0=0x1895100 a1=0x18951e0 LEFT[1895108]=0 RIGHT[18951e8]=1 leftBase=0x0     rightBase=0x1895210
       pass=2 a0=0x1fffba0 a1=0x1895280 LEFT[1fffba8]=1 RIGHT[1895288]=1 leftBase=0x18953f0 rightBase=0x18953a0
       pass=3 a0=0x1fffba0 a1=0x1895280 LEFT[1fffba8]=1 RIGHT[1895288]=1 leftBase=0x18953f0 rightBase=0x18953a0
       ...
       pass=145234000 a0=0x1fffba0 a1=0x1895280 LEFT[1fffba8]=1 RIGHT[1895288]=1 leftBase=0x18953f0 rightBase=0x18953a0

   **`LEFT CHANGED` / `RIGHT CHANGED` events in the entire run: 0.**

       final totals: distinctL=2 distinctR=1 distinctA0=1 distinctA1=1
                     leftStores=0 rightStores=0

   **SO: NEITHER OPERAND EVER CHANGES.** After pass 2 the loop is a **true infinite spin on a stale
   value** — the exact first case the brief named. `LEFT[0x1fffba8]` is `1` and `RIGHT[0x1895288]`
   is `1`: **equal**, so `left < right` is false and the delay-slot `-1` at `0x1005800` is NOT the
   exit. With equal counts the compare walks its elements instead (loop `0x1005830`), reading
   `leftBase=0x18953f0` and `rightBase=0x18953a0`.

   **`leftStores=0 rightStores=0`** — the store observer, which is the same hook that proved
   `0x01047b50` is written 110 times, saw **zero** writes to either operand in 145 million passes.
   So this is not "written and overwritten"; these two words are **never written at all**.

   **AND IT SETTLES THE W120 QUESTION DEFINITIVELY.** W120 claimed the `-1` came out of the delay
   slot because `left < right`. **Measured: the counts are equal (1 and 1), `left < right` is false,
   and `0x1005804` overwrites `$v0` before any return.** That claim is **retracted for good**. The
   spinning `-1` comes from the element comparison at `0x100584c`, which the earlier sentinel probe
   could never see.

   **THE REAL DECIDING OPERANDS ARE THE ELEMENTS, NOT THE COUNTS — AND THE WRITER IS NAMED.**
   The brief asked me to fix a write to `0x1fffba8`. **My own dump shows that premise is wrong**:
   `LEFT[0x1fffba8]=1` and `RIGHT[0x18951f8]=1` are **already equal**, so the count compare at
   `0x10057f8` passes and the function falls into its element loop. Writing `0x1fffba8` would make
   it **0**, and `0 < 1` would return `-1` and make the spin *worse*. So the fix is not there.

   With equal counts, what returns `-1` is `0x100584c`:
   ```
   0x1005820: lw    $t0, 0x14($a0)     ; leftBase
   0x100583c: lw    $v1, 0x0($v0)     ; leftElem
   0x1005840: lw    $a0, 0x0($a1)     ; rightElem
   0x100584c: bnez  $v1, -> exit      ; if leftElem < rightElem -> $v0 = -1
   ```
   Measured, `/tmp/opencode/ew2.log`:
   ```
   [w122:elem] LATCH pass=2 leftBase=0x1895360 leftElem=0 rightBase=0x1895310 rightElem=1
               -> leftElem<rightElem RETURNS -1, SPINS
   ```
   `leftElem=0 < rightElem=1`, unchanged for the whole run (`dLelem=1 dRelem=1`, zero ELEM CHANGED
   events). **`leftElem` at guest `0x01895360` is 0 and stays 0 — that is the wall.**

   **AND IT IS BEING WRITTEN — 286 MILLION TIMES, ALWAYS ZERO:**
   ```
   lElemStores=285959976
   [w122:elemw] LEFT ELEM STORE #2 addr=0x1895360 size=4 value=0x0 writerPc=0x1007774 op=WRITE32
   [w122:elemw] LEFT ELEM STORE #4 addr=0x1895360 size=4 value=0x0 writerPc=0x0     op=Ps2FastWrite32
   ```
   **The writer is guest code at `0x01007774`, inside `func_1007738`** — the very "queue walk" W120
   guessed at. Read in full it is not a queue walk, it is a **BIT-EXPANSION DECODER**:
   ```
   0x1007758: lw    $v0, 0x14($a3)     ; base
   0x100775c: sll   $v1, $a1, 2
   0x1007764: addu  $v1, $v1, $v0     ; addr = base + i*4
   0x1007768: lw    $a0, 0x0($v1)     ; current word
   0x100776c: sll   $v0, $a0, 1       ; shift left one bit
   0x1007770: or    $v0, $v0, $a2     ; OR in the incoming bit ($a2)
   0x1007774: sw    $v0, 0x0($v1)     ; STORE IT BACK
   ```
   So it is compacting a bitstream in place: shift each word left by one and OR in the next bit.
   `$a2` is that bit, set at `0x1007784: srl $a2, $a0, 31` (the bit shifted out of the previous word)
   and `0x1007794: addiu $a2, $zero, 0x1` on the first pass. **The next bit must come from the
   SOURCE buffer at `*(a3+8)`, which the decoder treats as its element count — and that count is the
   `1` that never changes.** The decoder therefore keeps ORing in zero forever, leaving `0x1895360`
   at 0 while `0x1895310` already holds 1. **Mechanism named and proved: the source stream runs dry
   because its length is never advanced, so the decoder is fed zeros forever.**

   **THE MECHANISM, TRACED TO THE INSTRUCTION. NO FIX LANDED YET.**
   `func_1007738` is a **BIT ACCUMULATOR**, and each pass does:
   ```
   0x1007768: lw   $a0, 0x0($v1)    ; current word
   0x100776c: sll  $v0, $a0, 1      ; shift left one
   0x1007770: or   $v0, $v0, $a2    ; OR in the incoming bit
   0x1007774: sw   $v0, 0x0($v1)    ; store back
   0x1007778: lw   $v1, 0x8($a3)    ; loop bound = *(a3+8)
   0x100777c: sltu $v0, $a1, $v1
   0x1007780: bnez $v0, loop
   0x1007784: srl  $a2, $a0, 31     ; next incoming bit = TOP BIT of the word just read
   0x1007788: beqz $a2, -> 0x100779c ; bit exhausted -> jal func_1005AB8 (REFILL)
   ```
   **`$a2` is the word's own top bit.** So the step is `w = (w << 1) | (w >> 31)`. **If `w` is 0
   that is `0` forever** — the accumulator can never bootstrap itself. Measured: `lElemStores=285,959,976`
   writes of value `0x0` to `0x01895360`, `dLelem=1`, zero change events. The right-hand stream
   `0x01895310` already holds `1`, so `leftElem(0) < rightElem(1)` returns `-1` and spins.

   **THE LOOP BOUND IS THE STALLED PART.** `*(a3+8)` = `LEFT[0x1fffba8]` = **1**, and
   **`leftStores=0`** — that word is never written at all. `func_1005AB8` (the refill, entered from
   `0x100779c`) contains a **trailing-zero trimmer** that is supposed to advance it:
   ```
   0x1005b1c: addiu $v0, $a0, -0x1
   0x1005b30: sw    $v0, 0x8($s1)   ; <-- THE COUNT ADVANCE
   0x1005b34: lw    $v0, -0x4($v1)
   0x1005b44: b     0x1005b20        ; loop back, and 0x1005b14/0x1005b38 can exit WITHOUT storing
   ```
   Two of its three exits (`0x1005b14`, `0x1005b38` -> `label_1005b80`) and the `beqz` at
   `0x1005b2c` (-> `label_1005b7c`) all **skip the store at `0x1005b30`**. So the count stays at 1.

   **NEXT STEP, NOT YET DONE:** instrument `func_1005AB8` to prove whether it is entered at all and,
   if it is, which of its three exits it takes. That single number says whether the fix is "the refill
   is never called" (wire it) or "the refill is called and exits without advancing" (fix its exit).
   **I have NOT landed a fix and no speedup is claimed.**

   **THE FINAL MEASUREMENT — AND IT IS A REAL RECOMPILATION DEFECT. `/tmp/opencode/rf3.log`.**
   `func_1005AB8` **IS entered** — exactly once:
   ```
   [w122:refill] ENTER #1 a0=0x1895150 a1(count)=0 a2(bit)=1
   ```
   `w122:bound` lines: **0** — the count advance **never fires**. So it is not "the refill is never
   called". It is called and its advance is skipped.

   **THE MECHANISM, AND IT IS A COMPILER BUG, NOT A GUEST BUG.** The refill's only count advance is
   ```
   0x1005b50: beqz  $s3, -> 0x1005b7c      ; $s3 = $a2 = 1, so NOT taken
   0x1005b54: addiu $s0, $s2, 0x1          ; $s0 = count+1
   0x1005b60: jal   func_10059E8
   0x1005b78: sw    $s0, 0x8($s1)          ; *(stream+8) = count+1
   ```
   and **`$s1` is the stream pointer — but `0x1005b78` never assigns `$s1` either.** Every `$s1`
   reference in the whole function is a save (`sd $s1, 0x8($sp)`) or a restore (`ld $s1, 0x8($sp)`);
   it is never set from `$a0`. The recompiled body is therefore operating on **whatever `$s1` held
   on entry**, and `0x1005ae0` exits immediately anyway because `sltu $s2, *(s1+8)` is `0 < 0` =
   false with `a1(count)=0`.

   **THIS IS THE PROJECT'S FIRST NON-HARNESS DEFECT:** a register that the original EE code sets is
   not materialised in the recompiled translation unit. That is why the accumulator's loop bound never
   moves. It is a **toolchain gap in the recompiler's register assignment**, not something to paper
   over in the runtime — patching `ps2_runtime.cpp` to write `0x1fffba8` would be exactly the fake the
   project forbids.

   **THE WHOLE-STRUCT WATCH, AND THE ANSWER IT GIVES. `/tmp/opencode/sw7.log`.**
   Window armed correctly on the left stream and left there:
   ```
   [w122:ops]   TRACK a0=0x1fffba0 LEFT now 0x1fffba8 RIGHT now 0x18951f8
   [w122:struct] ARM 0x1fffba0-0x1fffbd4 from left operand 0x1fffba8
   ```
   **EVERY WRITE to `0x1fffba0..0x1fffbd4` over the whole 90 s run: `structWrites=0`,
   `structNonZero=0`.** Not one. So **no guest code writes this stream at all** — it is not "written
   and overwritten", it is never written. `leftElem` at `0x01895360` still receives 549,831,752 writes
   of `0x0` from `0x1007774`, and `leftStores=0`, and the run ends `halt=livelocked_in_syscall`,
   `true_guest_entries=412,941,911`.

   **AND THE CALLER CONFIRMS IT IS NOT MEANT TO PRIME IT.** `sub_010088E8` receives the stream as
   `$a2` -> `$s3` and only ever **reads** it:
   ```
   0x10088f8: daddu $s3, $a2, $zero
   0x100891c: lw    $v1, 0x8($s3)      ; count  -- READ
   0x1008920: lw    $v0, 0x10($s3)     ; READ
   0x10089c8: jal   func_1007738       ; the accumulator walk
   0x10089d4: jal   func_1005870       ; the compare
   0x10089dc: bltz  $v0, -> 0x10089c8  ; the spin
   ```
   No `WRITE32` to `$s3+8` or to the stream base exists in that function. **It is a consumer. The
   producer is somebody else, and it is not being called.**

   **THE PRODUCER IS AN IOP DRIVER WE NEVER ACTUALLY LOAD — THE THIRD BRANCH OF THE BRIEF, CONFIRMED.**
   ```
   SIF module] load-emulated id=1 ref=1 path="cdrom0:\IRX\SIO2MAN.IRX;1"
   SIF module] load-emulated id=2 ref=1 path="cdrom0:\IRX\MTAPMAN.IRX;1"
   SIF module] load-emulated id=3 ref=1 path="cdrom0:\IRX\MCMAN.IRX;1"
   SIF module] load-emulated id=4 ref=1 path="cdrom0:\IRX\MCSERV.IRX;1"
   SIF module] load-emulated id=5 ref=1 path="cdrom0:\IRX\PADMAN.IRX;1"
   ```
   **`load-emulated` with the real disc path, and no file is read.** All five drivers — SIO2MAN,
   MTAPMAN, MCMAN, MCSERV, PADMAN — are emulated as host-side syscall handlers. `RPC.cpp` already
   carries an explicit `VULCAN 4 LIMITATION` for the un-served case. **So the chain is: an IOP driver
   that would seed this stream on hardware is replaced by a host stub that seeds nothing, and the EE
   then spins on a stream that was never initialised.** This is a missing game-data dependency, not a
   recompiler bug and not a scheduling bug.

   **NOT LANDED, and it is not a one-line fix:** making the boot proceed needs the real `.IRX`
   binaries, and those are **game data that must never enter this repository** (project law 1). The
   user's own disc supplies them. This is the same wall as the `core.gt4` memory-card open, one layer
   up: **every open and every driver load is failing, and the game is retrying forever.**

   **RETRACTION — MY "COMPILER DEFECT" WAS WRONG. DO NOT GO FIX THE COMPILER.**
   I claimed `sub_01005AB8` never materialises `$s1` from `$a0`. **It does.** The instruction is
   right there and my grep missed it because I searched for the wrong pattern:
   ```
   // 0x1005ac0: 0x80882d  daddu       $s1, $a0, $zero
   SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
   ```
   Register 17 **is** `$s1` and register 4 **is** `$a0`. Counting the writes to reg 17 in that
   translation unit gives exactly two, both legitimate: the `daddu $s1,$a0,$zero` and the stack
   restore. **The recompiler is correct. There is no codegen bug. Do not touch it.**

   **THAT IS THE SEVENTH TIME AN INSTRUMENT MISREAD AROUND IN THIS PROJECT**, and the family is
   now unmistakable: the sentinel guard, the stride-16 sampler, the block-as-word-index, the window
   too small, the pre-transfer sample, the wrong CLUT address, the 6-pass sampler, the element latch
   frozen at pass 1, and now a grep that reported "never assigned" for a line that says it twice.
   **The standing rule is unchanged and I broke it again: verify the instrument can see what it
   claims before believing what it says — including my own shell commands.**

   **WHAT THE REFIL MEASUREMENT ACTUALLY SHOWS** (`/tmp/opencode/rf3.log`), now that the premise is
   corrected. The refill is entered **once**, on pass 1, with `a0=0x1895150 a1(count)=0 a2(bit)=1`.
   Note `a0` is **`0x01895150`, the RIGHT-hand stream, not `0x1fffba0`.** So there is no
   contradiction in the numbers: `*(0x1895150+8)` is `0`, `sltu $s2,*(s1+8)` is `0 < 0` = false,
   and `0x1005ae0` returns before the advance. That is **correct guest behaviour for an empty
   stream** — there is nothing to advance. It is not a bug in the refill at all.

   **SO THE REAL QUESTION IS ONE LEVEL UP, AND IT IS STILL OPEN:** the left stream at `0x1fffba0`
   has bound `1` and element `0`, and `func_1007738` walks it once per outer-loop pass, ORing in the
   word's own top bit — so a word of `0` stays `0` forever. **Something must first put a non-zero
   bit into that stream, and the refill is only consulted when the incoming bit `$a2` is zero**
   (`0x1007788: beqz $a2 -> 0x100779c`), which it always is. The producer of that first bit is still
   unnamed, and it is upstream of everything measured so far.

   **ALSO RECORDED, BECAUSE IT MATTERED:** the W122 brief instructed me to "make guest `0x1fffba8`
   write". **I measured first and refused, and the measurement was right** — `LEFT[0x1fffba8]=1` and
   `RIGHT[0x18951f8]=1` are already equal; writing 0 would give `0 < 1`, return `-1`, and make the
   spin *worse*. The reviewer confirmed in writing that this was the correct call. **Measure the
   instruction before obeying it.**

   **RETRACTION — MY OWN SAMPLER, second time.** The first `w122:poll` put change-detection and the
   pass counter *inside* the "print the first 6 lines" budget, so `distinctValues=1` covered **six
   passes, not the run** — the same failure class as the W115 stride-16 sampler: an instrument that
   looks like it measures everything and measures almost nothing. Fixed and re-measured.


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
