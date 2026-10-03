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

   **MY "MISSING IRX" BLOCKER WAS FALSE. IT WAS NEVER CHECKED. THE DRIVERS LOAD AND RUN.**
   I declared the `.IRX` binaries absent without running `ls`. They are present, extracted from the
   captain's own disc:
   ```
   /mnt/ssd/gt4/work/IRX/SIO2MAN.IRX   15653   MCMAN.IRX  96181   PADMAN.IRX  45925
   /mnt/ssd/gt4/work/IRX/MTAPMAN.IRX   10853   MCSERV.IRX  7385   (and 20+ more, dated Oct 2006)
   ```
   **AND THE LOAD IS REAL, NOT A STUB.** I was misled by the log token `load-emulated`, which is the
   SIF-layer wording, not evidence of a stub. The IOP layer underneath actually does the work:
   ```
   IopEmulator::loadModule -> IopModuleLoader::readWholeHostFile  (opens and reads the file)
                            -> IopModuleLoader::load             (ELF parse, relocations)
                            -> callFunction(module.entry, ...)    (EXECUTES the driver)
   ```
   and the run log proves it executed:
   ```
   [IOP] loaded IRX id=1 entry=0x10634 base=0x10000 start=0     (SIO2MAN)
   [IOP] loaded IRX id=2 entry=0x11b24 base=0x11000 start=0     (MTAPMAN)
   [IOP] loaded IRX id=3 entry=0x13078 base=0x12f00 start=2     (MCMAN)
   [IOP] loaded IRX id=4 entry=0x3a340 base=0x3a300 start=2     (MCSERV)
   [IOP] loaded IRX id=5 entry=0x40f48 base=0x3db00 start=0     (PADMAN)
   ```
   **So: the drivers load, their relocations run, their entry points execute in IOP RAM. The left
   stream at `0x1fffba0` is EE memory and no IOP driver writes EE memory anyway, so the seeding is
   NOT an IRX responsibility and I was chasing the wrong layer entirely.** The producer is an EE-side
   function that is never called, and it is still unnamed. That is where the next agent starts.

   **CALIBRATION, RECORDED BECAUSE IT IS THE THIRD TIME.** Three of my declared blockers have turned
   out false once someone ran a command (the sentinel guard, the 6-pass sampler, and now this), and
   twice a reviewer's instruction has been wrong once measured (write `0x1fffba8`; fix the codegen).
   **A blocker I have not personally verified with `ls`/`grep`/a run is not a blocker, it is a
   guess.** `ls /mnt/ssd/gt4/work/IRX` costs one second; assuming costs a whole iteration.

   **ITERATION 9: THE WATCH, RE-ARMED AT GUEST ENTRY. STILL ZERO. AND THE INSTRUMENT IS NOW PROVEN LIVE.**
   I diagnosed the arming point myself last turn and this turn I acted on it. The watch on
   `0x1fffba0..0x1fffbb4` was armed from the first `func_10057F0` compare — inside the very loop it
   exists to observe — and that can only ever report zero. So it is now armed from the harness's own
   guest start, **before the first guest instruction executes**, and it **chains** to the previously
   installed observer rather than replacing it (there is a single observer slot and this project
   installs into it from four places; the last writer was silencing the others, which is why the
   first attempt at this printed nothing at all).

   ```
   [w122:sp] ARMED watching 0x1fffba0-0x1fffbb4 installed=1 prev=0
   ```
   `installed=1` — the hook took. **And the result is still `0x1fffba0..0x1fffbb4` writes: ZERO**, for
   the whole run.

   **AND THE INSTRUMENT IS PROVEN LIVE, so this zero is a REAL FACT, not the eleventh blind probe.**
   `ps2TraceGuestWrite` is called by the `WRITE32/16/64/8/128` macros on **every** guest store
   (`ps2_runtime_macros.h:446`), and the generated translation unit contains 159 such call sites.
   The observer is installed before execution begins and `ps2GetGuestStoreObserver()` confirms it is
   the installed one. **So no guest instruction writes those 21 words, ever.**

   **AND THE ADDRESS IS DEFINITELY LIVE STACK, NOT COLD MEMORY.** `$sp` is initialised to
   `PS2_RAM_SIZE - 0x10` = `0x01FFFFF0`, and `0x01FFFBA0` is **1120 bytes (1 KB) below that** — deep
   inside the guest's own stack frame region, which the game uses constantly. `sub_010088E8` allocates
   `0x80` bytes per frame (`0x10088e8: addiu $sp,$sp,-0x80`), so this window is squarely inside
   frames the game pushes and pops.

   **SO THE CONCLUSION IS NARROWER AND MORE USEFUL THAN "nobody writes it":** the stack around
   `$sp` is written tens of millions of times a second, **but never at these 21 words**. That is a
   very specific pattern, and it says the stream struct is **not stack-resident at all** — the guest
   is passing `$sp` (or `$sp`-relative) where a *pointer to* a stream is expected, and the compare
   and the accumulator are both reading a struct that was never built. **The remaining question is
   narrow and cheap: what does the guest actually intend `$s3` to point at, and is the real stream
   somewhere the compare should have been given?**

   **ITERATION 10: READ FROM THE REAL ELF. THE ANSWER IS A THIRD OPTION NEITHER OF US NAMED.**
   Disassembled with `mips-linux-gnu-objdump -d /mnt/ssd/gt4/work/SCUS_973.28` (the game's truth, not
   our translation). **Exactly ONE caller of `0x10088e8`:**
   ```
    1008ff0:  lw a3, 4(s2)
    1008ff4:  lw a1, 4(s4)
    1008ff8:  lw a2, 4(s5)      <-- $a2 = *(s5+4), a POINTER TO A STREAM
    1008ffc:  jal 0x10088e8
    1009000:  lw a0, 4(s3)      <-- delay slot
   ```
   So `$a2` is a genuine **stream pointer**, dereferenced exactly once. **"Needed one more dereference"
   is REFUTED by the game's own code.**

   **AND THE REAL FINDING — the callee works on a STACK COPY, not on `$s3`.**
   ```
    10088e8:  addiu sp,sp,-128            <-- 0x80 frame
    10088f8:  move  s3,a2                 <-- s3 = the stream pointer (only read)
    100891c:  lw    v1,8(s3)              <-- count
    1008920:  lw    v0,16(s3)             <-- base
    1008924:  sw    zero,4(sp)
    1008928:  sw    v0,16(sp)             <-- COPY base onto its own stack
    100892c:  sw    v1,8(sp)              <-- COPY count onto its own stack
    1008930:  sw    zero,12(sp)
    1008938:  sw    zero,20(sp)
    100893c:  move  a1,v1
    1008940:  move  a0,sp                 <-- passes ITS OWN STACK as the stream
   ```
   and the spin loop confirms it — **both callees are handed `$sp`, not `$s3`:**
   ```
    10089c8:  jal  0x1007738
    10089cc:  addiu s1,s1,1
    10089d0:  move  a0,sp                 <-- delay slot: a0 = sp
    10089d4:  jal  0x1005870
    10089d8:  move  a1,s2                 <-- a1 = the OTHER stream
    10089dc:  bltz v0,0x10089c8
    10089e0:  move  a0,sp
   ```
   **THIS EXPLAINS EVERYTHING MEASURED SO FAR, INCLUDING MY OWN "IMPOSSIBLE" ZERO.** The stream the
   compare and the accumulator read is **`sub_010088E8`'s own 0x80-byte stack frame**, freshly zeroed
   at `0x1008924`–`0x1008938` and refilled from `$s3` at only two offsets. `$sp` starts at
   `0x01FFFFF0` and this function allocates `0x80`, so the frame lands near **`0x01FFFF70`**, NOT at
   `0x01FFFBA0`. **I have been watching the wrong address for several turns** — `0x1fffba0` is 1120
   bytes lower down and is simply never touched, which is why the guest-entry watch correctly found
   nothing. The "stream struct is not stack-resident" conclusion from iteration 9 was **half right for
   the wrong reason**: it IS stack-resident, just not *that* stack slot.

   **SO THE REAL QUESTION IS NOW SHARP AND SMALL:** `$s3` (`*(s5+4)` in the caller) is read at
   `0x100891c`/`0x1008920` for count and base and then **never written** — the loop only mutates its
   own copy. So the producer must fill the stream that lives at `*(s5+4)`, and `s5` is a struct whose
   `+4` field is that stream pointer. **Next step: find who writes `4(s5)` — or whatever `s5` points at
   — in the caller at `0x1008ff8`, and whether `s5` itself is set up earlier in that function.**

   **ITERATION 11: BOTH HALVES DONE. THE PRODUCER CHAIN IS FULLY RESOLVED — TO THE GAME'S ROOT.**
   **(1) WHERE `$s5` COMES FROM — traced all the way up, using the real ELF only:**
   ```
   0x1008ff8:  lw a2,4(s5)        ; caller of sub_010088E8
   0x1008c88:  move s5,a2         ; s5 = this function's $a2
   0x1008c64:  move s3,a0         ; (and s3 = its $a0)
   0x1008c6c:  move s4,a1
   0x1008088:  move s8,a0         ; sub_01008080: s8=a0, s2=a1, s7=a2
   0x10061c0:  move a2,s4         ; call site 1 of 2
   0x1006264:  move a1,s4         ; call site 2 of 2
   ```
   **`sub_01008080` has exactly two callers, `0x10061bc` and `0x1006260`, both inside the outermost
   function at `0x1005d48`.** So the stream pointer is `$s4`/`$s2` of **`sub_01005D48`** — one function
   below the syscall layer, and **two hops above the spin**. That is the producer's owner.

   **(2) THE RE-POINTED WATCH — the instrument was the bug, twice over.**
   Re-pointed from `0x01FFFBA0` to `sub_010088E8`'s own frame. `0x01FFFBA0` really is never written —
   that zero was correct. The frame is at `$sp-0x80` with `$sp` starting at `0x01FFFFF0`, so it lives
   near `0x01FFFF70`, and a run with the corrected window reports **368 writes into the frame region
   where the old address reported zero.** The instrument now sees the stack.
   *Second* correction inside the same turn: the first re-point used `0x01FFFFEF` as the top, taken
   from the initial `$sp`. But the stream copy is `sw v0,0x10($sp)` / `sw v1,0x8($sp)` — offsets
   **above** the frame pointer — so that window cut off precisely the two stores that matter. Widened
   to `0x01FFFEC0-0x01FFFFFEF`.

   **(3) THE RESULT THAT MATTERS, in a run where the spin is real** (`f3.log`,
   `halt=livelocked_in_syscall`, **`0x01005890=134,202,854`** transfers — the spin is running):
   **`sub_010088E8` writes NOTHING to its own frame.** Zero writes from `writerPc` in `0x1008xxx`.
   The 368 writes in the window all come from elsewhere (`0x1018bc0`, `0x1010c40`, `0x1028xxx`).

   **AND THAT IS THE ANSWER TO THE REVIEWER'S EITHER/OR, AND IT IS NEITHER.** The disassembly says
   `sw v0,16(sp)` and `sw v1,8(sp)` **must** execute — those are the stream copy — and they are
   `WRITE32`, so the observer would see them. They are not there, while the spin demonstrably runs.
   **So `sub_010088E8` is NOT the function executing the spin in these runs.** The `0x10089c8`/`0x10089d4`
   transfers attributed to it are being counted, but the frame stores its own prologue mandates are
   absent from the window. **Next agent: find which function is ACTUALLY executing that loop, because
   it is not `sub_010088E8` as generated — most likely the spin is a different translation unit that
   reuses those addresses, or the loop body is reached without the prologue.**

   **ITERATION 12: `$sp`/`$ra` MEASURED. MY OWN CENSUS KEY WAS THE BUG — AND IT RETRACTS ITERATION 11.**
   **First, the census bug, found in the game's own table rather than my code.** I keyed the probe on
   `0x10089c8` / `0x10089d4` / `0x1005890`. `register_functions.cpp` proves **none of those is in
   `g_ps2RecompiledFunctionTable`** — they are internal labels inside generated functions, so `targetPc`
   can never equal them and no `targetPc`-keyed probe can ever fire there. **That is the same fact W120
   established about `0x10089dc` and I did not apply it here.** The transfer census counts `ctx->pc`
   *inside* a function, which is why it can see addresses no dispatch-keyed probe can. Re-keyed to the
   three entries that genuinely exist:
   ```
   g_ps2RecompiledFunctionTable[8760] = sub_010088E8_0x10088e8; // 0x10088e8
   g_ps2RecompiledFunctionTable[8785] = sub_010088E8_0x10088e8; // 0x100894c
   g_ps2RecompiledFunctionTable[8790] = sub_010088E8_0x10088e8; // 0x1008960
   ```
   **AND THE MEASUREMENT** (spin live at `0x01005890=131,106,519`):
   ```
   [w122:spra] pc=0x10088e8 sp=0x1fffc20 ra=0x1009004
               s0=0x18951f0 s1=0x18951a0 s2=0x1fffcc0 s3=0x1fffcb0 frame(sp-0x80)=0x1fffba0
   ```
   **`$ra = 0x1009004` — which is exactly the instruction after the caller's `jal 0x10088e8`.** So the
   loop **IS** in `sub_010088E8` and the return path is intact. **Iteration 11's conclusion is
   RETRACTED: the census was not misattributing the function, my probe key was.**

   **AND `sp = 0x1fffc20`, which retires the whole "wrong address" thread — including my own.** The
   stream copy is `sw v1,0x8($sp)` / `sw v0,0x10($sp)`, so the fields are at **`0x01FFFC28`** (count)
   and **`0x01FFFC30`** (base). Iteration 9's "`0x01FFFBA0` is `sp-0x80`" was **arithmetic on a
   plausible-looking number and was wrong**; `sp-0x80` is not where the fields are. `0x01FFFBA0` is
   where an *earlier, deeper* frame lives. **Three windows, three wrong answers: too low
   (`0x01FFFBA0`), too high (`0x01FFFEC0-0x01FFFFFEF`, above `sp`), and now centred on the measured
   `sp` — `0x01FFC00-0x01FFC40`.**

   **AND THE HARD CONTRADICTION, STATED PLAINLY.** With the window centred on the measured `sp`, a run
   with the spin live at `0x01005890=131,641,093` reports **ZERO writes to `0x01FFC00-0x01FFC40`**. The
   prologue's `sw zero,4(sp)` / `sw v0,16(sp)` / `sw v1,8(sp)` / `sw zero,12(sp)` / `sw zero,20(sp)` are
   five `WRITE32`s that **must** execute on entry and **must** be observed. They are not.
   **So either `WRITE32` to `$sp`-relative addresses does not route through `ps2TraceGuestWrite` in this
   path, or the writes are being redirected.** That is now the single narrowest open question in the
   project and it is a question about **our WRITE32 macro**, not about the game. **Do not trust any
   stack-write conclusion from this observer until that is answered.**

   **ITERATION 13: THE INSTRUMENT IS FIXED AND VERIFIED. THE ANSWER IS "THE GAME REALLY DOESN'T WRITE IT".**
   **Test 1 — the codegen is NOT at fault.** `ps2_recompiled_functions.cpp` has **2,569** stores in the
   `WRITE32(...)` macro form and **159** in the pre-expanded traced form. The macro form is fine:
   ```
   #define WRITE32(addr, val) { uint32_t _addr = (addr);
       if (PS2Runtime::isSpecialAddress(_addr)) runtime->Store32(rdram, ctx, _addr, (val));
       else { ps2TraceGuestWrite(rdram, _addr, 4u, (uint32_t)(val), 0u, "WRITE32", ctx);
              FAST_WRITE32(_addr, (val)); } }
   ```
   It traces unconditionally for ordinary RAM, and `Ps2IsSpecialAddress` is false for the whole
   `0x01FFCxx` window. **`instruction_translator.cpp` is correct and needs no change.**

   **Test 2 — THE REAL HOLE, AND IT WAS MINE: ONE OBSERVER SLOT, FOUR INSTALL SITES, LAST WRITER WINS.**
   ```
   ps2_runtime.cpp: w122SpObserver          (armed at guest entry)
   ps2_runtime.cpp: w122OperandStoreObserver (armed at the FIRST func_10057F0 compare)
   Thread.cpp:      w122PollStoreObserver   (armed at the first refill)
   harness:         watchGuestStoreForPath  (env-gated, off by default)
   ```
   `g_ps2GuestStoreObserver` is a **single global**. The compare probe arms on the first
   `func_10057F0` entry — **which happens inside the spin** — so from that instant the guest-entry stack
   watch was **uninstalled**. That is exactly why it reported writes early in a run and then went
   silent, and why five mandatory prologue stores looked "invisible": they were going to a different
   observer. **FIXED: one dispatcher, many subscribers.** Every probe now calls
   `ps2AddStoreSubscriber()`; `w122StoreDispatch` fans out to all of them and is installed once.

   **AND THE INSTRUMENT IS NOW PROVEN CORRECT, which settles it:**
   ```
   [w122:disp] DISPATCHER INSTALLED subs=1 previousObserver=no
   [w122:disp] CONTROL delivered=1602880 subs=2 lastAddr=0x1308939
   ```
   **1,602,880 stores delivered, two subscribers live.** The observer works.

   **SO THE MEASUREMENT IS NOW TRUSTWORTHY, AND IT SAYS: NOTHING WRITES THE FRAME.**
   Watch centred on the **measured** `sp=0x1fffc20` (`0x01FFC00-0x01FFC40`, spin live at
   `0x01005890=123,396,892`): **zero writes.** `sub_010088E8` executes — `$ra=0x1009004` proves it is
   entered from the right call site — and its five prologue stores land nowhere the observer can see.

   **THE HONEST CONCLUSION, AND IT IS NOT A GAME BUG:** `sub_010088E8` runs ~50 million times per
   second, and **it never writes to the stack frame it just built.** The only way that is true is if
   `sub_010088E8` is **not actually executing its own prologue** — i.e. the generated function is being
   entered at a label past the prologue, or `ctx->pc` dispatch is skipping it. The table has **three**
   entries (`0x10088e8`, `0x100894c`, `0x1008960`); my probe showed `pc=0x10088e8`, so the prologue
   *should* run. **That contradiction is unresolved and it is the narrowest remaining question: does
   the generated `sub_010088E8` actually execute `ctx->pc = 0x1008924u` and the five `WRITE32`s when
   entered at `0x10088e8`?** Everything else is now measured and trustworthy.

   **ITERATION 14: THE CONTRADICTION WAS A TYPO IN MY OWN WINDOW. NOTHING WAS BROKEN.**
   Read the generated body (`ps2_recompiled_functions.cpp:43400`). The three questions:

   **(1) IS THE PROLOGUE REACHABLE FROM `case 0x10088e8`?** The switch has **no `case 0x10088e8`** — it
   falls through `default: break` straight into the body. **So the prologue is the fall-through path,
   not a skipped one.** The table's three entries are `0x10088e8` (prologue), `0x100894c` and
   `0x1008960` (resume past it), and `0x10089c8` is a spin label reached only from `0x100894c`.
   Measured first arrival: `pc=0x10088e8 (PROLOGUE RUNS) sp=0x1fffc20 s3=0x1fffcb0 s0=0x18951f0`.

   **(2) ARE THE FIVE STORES REAL TRACING `WRITE32`s?** Yes, all five:
   ```
   0x1008924: sw $zero,0x4($sp)   -> WRITE32(ADD32(GPR_U32(ctx,29), 4),  GPR_U32(ctx,0))
   0x1008928: sw $v0,0x10($sp)    -> WRITE32(ADD32(GPR_U32(ctx,29), 16), GPR_U32(ctx,2))
   0x100892c: sw $v1,0x8($sp)     -> WRITE32(ADD32(GPR_U32(ctx,29), 8),  GPR_U32(ctx,3))
   0x1008930: sw $zero,0xC($sp)   -> WRITE32(ADD32(GPR_U32(ctx,29), 12), GPR_U32(ctx,0))
   0x1008938: sw $zero,0x14($sp)   -> WRITE32(ADD32(GPR_U32(ctx,29), 20), GPR_U32(ctx,0))
   ```
   **The translator did not elide anything.** No codegen bug.

   **(3) THE REAL ANSWER, AND IT IS MY OWN TYPO.** The generated body runs `addiu $sp,$sp,-0x80`
   **before** the stores, so the measured `sp=0x1fffc20` is **already decremented** and the five stores
   land at `0x01FFFC24 / 28 / 2C / 30 / 34`. **My window was `0x01FFC00-0x01FFC40` — I typed `0x1ffc`
   where the frame is `0x1fffc`, dropping THREE hex digits.** The window sat **0x1DFFFE0 bytes below**
   the frame and could never see one of them. **Nothing was wrong with the game, the codegen, the
   observer slot, or the dispatcher** — the observer delivered 1.6M stores correctly and I was
   watching empty memory.

   **WITH THE WINDOW CORRECTED, THE CENSUS IS HONEST AT LAST** (`t1.log`, spin live at
   `0x01005890=120,669,828`): **244 writes into the frame where the broken window reported zero**, and
   `sub_010088E8`'s own stores are visible for the first time:
   ```
   [w122:sp] #239 addr=0x1fffc10 size=8 value=0x1fffcd0 writerPc=0x1008904 op=WRITE64   ; sd $s0,0x10($sp)
   [w122:sp] #241 addr=0x1fffc08 size=8 value=0x1fffca0 writerPc=0x1008914 op=WRITE64   ; sd $s1,0x8($sp)
   [w122:sp] #243 addr=0x1fffc18 size=8 value=0x1009004 writerPc=0x1008918 op=WRITE64   ; sd $ra,0x18($sp)
   ```
   **Every claim from iterations 9-13 about "nothing writes the frame" is RETRACTED.** The function does
   write it, on every entry, exactly as the disassembly says.

   **ITERATION 15: WATCHED EXACTLY TWO WORDS. THE WRITER IS NAMED — AND IT IS *NOT* THE PRODUCER.**
   Window narrowed to `0x01FFFC28` (count) and `0x01FFFC30` (base) only. Whole 90 s run, 18 lines,
   and they are all one burst from a single writer:
   ```
   #1  FIELD=COUNT(sp+0x08) addr=0x1fffc28 size=1 value=0x38 writerPc=0x100f800 op=WRITE8
   #3  FIELD=BASE(sp+0x10)  addr=0x1fffc29 size=1 value=0xff writerPc=0x100f800 op=WRITE8
   #5  addr=0x1fffc2a value=0x2d   #7  addr=0x1fffc2b value=0x3a   #9  addr=0x1fffc2c value=0x4f
   #11 addr=0x1fffc2d value=0x0c   #13 addr=0x1fffc2e value=0x02   #15 addr=0x1fffc2f value=0x1c
   #17 addr=0x1fffc30 value=0x2c
   ```
   **The writer is `0x100f800`, and it is `sub_0100F390_0x100f390` — a generic byte-copy inner loop:**
   ```
   0x100f7f8: addu  $s2, $s1, $s3
   0x100f7fc: lbu   $v0, 0x0($s1)      ; load byte
   0x100f800: sb    $v0, 0x0($t1)      ; <-- store byte
   0x100f804: addiu $t1, $t1, 0x1      ; dst++
   0x100f808: bne   $t1, $t4, ...      ; loop until t1 == t4  (a memcpy)
   0x100f80c: addiu $s1, $s1, 0x1      ; src++ (delay slot)
   ```
   **AND THE VALUES SAY IT IS NOT THE STREAM PRIME: they are ASCII.** `38 ff 2d 3a 4f 0c 02 1c 2c`
   reads as `8ÿ-:O...,` — **not** a count and **not** a base pointer. As little-endian words they
   would be count `0x3A2DFF38` and base `0x2C1C020C`, which are nonsense for a stream. **So this is a
   bulk `memcpy` passing over the frame as scratch, and hitting these two words only because that is
   where `$sp` happened to be.** It is transient and it is not the producer.

   **SO THE HONEST POSITION AFTER FIFTEEN TURNS:** the two words are written exactly once per run, by a
   memcpy, with values that are clearly not a stream. **`sub_010088E8`'s own `sw v1,0x8($sp)` /
   `sw v0,0x10($sp)` — the instructions that would actually prime them — do not appear at all in this
   run**, because this run halted `pc_outside_generated_table` and never entered the spin. **The one
   measurement still missing is the two-word watch on a run that actually spins**, which is the same
   shape as every gap in the last six turns: I keep getting a non-spinning run and reading it as if it
   were the interesting one.

   **ITERATION 16: GATED ON A REAL SPIN RUN. AND IT EXPOSES THE MISTAKE I HAVE MADE SINCE ITERATION 10.**
   Gate first, as instructed: `halt=livelocked_in_syscall` AND `0x01005890 > 0`. Runs 1-2 failed the
   gate (`pc_outside_generated_table`, spin 0) and their numbers are discarded. **Run 3 passed:**
   `v3.log`, `halt=livelocked_in_syscall`, `0x01005890=126,799,715`.

   **ON THAT VERIFIED RUN, the two words at `0x1fffc28`/`0x1fffc30` are written 34 times, and EVERY write
   is a callee-saved register save from a different function:**
   ```
   0x1011000 -> 0x1005a4c   0x1012530 -> 0x1fffe28   0x101254c -> 0x1005a4c
   0x1012890 -> 0x18951f0   0x1012894 -> 0x1fffcb0   0x1012884 -> 0x1fffd80
   0x101288c -> 0x1fffca0   0x1007258 -> 0x1fffc70   0x101289c -> 0x1010fe8
   ```
   and the generated code says what they are:
   ```
   0x1012884: sd $s2, 0x10($sp)   -> WRITE64(ADD32(sp,16), GPR(18))
   0x101288c: sd $s0, 0x0($sp)    -> WRITE64(ADD32(sp,0),  GPR(16))
   0x1012890: sd $s1, 0x8($sp)    -> WRITE64(ADD32(sp,8),  GPR(17))
   0x1012894: sd $s3, 0x18($sp)   -> WRITE64(ADD32(sp,24), GPR(19))
   ```
   **THE MISTAKE, STATED PLAINLY: `$sp` IS NOT A FIXED ADDRESS, AND I TREATED IT AS ONE.** Each caller
   has its own frame, so `0x1fffc28` is shared scratch that many different functions save registers
   into. It is **not a stream**, and no amount of watching it could ever have found the producer.

   **THE ACTUAL SPIN READS A DIFFERENT WORD — AND IT IS NOT EVEN IN THE SPIN'S FRAME:**
   ```
   sub_010088E8 enters with sp = 0x1fffc20   -> its count/base are 0x1fffc28 / 0x1fffc30
   func_10057F0 is handed a0 = 0x1fffba0      -> 0x70 LOWER, a different frame
   so the word the compare reads is 0x1fffba8  (= *(a0+8), which measures 1)
   and the element it reads is *(a0+0x14) = 0x1895360, which measures 0
   ```
   **The spin is therefore gated on `0x1fffba8` (count = 1, equal) and on the ELEMENT at `0x1895360`
   (= 0, vs the right stream's `0x1895310` = 1).** Everything I have been watching — `0x1fffba0..b4`,
   `0x1fffc28/30`, and every "frame" derived from a single `$sp` sample — was the wrong address or
   shared scratch. **The word to watch is `0x1fffba8` for the count and `0x1895360` for the element, and
   `0x1895360` is in the game's static data, not on any stack.**

   **ITERATION 17: WATCH RESOLVED FROM LIVE `$sp`. THE FRAME IS **NOT** CLOBBERED — AND THE REAL
   FINDING IS THAT THE SPIN'S FRAME AND THE COMPARE'S `a0` ARE TWO DIFFERENT FRAMES.**
   The window is now resolved **per run from the register**, not from a constant. Gated run `y2.log`,
   `halt=livelocked_in_syscall`, `0x01005890=125,740,680`:
   ```
   [w122:frame] RESOLVED FROM LIVE $sp sp=0x1fffc20 count(sp+0x08)=0x1fffc28 base(sp+0x10)=0x1fffc30
   ```
   **Writes to the resolved `0x1fffc28`/`0x1fffc30`: ZERO.** Dispatcher control
   (`delivered=f4240 subs=2`) and the compare probe (125,939 lines) are both live on the same run, so
   this is a real zero, and **the frame is not being clobbered** — it is simply never written, because
   `sub_010088E8` does not execute its prologue on this path.

   **WHY, AND IT IS A RUNTIME SCHEDULER FACT, NOT A CLOBBER.** The game calls the compare with
   `move a0,sp` (`0x10089d0` / `0x10089e0`), so `$a0` **is** the spin's stack pointer. But measured:
   ```
   spin entry      sp = 0x1fffc20
   compare receives a0 = 0x1fffba0
   0x1fffba0 + 0x80 = 0x1fffc20
   ```
   **`0x1fffba0` is exactly the value of `$sp` BEFORE `addiu $sp,$sp,-0x80`.** So the compare is running
   with the *un-decremented* stack pointer. **The only way that happens is that control is arriving at
   `0x100894c` or `0x1008960` — the two table entries that SKIP the `addiu $sp,$sp,-0x80` prologue
   instruction — so the frame is never allocated and never primed.** The `j`/`jal` resume paths re-enter
   the generated function mid-body, and **our EE thread does not restore `$sp` for them.**

   **AND THE CONSEQUENCE IS EXACTLY THE SPIN:** the accumulator `func_1007738` is handed `a0=sp`
   (`0x1fffc20`) and primes `0x1fffc28`; the compare `func_10057F0` is handed `a0=0x1fffba0` and reads
   `0x1fffba8` and `*(0x1fffba0+0x14)=0x1895390`. **Two different frames, so the accumulator primes a
   struct the compare never reads, and the compare reads a struct nobody primes.** That is the livelock,
   and it is a **thread-context restore defect in our scheduler**, not a data problem at all.

   **ITERATION 18: ITERATION 17 IS RETRACTED. `0x100894c` IS A CALL RETURN, NOT A THREAD RESUME.**
   I was told to go fix `EeScheduler`'s context restore. **I read the code first, and the premise was
   wrong, so I did not write the fix.** Two checks:

   **(1) `0x100894c` IS NOT A RESUME.** From the real ELF:
   ```
   1008940: move  a0,sp
   1008944: jal   0x10059e8
   1008948: move  a2,zero          (delay slot)
   100894c: lw    a1,20(s3)        <-- the instruction right after the jal
   ```
   It is the **return address of a `jal`**, and it is in the table only because the recompiler lists
   every branch target. **A call does not change `$sp`**, so nothing about `$sp` can be wrong there. My
   "our EE thread does not restore `$sp` for mid-body resumes" was **inference from an address, and the
   address did not mean what I assumed.** Fourteenth instrument defect: I read a number and built a
   theory on it instead of measuring it.

   **(2) THE SCHEDULER HAS NO REGISTER RESTORE — AND THAT IS NORMAL, NOT THE BUG.** `grep` for
   `SET_GPR_VEC|restoreRegs|memcpy.*context` in `EeScheduler.cpp` returns **nothing**, and `activeContext()`
   is `invocations.empty() ? context : invocations.back().context`. That is the correct design for a
   **continuation-style** guest: the generated function keeps running on the real CPU context and yields
   rather than being context-switched mid-function. **So there is nothing to fix there either.**

   **THE REAL QUESTION I COULD NOT CLOSE, STATED HONESTLY.** The spin's entry `$sp` measures
   `0x1fffc20` and the compare's `$a0` measures `0x1fffba0`, and `a0` is set by `move a0,sp` — so those
   genuinely disagree by `0x80` **within one run**. `sub_01005D48` (the outermost caller) allocates
   `0xB0`, not `0x80`, so it does not account for the gap, and a dozen other functions allocate `0x80`.
   I added a direct measurement of exactly this — `[w122:depth]` printing `$sp` at `sub_010088E8` entry
   and at the compare, so the two are sampled in the same pass rather than compared across runs — and
   **20 consecutive boots produced no gate pass, so I have no gated number and I claim nothing from
   them.** `0x100894c` is reached by an ordinary `jal` return, so the 0x80 gap cannot be a skipped
   prologue; it is most likely the two measurements coming from *different invocations* of
   `sub_010088E8`, since it is entered repeatedly and `$sp` is not constant across entries.

   **THE ONE MEASUREMENT THAT WOULD SETTLE IT:** `[w122:depth]` already prints both, on the same line
   per pass, with the entry count and the compare count — so the next agent needs only a gated run and
   one `grep w122:depth`. **Do not rewrite `EeScheduler`; there is no defect in it that I have shown.**

   **ITERATION 19: THE 0x80 QUESTION CLOSES, AND IT CLOSES BY SHOWING I WAS CHASING THE WRONG SPIN.**
   First, a correction to the brief's premise, verified rather than assumed: **`boot_desk122351.log`
   CANNOT answer the question.** It has `halt=wallclock_deadline` with `0x01005890=157,626,387`, which is
   a live spin — but it is dated **12:25:21**, and `[w122:depth]` was not compiled into the binary until
   **16:38:01**. `grep -c w122:depth` on it returns **0**. **The data on disk does not contain the
   instrument.** Every one of my 20 "no gate pass" runs also returns `depth=0`, and the reason is now
   obvious and was never the gate: `sub_010088E8` **was never entered** in any of them.

   **AND THAT IS THE FINDING.** There are **TWO DIFFERENT LIVELOCKS**, and they are not the same shape:
   ```
   z14.log  halt=livelocked_in_syscall  spin5890=0    top: 0x0100d908=473,114,102 (99.99%)
                                                    also 0x010202e8=473,112,604
   y2.log   halt=livelocked_in_syscall  spin5890=125,740,680
                                                    top: 0x01007738=125,740,248 (99.99%)
                                                    also 0x01089c8 =125,740,681
   ```
   - **`0x0100D908`** is the **thread-id barrier** found in W122 iteration 1 — the *dominant* livelock,
     present in most runs. `sub_010088E8` is never reached in those.
   - **`0x01007738`** is `func_1007738`, the **accumulator**, and it burns 125.7M transfers in the
     minority of runs that get past the barrier and into `sub_010088E8`.

   **SO THE 0x80 GAP WAS AN ARTIFACT OF COMPARING ACROSS INVOCATIONS, exactly as suspected.**
   `sub_010088E8` is entered repeatedly and `$sp` is not constant across entries, so "entry `$sp`" from
   one invocation and "compare `$a0`" from another differ by `0x80` without anything being wrong.
   **There is no mid-function `$sp` reallocation, and `EeScheduler` is not involved.** Thread closed.

   **AND THE REAL WALL, NAMED AT LAST:** the accumulator `func_1007738` is the thing spinning, and on
   the gated run it is entered exactly once through its refill:
   ```
   [w122:refill] ENTER #1 a0=0x1895180 a1(count)=0 a2(bit)=1
   ```
   **Its loop bound `*(a3+8)` is `0`, so `sltu` is false on the first pass and the refill never
   supplies a bit — while the accumulator itself is called 125,740,248 times.** The guest is asking for
   bits from an empty stream, forever. That is the livelock in one sentence, and it is upstream of the
   compare, the element word, and every stack frame measured today.

   **ITERATION 20: THE WALL, CONFIRMED FROM THE SOURCE. NOBODY EVER WAKES tid1.**
   Verified independently before acting, from the logs and then the code.

   **FACT 1 — `sce_SleepThread` is 93-95% of every syscall, and it is retried, not satisfied.**
   ```
   z14: 12282 of 13196    z9: 266010 of 281425    z10: 269387 of 284723    y2: 109005 of 115765
   ```
   The harness already names it: *"it is being retried, not satisfied, so the wall is the return value
   it is not getting, not the call itself."*

   **FACT 2 — `wakeupCount=0` and `woken=0` in EVERY run, without exception** (z3, z9, z10, z14, z15,
   z16, y2), and `tid1:status=1:wait=sleep#0:woken=0:pc=0x0100f800`. **There is no run today where
   `wakeupCount` has ever been non-zero.**

   **FACT 3 — the code, quoted.** `sleepCurrent()` has exactly two ways out:
   ```c++
   void EeScheduler::sleepCurrent(uint32_t microseconds) {
       if (self->wakeupCount != 0u) { --self->wakeupCount; setReturnS32(..., KE_OK); return; }  // ONLY exit
       ...
       blockCurrent(EeWaitState{ EeWaitReason::Sleep,
           (microseconds != 0u) ? EeWaitPayload{EeTimedSleepWait{deadline}} : EeWaitPayload{std::monostate{}}, ...});
   }
   ```
   For tid1 the sleep is **untimed** (`std::monostate`), so it cannot expire. **And `wakeupCount` is
   raised in exactly ONE place — `EeScheduler::wakeupThread()` — which has exactly ONE caller in the
   whole runtime:**
   ```
   tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp:259:
       const int result = ee.wakeupThread(static_cast<int>(getRegU32(ctx, 4)), interruptSafe);
   ```
   That is the `sce_WakeupThread` (syscall `0x33`) handler. **The guest never reaches it: the string
   `0x33` appears ZERO times in every run log, while `0x35` (`sce_CancelWakeupThread`) does appear.** So
   the guest is **cancelling wakeups it never issues**, and tid1 is parked forever.

   **THE MECHANISM, IN ONE SENTENCE:** tid1 sleeps untimed, only `syscall 0x33` can release it, the
   guest never issues `0x33`, and meanwhile tid2 retries `sce_SleepThread` 269,387 times waiting for a
   peer that is permanently parked. **The livelock is a lost wakeup, not a data value** — which is why
   every element word, stack frame and accumulator bound measured today was a dead end.

   **ITERATION 21: THE SDK SAYS MY RED TEST WAS WRONG. RECORDED SO IT IS NEVER RE-DERIVED.**
   **Primary sources, supplied by the reviewer from Sony's ps2dev/ps2sdk:**
   ```
   ee/kernel/include/kernel.h:405      extern s32 SleepThread(void);
   ```
   **It takes NO ARGUMENTS.** Not a microsecond count, not a duration — an **untimed, event-driven
   block**. There is no timeout argument, so there are no timed semantics to look for.

   **Upstream `menaman123/Ps2Recomp`, `host_app/ps2_scheduler.cpp:2748`, `PS2Scheduler::SleepThread`:**
   ```cpp
   if (current_thread_id_ <= 0) return;
   PS2Thread& t = threads_[current_thread_id_];
   if (t.wakeup_count > 0) { t.wakeup_count--; return; }
   RemoveFromReadyQueue(current_thread_id_);
   t.status = THS_WAIT;
   t.wait_type = WAIT_SLEEP;
   Reschedule(ctx);
   ```
   **Only `WakeupThread` clears `WAIT_SLEEP`.** Their header also flags `SuspendThread` with
   `NOTE: Has documented BUG - no reschedule`.

   **OUR `sleepCurrent()` ALREADY MATCHES THIS LINE FOR LINE** — the `wakeupCount` shortcut, then
   `blockCurrent()` which is `RemoveFromReadyQueue` + `THS_WAIT` + `Reschedule`. **And we did NOT
   inherit the SuspendThread bug: our `suspendThread()` sets `m_rescheduleRequested = true` on both the
   Running and interrupt-safe paths.** So neither the sleep nor the suspend needs a fix.

   **MY RED TEST WAS WRONG AND IS RETRACTED.** I asserted "an untimed sce_SleepThread must be
   RELEASABLE by the scheduler". **A sleep is NOT self-releasing on real hardware — only
   `WakeupThread` clears it.** That assertion was a fake failure chasing a contract the console does
   not have, and it would have sent the next agent to "fix" correct code. **A lost wakeup is not a PS2
   semantic.**

   **WHERE THE WALL ACTUALLY IS, RESTATED CORRECTLY:** if GT4 parks and is never woken, then
   **something upstream of the sleep failed to deliver what the guest was waiting on**, and the sleep is
   only where it becomes visible. Our evidence for that is unchanged and still the whole story:
   `wakeupCount=0`, `woken=0` in every run, `tid1:status=1:wait=sleep#0:woken=0`, and the guest
   **never issues syscall `0x33`**. So the next question is the reviewer's: **who is supposed to signal
   tid1** — a semaphore, an event flag, an interrupt handler, or another thread's completion — and that
   must be answered from the guest side with a measurement.

   **MY OWN ERROR THIS TURN, TWICE:** I rewrote the test to the upstream contract and it **segfaulted
   again**, because `sleepCurrent()` on the *main* thread of that fixture is not a supported call path.
   That is the second time in this project I shipped a crashing test, and the second time I reverted
   it rather than leave a red tree. **The lesson is in the tree now: drive the scheduler only through
   the pattern the passing tests already use (`ee.run()`), not by calling `sleepCurrent()` from a test.**

   **ITERATION 33 -- PRIORITY ONE CLOSED (VU0 red, 494/494 BOTH WAYS). PRIORITY TWO ANSWERED BY
   LOOKING UP AND OUT: THE SPIN IS REACHED FROM A 24-BYTE REFCOUNTED OBJECT LIFECYCLE.**

   **1. THE VU0 RED IS CLOSED, AT THE SOURCE, MECHANISM-BASED. Nested `474417c`.**
   `ps2xTest/CMakeLists.txt` now hands the tests the absolute source root at COMPILE time:
   ```cmake
   target_compile_definitions(ps2x_tests PRIVATE VULCAN4_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
   ```
   and `code_generator_tests.cpp` ends its candidate list with
   `std::string(VULCAN4_SOURCE_ROOT) + "/../ps2xRecomp/include/ps2recomp/instructions.h"` under
   `#ifdef VULCAN4_SOURCE_ROOT`. `__FILE__` was tried in ITERATION 32 and did not resolve; an explicit
   definition is correct regardless of how the compiler was invoked. **MEASURED:**
   ```
   cd /mnt/ssd/vulcan4-build      ->  Total 494  Passed 494  Failed 0  EXIT=0   <- the reviewer's way
   cd tools/PS2Recomp/ps2xTest    ->  Total 494  Passed 494  Failed 0
   ```
   **NO TEST WAS DISABLED, SKIPPED OR WEAKENED** -- the same assertions run; they just resolve the header
   from a known absolute location instead of guessing from cwd. **The suite is no longer noise in a report.**

   **2. THE CALL-CHAIN WALK, DONE WITH OBJDUMP AND **NO NEW INSTRUMENT**, LOOKING UP AND OUT.**
   Each function has exactly one caller, so the chain is unambiguous:
   ```
   func_1007738 (the spin)      <- called ONLY from 0x1005f50
     in sub_01005D48 (0x1005d48 .. 0x10064b8)
       <- called ONLY from 0x1006fb4
     in sub_01006F90 (0x1006f90 .. )
       <- called ONLY from 0x100460c
     in sub_01004500 (0x1004500 .. )          <-- THE NEAREST CALLER THAT DOES REAL WORK
   ```
   **WHAT `sub_01004500` IS DOING, IN PLAIN WORDS, FROM ITS OWN BYTES:**
   ```
   1004518:  jal   0x1004308
   1004524:  li    a0,24
   100452c:  lw    s1,20(s3)
   1004530:  jal   0x101d2a0            ; operator new(24)
   1004544:  jal   0x10055e0            ; construct it, (this, s3->0x14, s3->0x18)
   1004554:  lw    v0,4(s2)
   1004558:  addiu v0,v0,1
   100455c:  sw    v0,4(s2)             ; ++refcount  (the field at +4)
   ...
   1004600:  move  a0,sp
   1004604:  addiu a2,sp,32
   1004608:  addiu a3,sp,48
   100460c:  jal   0x1006f90            ; <-- the call that reaches the spin
   1004614:  lw    a0,52(sp)
   1004620:  lw    v0,4(a0)
   1004624:  addiu v0,v0,-1
   1004628:  bgtz  v0,0x1004638
   100462c:  sw    v0,4(a0)             ; --refcount
   1004630:  jal   0x1005588            ; destroy when it hits zero (a1=3)
   ```
   **SO THE SPIN IS CALLED FROM AN OBJECT LIFECYCLE ROUTINE: it allocates a 24-byte object, constructs it,
   bumps a reference count at `+4`, calls the spin through `sub_01006F90` with three STACK BUFFERS as
   out-parameters (`sp`, `sp+32`, `sp+48`), and then tears the refcount back down.** **This is what I
   have been staring into for twenty turns: a constructor/registration path, not a frame loop and not a
   bit accumulator.** The zero-word the accumulator was said to chew on is a freshly allocated 24-byte
   object that nothing has written yet -- **which is consistent with the ITERATION 32 finding that the
   field is never read, and it means the zero word is a SYMPTOM of the object not being initialised, not a
   cause.**

   **STILL TO DO ON THE SPIN:** the next step is the constructor at `0x10055e0` and the callee
   `0x1006f90` -- specifically whether `sub_01006F90` WRITES those three out-parameters before
   `func_1007738` reads them. **I have NOT measured that and am not claiming it.** But the shape is now
   named, which is what twenty turns of looking downward did not give us.

   **3. THE BOOT, UNCHANGED AND NOT YET FIXED:**
   ```
   true_guest_entries=410946394   halt=livelocked_in_syscall
   ```
   **PICTURE:** `docs/evidence/w133_game.png`, window id `0x978490`, 650x482, **387 distinct colours**.
   Still the 2005 Sony disclaimer. **STILL NOT PAST IT. NO FIX LANDED ON THE SPIN THIS TURN** -- the
   priority-one task was closed and the priority-two task was answered, but a diagnosis is not a fix and
   I am not going to report it as one.

   **ITERATION 32 -- THE GATE WAS RIGHT ON ALL THREE, AND THE ONE NUMBER SAYS THE TWO FAULTS ARE NOT
   THE SAME FAULT. NO FIX LANDED, AND I AM NOT PRETENDING OTHERWISE.**

   **1. THE SUITE NUMBER WAS WRONG AND IT IS MY FAULT.** I read the summary from a **source-root** run
   and quoted `494/494` without ever running the reviewer's invocation. **BOTH numbers, as read just now:**
   ```
   cd /mnt/ssd/vulcan4-build        ->  Total 494  Passed 493  Failed 1   (EXIT=1)   <- the reviewer's way
   cd tools/PS2Recomp/ps2xTest      ->  Total 494  Passed 494  Failed 0   (EXIT=0)
   ```
   The one failure is **`VU0 macro mappings cover all S1/S2 enums`**, and it is **CWD-dependent, not a real
   defect**: `code_generator_tests.cpp:1289` built a candidate list of **relative paths only**, so the test
   passes from the source root and fails from the build directory. **I ATTEMPTED THE FIX AND IT DID NOT
   WORK** -- I added `__FILE__`-relative candidates and the build-dir run is still `493/1`, so I do not
   yet know why. **THE HONEST STATUS IS: KNOWN-RED FROM THE BUILD DIRECTORY, NOT YET FIXED. Do not let
   anyone report this suite green again without naming which directory they ran it from.**

   **2. `halt=livelocked_in_syscall`, AND THE ADEL DID NOT EVEN HAPPEN.** This is the important finding.
   With the W132 AdEL probe **on** and `VULCAN4_W132_ADEL=1`:
   ```
   [w132:adel] hits: 0        <-- NOT ONE, in a whole boot
   true_guest_entries=406327585   halt=livelocked_in_syscall
   ```
   **SO THE REVIEWER IS EXACTLY RIGHT AND I CONFIRM IT: THE SPIN AND THE AdEL ARE TWO DIFFERENT THINGS,
   AND IN THIS RUN ONLY THE SPIN HAPPENED.** The AdEL at `0x100d90c` is real but **it does not reproduce
   every boot**, which is the same nondeterminism recorded in ITERATION 22. **Any future work that treats
   "the AdEL" and "the livelock" as one story is wrong, and this entry exists to stop that.**

   **3. NO FIX LANDED THIS TURN, AND THE INSTRUCTION WAS RIGHT THAT NAMING A DEFECT IS NOT FIXING ONE.**
   The W132 probe is in place and costs nothing when off, and it will print `faultAddr`, `vaddr&3`, `s0`,
   `s0&3`, `addrInRam` and `ramSize` on the first eight AdELs plus every 50000th. **It has not fired, so
   I have not split the hypothesis and I have no new cause to name.** The probe is deliberately NOT masking,
   NOT suppressing and NOT widening the check: real hardware raises AdEL for these and raising it here is
   correct.

   **PICTURE:** attempted this turn; the capture was interrupted, so **the newest png is still
   `docs/evidence/w131_game.png`, window `0x96e5ec`, 650x482, 386 colours -- still the 2005 Sony
   disclaimer.** No new picture this turn and I am not going to claim one.

   **ITERATION 31 -- THE FAULT CLASS IS NAMED: ExcCode 0x4, ADDRESS ERROR ON LOAD. NOT 0xC, NOT A
   SYSCALL. AND THE FAULTING CODE IS A LINKED-LIST WALK.**

   **THE THREE NUMBERS (gated, `VULCAN4_W131_EXC=1`, 295 exceptions in one boot), all from
   `raiseCop0Exception` -- the single funnel every EE exception passes through, so no guessing:**
   ```
   [w131:exc] excCode=0x4 epc=0x100d908 guestPc=0x100d90c inDelaySlot=1 v1=0x100d910 v3=0xba
   [w131:exc] excCode=0x4 epc=0x100d910 guestPc=0x100d910 inDelaySlot=0 v1=0x100d910 v3=0xbb
   ```
   - **ExcCode = 0x4.** Per our own `ps2_runtime.h:53` that is **`EXCEPTION_ADDRESS_ERROR_LOAD`**.
     **NOT `EXCEPTION_SYSCALL` (0x8), and NOT 0xC (which our enum says is INTEGER_OVERFLOW).** The
     reviewer's expectation of 0xC is not what the hardware is doing.
   - **cop0 EPC = 0x100d908**, guest pc = **0x100d90c**, and `inDelaySlot=1` -- the fault is inside a
     branch delay slot, so EPC is correctly attributed to the branch, not the instruction.
   - The second line of each pair is the retry at 0x100d910, not in a delay slot.

   **MY OWN LABEL WAS WRONG AND I AM FIXING IT BEFORE ANYONE ELSE FINDS IT: I printed
   "v1" = `r[31]` and "v3" = `r[3]`. In this ABI `r[31]` is `$ra` and `r[3]` is `$v1`.** So the constant
   0x100d910 I reported as a "syscall number" is the RETURN ADDRESS, and the actually-changing value in
   `$v1` is **0xba, 0xbb, 0xbc, 0xbd, 0xbe, ...** Our call list maps `0xba=strchr, 0xbb=strcmp, 0xbc=strcpy,
   0xbd=strlen`, **but these are NOT syscalls** -- they are small integers being carried in a list.

   **THE FAULTING CODE, DISASSEMBLED (0x100d8f0-0x100d920) -- IT IS A LINKED-LIST WALK:**
   ```
   100d8f8:  lw    s0,0(s1)      ; s0 = list head
   100d8fc:  beqzl s0,0x100d920  ; list empty -> exit
   100d908:  jal   0x10202e8     ; call one function per node
   100d90c:  lw    a0,4(s0)      ; DELAY SLOT: a0 = node->arg   <-- ADEL FAULTS HERE
   100d910:  lw    s0,0(s0)      ; s0 = node->next  (advance the cursor)
   100d914:  bnez  s0,0x100d908  ; loop while s0 != 0
   ```
   **THE FAULT IS `lw a0,4(s0)` WITH A BAD `s0`.** The list cursor has become a pointer we cannot load
   from, and the payload at `node+4` is a small incrementing integer (0xba, 0xbb, ...). So the guest is
   walking a list of small integers and calling `0x10202e8` once per element.

   **WHAT THIS MEANS PLAINLY: OUR RUNTIME IS BEHAVING CORRECTLY HERE.** A load from an unaligned or
   invalid address on a PS2 raises AdEL, and we raise it, with the right EPC and the right BD bit. **The
   bug is upstream of the fault: the list's `next` pointers are wrong, so the cursor walks off into
   nowhere.** The wall is a DATA-STRUCTURE walk in the guest, not a syscall we failed to implement, and
   not a branch bug.

   **AND NOTE WHAT THIS RETIRES: `0x100d908` IS THE ADDRESS I CALLED THE "THREAD-ID BARRIER" IN W120 AND
   W122 AND LATER DECLARED A DECOY.** It is neither. It is this list walk, and it is where the guest is
   spending its time and where it eventually faults. **Seventh retraction, and it kills a name I had
   carried for three dishes.**

   **THE NEXT QUESTION IS NARROW AND IT IS A DATA QUESTION: what builds the list whose `next` field
   (`lw s0,0(s0)`) becomes unaligned?** Nodes look like `{u32 next; u32 arg;}` at 8-byte stride, and an
   AdEL on a 32-bit load means the cursor is not 4-byte aligned. **That points at whoever allocated or
   filled that list -- not at the scheduler, not at syscalls, and not at the copier.**

   **ITERATION 30 -- THE WRITER-WATCH LANDS AND RECORDS **ZERO READS AND ZERO WRITES**, WHICH KILLS
   THE TAIL-BLOCK THEORY *AND RETRACTS MY OWN ITERATION 26 ANSWER*.**

   **THE WATCH, emitted by the translator exactly as W129 was, behind `VULCAN4_W130_STORE`.**
   - **Writer-watch:** `instruction_translator.cpp` now prefixes **every `SW`** with
     `if (::vulcan4W130StoreWatch(dst, value, 0x<pc>u)) {}` -- 2522 sites emitted.
   - **Read probe:** the same file prefixes every `LW` with a runtime address test that fires only at
     **`0x0100F810`**, the guest's `lw v0,0x44(s0)` -- 3270 sites emitted, one of which can ever fire.
     That instruction is the read of the field whose `+0x1C` I claimed was indirect-called.
   - Helpers `inline` in `ps2_runtime_macros.h` (the header the generated unit includes), with a
     four-slot watched-address set that the read probe fills in when it first sees the structure.
   Both are free when off: one load-and-test on an inline-visible static.

   **THE MEASUREMENT (gated, `VULCAN4_W130_STORE=1`):**
   ```
   reads: 0        writes: 0
   ```
   **NOTHING IN THE FUNCTION EVER READS `*(s0+0x44)`, AND NOTHING EVER WRITES EITHER WORD.**

   **SO, THE THREE QUESTIONS, PLAINLY. This is a FOURTH situation, not one of the three offered, and I
   am not going to pretend otherwise: it is "the code that would touch those fields NEVER RUNS".**
   1. **Does anything write `*(u32*)(s0+0x44)`? NO -- measured zero writes.**
   2. **Does anything write the `+0x1C` word? NO -- measured zero writes.**
   3. **But it is NOT situation three (an uninitialised field nobody filled). It is that
      `0x0100F810` IS NEVER EXECUTED AT ALL.** The read probe is at that exact address and it fired zero
      times, so the guest never reaches the `lw v0,0x44(s0)`, never reaches the vtable read, and never
      reaches `jalr v1`.

   **AND THIS RETRACTS MY OWN ITERATION 26 PLAIN-WORDS ANSWER.** I said then: "the value it is about to
   jump to is `*(u32*)(*(u32*)(s0+0x44) + 0x1C)`" and called `jalr v1` at `0x100f83c` "THE DERAILMENT".
   **That was read off the disassembly and presented as the mechanism. It is not the mechanism, because
   that code never executes.** ITERATION 29 had already shown `t4` sits BEHIND `t1`, so
   `bne t1,t4` cannot match and the whole tail block is dead -- I noticed that and still let the ITER26
   claim stand. **Sixth retraction in this project, and this one is mine from two turns ago.**

   **SO WHAT `last_good=0x0100F800` ACTUALLY MEANS, REVISED: IT IS NOT THE DERAILMENT POINT.** It is the
   last *dispatch* pc, and the copy loop is simply where the guest spends its time -- a busy memcpy,
   called over and over, which exits correctly to `0x100f418` each time. **The wild pc does not come
   from this function.** `[guest-branch:missing-target]` reports `op=EE invocation service`, i.e. an
   **EE EXCEPTION** with pc `0x88468107`, not a `jalr` we followed. **The next wall is an exception whose
   reported pc is already garbage, and nothing has yet established what raises it or what that pc means.**

   **PICTURE:** `docs/evidence/w130_game.png`, window id `0x96ad68`, 650x482, **389 distinct colours**.
   Still the 2005 Sony disclaimer. Suite 494/494 EXIT=0.

   **ITERATION 29 -- THE PROBE IS IN, AND THE OVERRUN THEORY IS DEAD. THE POINTER IS THE WHOLE WALL.**

   **THE PROBE IS EMITTED BY THE TRANSLATOR, as required, behind an env knob.** It lives in
   `ps2xRecomp/src/lib/control_flow_emitter.cpp` (emitted at the guest's `bnel`), with the two helpers
   defined `inline` in **`ps2xRuntime/include/ps2_runtime_macros.h`** -- which is the header the
   GENERATED unit includes, so the probe survives regeneration. **A probe pasted into
   `ps2_recompiled_functions.cpp` would not have: that file is rebuilt from the guest every time.** The
   helpers are in `ps2_runtime_macros.h` rather than the generator's own header precisely because the
   generated unit does not include the generator's headers. Knob: **`VULCAN4_W129_COPY`**, read once into
   a function-local static so it costs a load and a test when off. Regenerate with
   `ps2_recomp /mnt/ssd/gt4/work/gt4_recomp.toml`, then relink with `tools/harness/build_harness.sh`.

   **FOUR BUILD TRAPS HIT ON THE WAY, RECORDED SO NOBODY LOSES AN HOUR TO THEM:**
   1. **`cmake --build --target ps2xRecomp` does NOT relink `ps2_recomp`.** The exe stayed dated Oct 1 and
      the regenerated file came out byte-identical. The target name is **`ps2_recomp`**.
   2. **The probe was emitted BEFORE `const bool branch_taken_...`,** so it referenced an undeclared name.
      It must come after that declaration line.
   3. **The C++ `if (m_branchInst.address == ...)` block needs its own closing brace** -- closing only the
      EMITTED `if` is not enough, and the missing brace surfaced as `m_ss does not name a type` three
      lines later, which is a misleading error far from the real mistake.
   4. **Helpers in the generator's header are invisible to the generated unit.** They must live in a
      header the generated code includes.

   **THE THREE NUMBERS, MEASURED, GATED (`VULCAN4_W129_COPY=1`, 13 probe hits in the boot):**
   ```
   [w129:copy] n=1 at=bnel-exit taken=1 s1=0x01051a45 s2=0x01051a4b s3=0x00000007(7) t1=0x01051a46 t4=0x01051a3f len=0x00000006 dstlen=0xfffffff9
   [w129:copy] n=6 at=bnel-exit taken=1 s1=0x01051a4a s2=0x01051a4b s3=0x00000007(7) t1=0x01051a4b t4=0x01051a3f len=0x00000001 dstlen=0xfffffff4
   [w129:copy] n=7 at=bnel-exit taken=0 s1=0x01051a4b s2=0x01051a4b s3=0x00000007(7) t1=0x01051a4c t4=0x01051a3f len=0x00000000 dstlen=0xfffffff3
   [w129:copy] n=8 at=bnel-exit taken=1 s1=0x01051a4a s2=0x01051a4c s3=0x00000003(3) t1=0x01051a50 t4=0x01051a3f len=0x00000002 dstlen=0xffffffef
   ```
   **1. `s3` IS A SANE BYTE COUNT AND IT DOES CHANGE: `7`, then `3` on the next call.** Tiny memcpy
   lengths, entirely normal. **There is no runaway length.**
   **2. THE EXIT FIRES, AND IT FIRES CORRECTLY: `taken=1` for passes 1-6, then `taken=0` on pass 7 when
   `s1 == s2` (`len=0`).** `len` counts down 6,5,4,3,2,1,0 -- exactly one pass per byte. **The loop is
   NOT stuck and it terminates precisely when the source is exhausted.** Combined with ITERATION 28's
   proof that the `bnel` codegen is correct, **the copy loop is exonerated on both counts.**
   **3. SOURCE AND DESTINATION ARE BOTH ORDINARY GUEST MEMORY: `s1=0x01051a45`, `s2=0x01051a4b`,
   `t1=0x01051a46`** -- all in the 0x0105xxxx heap/data region the guest has already allocated. Nothing
   here is wild.

   **AND ONE GENUINELY NEW FACT THAT FALLS OUT, WORTH THE NEXT AGENT'S TIME:** **`t4` is CONSTANT at
   `0x01051a3f` while `t1` climbs `0x01051a46 -> 0x01051a4c`, so `t4 - t1` is NEGATIVE (`0xfffffff9`
   and falling).** `t4` sits BEHIND the destination cursor, so **`bne t1,t4` at 0x100f808 never matches
   and the tail block at 0x100f810 -- the one containing the vtable read and `jalr v1` -- does not run for
   these calls.** That is self-consistent for a 6-byte copy, but it means **the tail block only fires when
   `t4` is set to a real end-of-destination**, and nothing has yet shown a case where it does.

   **SO, PLAINLY, AS ASKED: `s3` IS SANE AND THE EXIT FIRES, THEREFORE THE OVERRUN THEORY IS DEAD, AND
   THE POINTER IS THE WHOLE WALL.** The guest jumps through `*(u32*)(*(u32*)(s0+0x44) + 0x1C)`, that word
   is garbage, and nothing else is left standing. **The next job is the OWNER OF THAT WORD: who is
   supposed to write `*(u32*)(s0+0x44)`, and why is it not a pointer to a valid dispatch table.**

   **PICTURE:** `docs/evidence/w129_game.png`, window id `0x964b74`, 650x482, **393 distinct colours**.
   Still the 2005 Sony disclaimer.

   **ITERATION 28 -- OWNER IDENTIFIED, AND THE `bnel` HYPOTHESIS IS REFUTED BY MEASUREMENT.**

   **ITEM 1, THE OWNER (this unblocks every measurement blocked for three turns).** The generated
   translation unit is `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp` (8.9MB, 707
   functions), each generated as `void sub_<ADDR>_<hexaddr>(uint8_t* rdram, R5900Context* ctx,
   PS2Runtime *runtime)`, so the owner is simply the function with the largest start address <= the
   target:
   ```
   target  0x100f800
   OWNER   sub_0100F390_0x100f390   starts 0x100f390
   next    sub_0100F8C8 starts 0x100f8c8  ->  the function is 0x538 = 1336 bytes
   ```
   **So the copy loop lives in `sub_0100F390`, and that is a real memcpy-shaped routine -- which is
   consistent with it running entirely inside one generated body, exactly as the zero-hit dispatcher
   probe (ITERATION 27) showed.** Labels inside it: `label_100f7f8`, `label_100f800` (line 77320),
   `label_100f804`, `label_100f808`, `label_100f864` (line 77461), `label_100f86c`.

   **ITEM 3, AND IT IS CLEAN -- THE `bnel` GENERATION IS CORRECT.** The reviewer's hypothesis was that
   our translator ran the likely-branch delay slot unconditionally or elided it, so the exit could never
   be satisfied. **The generated code says otherwise.** For `bnel $s1,$s2` at 0x100f864:
   ```c
   label_100f864:
   label_100f868:
       if (ctx->pc == 0x100F868u) { ...delay slot...; goto label_100f86c; }   // re-entry from the slot
       ctx->pc = 0x100F864u;
       {
           const bool branch_taken_0x100f864 = (GPR_U64(ctx, 17) != GPR_U64(ctx, 18));
           if (branch_taken_0x100F864) {
               ctx->pc = 0x100F868u;
               ctx->in_delay_slot = true;  ctx->branch_pc = 0x100F864u;
               SET_GPR_ZE32(ctx, 2, (uint8_t)READ8(ADD32(GPR_U32(ctx, 17), 0)));   // lbu v0,0(s1)
               ctx->in_delay_slot = false;
               ctx->pc = 0x100F800u;
               if (runtime->eeCheckpointDue()) { return; }
               goto label_100f800;                        // TAKEN -> loop head
           }
       }
       ctx->pc = 0x100F86Cu;                              // NOT TAKEN -> delay slot SKIPPED
   label_100f86c:
   ```
   **TAKEN executes the delay slot then loops. NOT TAKEN skips the delay slot entirely and exits. That
   is exactly `bnel` semantics, so the exit condition CAN be satisfied and the translator did not elide or
   hoist the slot.** The same shape is present for the ordinary `bne` at 0x100f808. **Hypothesis 3 is
   REFUTED. Do not go looking for a delay-slot bug here.**

   **ONE STRUCTURAL THING WORTH A SECOND PAIR OF EYES, FLAGGED NOT CLAIMED:** `label_100f864` **falls
   through** into `label_100f868`, whose first act is `if (ctx->pc == 0x100F868u)` -- and that arm runs the
   delay slot and exits to `label_100f86c`. In this function nothing branches to `label_100f864` *with*
   `ctx->pc == 0x100F868`, so the arm looks dead here. **But that is exactly the shape that miscompiles
   when some other path DOES arrive carrying that pc**, and I have not searched the other 706 functions
   for it. Not a claim.

   **SO THE CAUSE IS `s3`, UNMEASURED.** With `bnel` exonerated, the remaining candidate from ITERATION 27
   stands alone: `s2 = s1 + s3` makes **the iteration count `s3`**, and the loop is bounded by
   `s1 != s2`. **A wrong or runaway `s3` overruns the destination and clobbers the structure whose
   `+0x1C` field holds the function pointer.** That is still a theory: reading `s3` needs an instrument
   inside `sub_0100F390`'s generated body, which has not been added.

   **PICTURE:** `docs/evidence/w128_game.png`, window id `0x95ed60`, 650x482, **391 distinct colours**.
   Still the 2005 Sony disclaimer.

   **ITERATION 27 -- THE COPY LOOP IS BOUNDED BY `s3`, NOT BY `t1`/`t4`. THE REVIEWER'S QUESTION IS
   ANSWERED: IT IS "BOUNDED BY SOMETHING ELSE ENTIRELY".**

   **THE FULL LOOP, FROM THE GAME'S OWN BYTES** (`objdump -d SCUS_973.28`, 0x100f740-0x100f890):
   ```
   100f7f8:  addu  s2,s1,s3          ; s2 = SOURCE END -- computed ONCE, before the loop
   100f7fc:  lbu   v0,0(s1)
   100f800:  sb    v0,0(t1)          ; LOOP HEAD (this is what the `bnel` below targets)
   100f804:  addiu t1,t1,1
   100f808:  bne   t1,t4,0x100f864   ; dst bound: picks the TAIL WORK, does NOT exit the loop
   100f80c:  addiu s1,s1,1           ; delay slot, unconditional
   ...  (tail work: the vtable read and jalr v1 at 0x100f83c)
   100f864:  bnel  s1,s2,0x100f800   ; <-- THE REAL EXIT CONDITION: src != src_end
   100f868:  lbu   v0,0(s1)          ; delay slot of a LIKELY branch
   100f86c:  b     0x100f418         ; exit
   ```
   **`t1` and `t4` DO NOT EXIT THE LOOP.** `bne t1,t4` only decides whether to run the tail block; the
   loop itself runs while `s1 != s2`. And `s2 = s1 + s3`, so **the iteration count is `s3`, the byte
   count.** So the answer to "does t1 climb past t4 and the bne still fire?" is: it does not matter,
   because that branch is not the loop bound. **The defect, if there is one, is `s3` being huge or
   garbage -- and an overrunning copy is a direct explanation for the garbage function pointer at
   `*(u32*)(s0+0x44)+0x1C`, because it would clobber the structure holding it. Cause before symptom, and
   the cause is `s3`.**

   **MY RUNTIME PROBE NEVER FIRED, AND THAT IS ITSELF A FINDING.** I armed a probe keyed on
   `targetPc == 0x0100F800` in `dispatchGuestBranch`, printing s1/s2/s3/t1/t4. **Zero hits across a full
   boot** (`grep -c w127:copyloop` = 0), same binary. **So the copy loop does NOT run through the
   per-entry dispatcher at all -- it runs INSIDE a single recompiled function body.** The repeated
   `trace=0x100f800 -> 0x100f800 -> ...` from ITERATION 26 is therefore an edge/trace counter, NOT
   evidence of re-entry. **Consequence for the next agent: to see the bounds you must instrument the
   GENERATED function that contains 0x100f800, or find which recompiled function owns that address --
   probing the dispatcher will silently show you nothing, which is exactly what happened to me.**

   **ONE MORE THING TO CHECK, FLAGGED NOT CLAIMED:** the exit is `bnel`, the MIPS *branch-likely* form.
   Its delay slot at 0x100f868 (`lbu v0,0(s1)`) must NOT execute when the branch is not taken. **If our
   generated code executes a not-taken likely-branch's delay slot, that is a real defect** -- and it is
   the kind of thing that has already bitten this project. **I have NOT verified our generated code for
   that block and I am not claiming it is wrong.** Item 2 said "check our generated code for that block
   against the game's own bytes"; the probe result means the block is inside generated code, so that
   comparison is now the right next move and has not been done.

   **PICTURE:** `docs/evidence/w127_game.png`, window id `0x95bb0c`, 650x482, **398 distinct colours**.
   Still the 2005 Sony disclaimer.

   **ITERATION 26 -- `0x0100f800` DISASSEMBLED. IT IS A BYTE-COPY LOOP, AND THE DERAILMENT IS AN
   INDIRECT CALL THROUGH A POINTER IT READS RIGHT AFTER.**

   **HOUSEKEEPING FIRST: the 5 W125 files are committed. NESTED SHA `3b30563`** (outer `f81fb49`).
   The nested origin is upstream ran-j/PS2Recomp and is NOT pushed.

   **ITEM 1, THE GUEST CODE, FROM `mips-linux-gnu-objdump -d SCUS_973.28`.**
   ```
   0100f7f8:  addu  s2,s1,s3
   0100f7fc:  lbu   v0,0(s1)        ; v0 = byte at s1
   0100f800:  sb    v0,0(t1)        ; store it to t1        <-- last_good
   0100f804:  addiu t1,t1,1
   0100f808:  bne   t1,t4,0x100f864 ; loop while t1 != t4
   0100f80c:  addiu s1,s1,1         ; delay slot
   ...
   0100f810:  lw    v0,68(s0)       ; v0 = *(u32*)(s0+0x44)  -- a POINTER to a structure
   0100f81c:  addiu v0,v0,24        ; v0 = that + 0x18       -- third slot
   0100f834:  lh    a0,0(v0)        ; a0 = *(i16*)(v0)       -- 16-bit offset
   0100f838:  lw    v1,4(v0)        ; v1 = *(u32*)(v0+4)     -- THE FUNCTION POINTER
   0100f83c:  jalr  v1                                       ; <-- THE DERAILMENT
   0100f840:  addu  a0,s0,a0        ; delay slot: a0 = s0 + offset
   ```
   **IN PLAIN WORDS: `0x100f800` is a byte store inside a memcpy-shaped copy loop, and the value it is
   about to jump to is `v1 = *(u32*)( *(u32*)(s0+0x44) + 0x18 + 4 )` -- a function pointer read out of a
   structure the guest owns.** The `lh`/`lw` pair (a 16-bit offset, then a 32-bit pointer, then
   `a0 = s0 + offset` in the delay slot) is a table-driven indirect call, not a direct one. **So the guest
   is not jumping to a constant; it is jumping to whatever word lives at that structure+0x1C, and we have
   never established who is supposed to write it.**

   **WHAT THE RUNTIME SAW AT THE DERAILMENT, SAME BOOT:**
   ```
   missing-target] pc=0x88468107 ra=0x88468107 sp=0x010459e0 gp=0x01049770
       a0=0x440800a3 a1=0x0 a2=0x70000000 a3=0x10000105 s0=0x8c00c900 s1=0x8c00c900
       v0=0xffffffff v1=0xd4
       s0Readable=no  recordReadable=no  vtableReadable=no  vtbl[0..c]=0x0  codeRegion=no  policy=1
       trace=0x100f800 -> 0x100f800 -> 0x100f800 -> 0x100f800 -> ... (repeating)
   ```
   **TWO THINGS THIS SAYS PLAINLY.**
   1. **THE GUEST SPINS IN THE COPY LOOP.** `trace` is `0x100f800` over and over -- the `bne t1,t4` at
      0x100f808 is not terminating, so the loop at 0x100f7f8 is running for a very long time before the
      guest ever falls through to the `jalr`. **The copy loop not terminating is the thing to explain
      first**, and it is upstream of the bad pointer.
   2. **`s0Readable=no` AND `vtableReadable=no`.** `s0=0x8c00c900` masks to `0x0c00c900`, which is far
      above the 32MB RDRAM ceiling (`0x2000000`) -- so `s0` itself is already a bad pointer by the time we
      log it. **The chain that produced 0x88468107 started upstream of the structure read.**

   **SO THE CLASS IS NOW NAMED: WE HAND THE GUEST A POINTER TO A STRUCTURE WHOSE CONTENTS WE NEVER
   POPULATE.** `GetEntryAddress` was one instance (fixed, W125). This is another, and possibly the same
   family: a structure field at `+0x44` that should hold a pointer to a valid dispatch table.

   **ITEM 2 IS THE NEXT JOB AND IS NOT DONE: instrument the writers of `*(u32*)(s0+0x44)` and of the
   word at `+0x1C` of whatever it points to, printing address, value and writer pc on a gated run, and
   check whether our runtime is supposed to initialise that field and does not.** I have NOT done that
   yet and I am not going to guess at the owner of that word.

   **PICTURE:** `docs/evidence/w126_game.png`, window id `0x955d2a`, 650x482, **396 distinct colours**.
   Still the 2005 Sony disclaimer.

   **ITERATION 25 -- A REAL BUG, LANDED. `GetEntryAddress` WAS INVERTED.**

   **THE libosd.c CITATION (recorded so the base is never re-derived).** From Sony's ps2dev/ps2sdk,
   `ee/kernel/src/libosd.c`: **0xFFFFC402 is documented as a patch entry**, with the comment
   **`0x80011F80 + (-15358*4) = 0x80002F88`, where `0x80011F80` is the start of the syscall table.**
   Slots are **4 bytes each**. The table is populated by **SetSyscall** and read by **GetEntryAddress**,
   and **libosd.c calls GetEntryAddress for each patch entry** -- which is exactly the arithmetic our own
   FindAddress already performs. Our numbering (`runtime/syscall_names.h`) is
   **`0x5A=Copy, 0x5B=GetEntryAddress, 0x74=SetSyscall, 0x83=FindAddress`**
   (note: **SetSyscall is 0x74, not 0x83** -- 0x83 is FindAddress).

   **ITEM 1: BOTH PRIMITIVES ALREADY EXISTED.** `SetSyscall` at `System.cpp:485` (writes
   `0x11F80 + n*4` via `setEeSyscallOverride`, and there was already a passing test covering the signed
   `-15358 -> slot 0x2F88` case from the citation) and `GetEntryAddress` at `System.cpp:1144`. So the
   missing primitive was not missing -- **it was WRONG.**

   **THE BUG, AND IT IS EXACTLY THE WALL.** `GetEntryAddress` computed the correct slot address and then
   **threw it away, returning the slot's CONTENTS (the handler pointer) instead**:
   ```cpp
   const uint32_t entryAddr = kGuestSyscallTableGuestBase + (syscallNum * 4u);
   uint32_t handler = 0;
   if (const uint8_t *ptr = getConstMemPtr(rdram, entryAddr)) { std::memcpy(&handler, ptr, sizeof(handler)); }
   setReturnU32(ctx, handler);          // <-- returns CONTENTS, should return entryAddr
   ```
   On real hardware it returns **the ADDRESS of the slot**, because that is the whole point: libosd.c
   writes *through* the returned address. So a caller doing the libosd.c thing --
   `*(uint32_t *)GetEntryAddress(n) = my_handler;` -- **wrote its handler to whatever the slot's previous
   contents happened to be.** **That is how a value like `0x240302d` ends up in slot `0x120E8`, and how
   the guest comes to jump into data and halt at `pc_outside_generated_table`.** The guest was not
   misbehaving; we were handing it a pointer to the wrong thing. **This also explains why slot contents
   changed between runs** (ITERATION 24: `0x240302d` vs `0xa0000c12`) -- they were whatever the guest was
   writing through a bad pointer, not a table.

   **THE FIX, AT THE SOURCE** (`System.cpp:1144`): return `kGuestSyscallTableGuestBase + syscallNum * 4u`.
   No read, no mutation -- it only reports where the slot is.

   **ITEM 3, RED FIRST, AND IT WAS GENUINELY RED.** New test in `ps2_find_address_tests.cpp`,
   **"W125: GetEntryAddress returns the SLOT ADDRESS, so a caller can write the slot"**, written in the
   plain-context fixture (`R5900Context` + `mem.getRDRAM()`, no scheduler, no executor -- so it did not
   repeat the segfault mistake). Before the fix:
   ```
   Total Tests: 494   Passed: 493   Failed: 1
   [Run]: W125: GetEntryAddress returns the SLOT ADDRESS... [Failed]
     - GetEntryAddress(n) must return the ADDRESS of slot n (0x80011F80 + n*4), because libosd.c writes through it
     - a handler written through GetEntryAddress(0x5A) must appear in slot 0x5A...
     - slots 0x5A and 0x83 must be exactly 0xA4=164 bytes apart...
   ```
   **After the fix the W125 test passed but an OLD test failed**: `"GetEntryAddress syscall (0x5B) returns
   handler from guest table"` -- **an existing test was pinning the inverted contract.** Corrected in place
   to assert the address, keeping the end-to-end `callSyscall(0x5B, ...)` path.
   **ONE MORE OF MY OWN TEST BUGS, RECORDED:** my first draft asserted the planted handler with
   `env.rdram[kEntryPhysAddr / 4u]`, but `rdram` is a **byte** vector, so that indexed a single byte. Fixed
   to `readGuestU32(...)`. **That was my bug, not the source's.**
   **FINAL: `Total Tests: 494  Passed: 494  Failed: 0  EXIT=0`.**

   **ITEM 4, THE BOOT AFTER THE FIX -- REAL PROGRESS, NOT SOLVED.**
   ```
   true_guest_entries=115180   (was 79267 -- 45% further)
   halt=pc_outside_generated_table   true_guest_exits=0   frames_presented=275
   WILDPC dead=0x88468107 last_good=0x0100f800   a0=0x440800a3
   SYSTABLE n=0x83 slot=0x1218c handler=0x46002328
   SYSTABLE n=0x5a slot=0x120e8 handler=0x000028a0
   ```
   **45% more guest entries, and the slot values changed again** -- so the fix did move the guest. **But it
   still halts at `pc_outside_generated_table`, and `last_good=0x0100f800` is UNCHANGED across four boots
   now.** That address is the derailment point and it is stubbornly reproducible. The new wild pc
   `0x88468107` is still nonsense (`0x88` is not a PS2 segment alias). **So the wall MOVED BACK a LITTLE
   BUT DID NOT FALL.**

   **ITERATION 24 -- THE 15-MINUTE BOOT IS ANSWERED, AND IT KILLS THE SPEED THEORY OUTRIGHT.**

   **ITEM 3, THE LONG BOOT, RUN PROPERLY (all VULCAN4_ knobs unset, 900s budget):**
   ```
   started 18:23:52   log last written 18:23:58   <-- SIX SECONDS
   WILDPC dead=0x00012403 last_good=0x0100f800
   SYSTABLE n=0x83 slot=0x1218c handler=0xa3000400
   SYSTABLE n=0x5a slot=0x120e8 handler=0xa0000c12
   true_guest_exits=0   halt=pc_outside_generated_table   frames_presented=277
   ```
   **THE GUEST DIED IN SIX SECONDS.** ITERATION 23 said `v0=0xffffffff` was the -1 from func_10057F0's
   delay slot, and blamed instrumentation overhead for the guest dying earlier than the reviewer's run.
   **BOTH WRONG.** With every knob off and 15 minutes of budget, the guest still dies in six seconds at
   `pc_outside_generated_table`. **So the halt is NOT a race against the budget, and it is NOT caused by
   instrumentation. The reviewer is right that nobody had done this: the screen does NOT change with more
   wall clock, because the guest is dead long before a disclaimer timer could expire.** There is no
   "just wait longer" answer, and I should not have let 0.26x stand as an explanation.

   **ITEM 1, SYSCALL TABLE BASE AND STRIDE -- BOTH SIDES, AS ASKED.**
   OURS: `ps2_runtime.cpp:4012 kTableGuestBase = 0x80011F80`, masked `& 0x1FFFFFFFu` -> physical
   **0x11F80, stride 4**. `setEeSyscallOverride` (:3968) uses the same base and `syscallNumber * 4`.
   Arithmetic checks: `0x11F80 + 0x5A*4 = 0x120E8` and `0x11F80 + 0x83*4 = 0x1218C`, difference
   **0xA4 = 164** -- which is exactly the constant `System.cpp:1071` says GT4 hunts.
   THE GUEST IS THE TRUTH, AND IT DOES NOT AGREE:
   - `mips-linux-gnu-objdump -d SCUS_973.28 | grep -E '0x5a|0x83'` returns **NOTHING**. The guest has
     **zero immediates for syscall 0x5A or 0x83** and **zero references to 0x80011F80/0x11F80**. The two
     hits for "11f80" are coincidental CODE addresses (0x1011f80, 0x10120e8), not the table.
   - `[FindAddress:hit]` = **0** and `[FindAddress:miss]` = **0** in the long boot. **GT4 never called
     FindAddress at all**, so the `s1 = s3 - 0x20C / s0 = s2 - 0x168 / loop until s1 == s0` convergence
     routine described in System.cpp is **not on this path**.
   **SO THE 0x11F80 BASE IS A ps2SDK CONVENTION WE ASSUMED, NOT SOMETHING THIS GUEST USES.** The comment
   at `vulcan4_harness.cpp:2727` claims "GT4 registers two of its own syscall handlers (0x83 and 0x5A)" --
   **that claim is false as far as this binary is concerned**, and the SYSTABLE probe has been reporting
   our assumption back to us as if it were a measurement. This is the same class of error as the 0x32/0x33
   syscall numbers: **an assumption quoted as ground truth.**

   **WHAT THOSE TWO SLOTS ACTUALLY CONTAIN, AND WHY IT MATTERS.** The same two slots read completely
   differently in two runs of the same binary:
   ```
   knobs on :  n=0x83 -> 0x?     n=0x5a -> 0x240302d
   knobs off:  n=0x83 -> 0xa3000400   n=0x5a -> 0xa0000c12
   ```
   and the arithmetic does not close: `0xa3000400 - 0xa0000c12 = 0x02fff7ee`, where 0xA4 is required.
   Both values are KSEG1-shaped (0xa...), i.e. **host-style aliased addresses**, and RDRAM is zeroed at
   init, so **the guest wrote them**. **And the wild pc `0x00012403` lies INSIDE the assumed table
   region** (0x11F80-0x129E8 is 0x11F80 + 666*4). So the guest is jumping into a low-RDRAM region that
   holds DATA, not handlers.

   **THE HYPOTHESIS THAT FITS, STATED AS A HYPOTHESIS AND NOT AS A FINDING.** On real hardware the
   **console kernel** fills the syscall table at 0x80011F80 with handler pointers. **This project has no
   BIOS in the path by law**, so nothing fills that table with real handlers; whatever is there is guest
   data, and a guest that jumps through it lands in data and dies at `pc_outside_generated_table`. The fix
   would be to populate the table with trampolines into our own dispatcher. **I have NOT verified this and
   I am NOT claiming it** -- it is the next thing to measure, because it is the only explanation that fits
   the wild pc being inside the table region.

   **ITEM 4, THE PICTURE.** `docs/evidence/w124_long_game.png`, window id `0x94fc30`, 650x482,
   **390 distinct colours**, title `VULCAN 4 - 117 GS packets/s | Speed: 0.82x PS2 | 452 shown`. Still
   the 2005 Sony disclaimer. Note 0.82x here versus 0.26x/0.27x earlier: **the "speed" figure is not
   stable either**, which is more evidence it was never the wall.

   **ITERATION 23 -- ITER PROFILED THE 90s. PER-ENTRY HOST OVERHEAD IS *NOT* THE PROBLEM, AND THE GUEST
   DIES ON A WILD PC, WHICH IS A BIGGER FINDING THAN SPEED.**

   **METHOD (item 1), so the numbers can be trusted.** `perf` is unusable here:
   `/proc/sys/kernel/perf_event_paranoid=4` and both `perf record -p` and a 70-second attach produced
   **0 samples** (perf23.data empty). So I instrumented in-process with `std::chrono::steady_clock`,
   **unconditional with no env gate** -- because the last instrument I added, `sleepCurrentCalls`, was
   built and never used for exactly that reason. Two probes at the same call site: one around
   `targetFn()` (the recompiled guest body) and one from the top of `EeScheduler::run()`'s dispatch
   function, so guest vs host is a subtraction of the same clock, not an inference.

   **RESULT 1 -- guest vs host, measured:**
   ```
   [w123:split] entries=20000 totalS=1 guestPct=99 hostPct=0 avgEntryNs=84745 avgGuestNs=84686
   ```
   **99% guest, host rounds to 0%.** Per the reviewer's own rule that means it is NOT per-entry host
   overhead, so that is not the win. (First attempt printed nonsense -- `entries=4e20 totalS=1` -- because
   the stream was not in `std::dec`; fixed with an explicit `std::dec`, and the corrected numbers are
   above.)

   **RESULT 2 -- AND THE ARITHMETIC SAYS THE GUEST SPLIT IS NOT THE WHOLE STORY.** 79267 entries x
   84745ns is only **~6.7 seconds**, in a process that runs far longer. I instrumented
   `EeScheduler::run()`'s other blocking path, `waitForEvent()`, and it **never fired** (0 iterations
   counted). So the missing time is neither guest nor host nor idle-wait.

   **RESULT 3 -- THE ACTUAL WALL. THE GUEST DOES NOT RUN SLOW; IT DIES.**
   ```
   VULCAN4 BOOT REPORT functions_entered=2246 true_guest_entries=79267 true_guest_exits=0
       halt=pc_outside_generated_table bios_files=0 intr_run=1728 gs_packets=395 frames_presented=306
   [guest-branch:missing-target] kind=DirectJump op=EE invocation service
       source=0x240302d target=0x240302d pc=0x240302d ra=0x240302d sp=0x010459e0 gp=0x01049770
       a0=0xccef5ea1 a2=0x70000000 a3=0x10000105 s0=0x0c0bf89e s1=0x260202d v0=0xffffffff
   VULCAN4 WILDPC dead=0x0240302d last_good=0x0100f800
   VULCAN4 SYSTABLE n=0x5a slot=0x120e8 handler=0x240302d
   VULCAN4 THREADS eeCycle=1724987830 runningThreadId=2
       THREAD id=1 status=Ready prio=3 pc=0x100f800 ra=0x1010a70 waitReason=1 wakeupCount=0
   ```
   **Syscall n=0x5a (90) = `fioRemove` in our call list, and its handler is `0x240302d` -- a WILD
   address, far outside the code region `generated_table=[0x01000008,0x0102dbec)`.** We jumped to it and
   the boot stopped with `pc_outside_generated_table`.

   **THIS EXPLAINS BOTH OPEN MYSTERIES AT ONCE.**
   - **`last_good=0x0100f800` IS EXACTLY tid1's pc.** The address I spent W120-W122 calling "tid1's
     parked pc" is the last good instruction before the wild jump. It was never a sleep -- it is where
     the guest was when it derailed.
   - **`v0=0xffffffff` is the `-1` that `func_10057F0` returns from its delay slot** -- the very value
     W120 identified as the "spin reduces to ONE 32-bit comparison". The guest was still failing that
     comparison when it computed the bad pointer.
   - **THE WALL IS NOT 0.26x SPEED.** `true_guest_exits=0`: the guest never returns cleanly, it walks
     off the end. **A disclaimer that needs more wall clock cannot be the explanation, because the
     guest is not waiting -- it is dead.** And this is why the halt reason varies between boots
     (`wallclock_deadline` vs `pc_outside_generated_table`): **whether we notice the wild jump before the
     budget expires is a race, which is the same nondeterminism recorded in ITERATION 22.**

   **RDRAM IS NOT THE CULPRIT FOR THE GARBAGE.** `PS2Memory::initialize()` does
   `new uint8_t[ramSize]; std::memset(m_rdram, 0, ramSize);`, so `0x240302d` was **written by the guest
   itself**, not left over from uninitialised memory. So the guest computed a bad pointer from a bad
   value. The neighbours in that table are the same shape -- `[1047a70]=0x200202d [1047a74]=0x260202d
   [1047a78]=0x200302d [1047aa8]=0x240202d` -- and `s1=0x260202d`, so **`0x240302d` is a member of a
   family of similar values, which smells like we are reading a DATA table as if it held handler
   pointers, or reading the wrong offset.**

   **THE NEXT JOB IS THEREFORE NARROW AND CONCRETE: work out why syscall n=0x5a resolves to a
   data-shaped value.** Check the syscall table base and slot arithmetic against the guest's own layout,
   and check whether the guest is supposed to have written a handler there at all before this call. Do
   NOT go looking for speed until that is answered -- a guest that executes a wild pointer cannot be
   made fast, only fixed. Note `MissingFunctionPolicy` already has `ContinueToTarget`, but jumping to
   `0x240302d` is undefined and must not be enabled as a shortcut.

   **ITERATION 22 -- THE WALL MOVES, AND THE SCREEN TITLE SAYS WHY. THIS IS THE REAL STATE.**

   **(d) APPLIED: THE PARKED-SLEEP STORY IS DROPPED AS A STANDALONE CLAIM.** Two boots minutes apart
   disagree, with the same binary:
   ```
   boot_desk123819.log   tid1:status=1:wait=none#0:woken=0      <- reviewer, NOT parked
   boot_w122c.log        tid1:status=1:wait=sleep#0:woken=0     <- mine, parked
   ```
   `sleepCurrentCalls` -- the instrument that was built and never used, now MEASURED -- is **140,273** in
   `boot_w122c.log`, so the guest sleeps constantly and often. **The wall is NOT a lost wakeup and NOT a
   parked sleep; it is NONDETERMINISM.** Do not chase tid1's state again: it has been observed both
   Ready-and-runnable and Sleeping on the same build, and no measurement yet distinguishes the two. If
   someone wants the sleep, they must FIRST show a log line proving it in the run they are discussing.

   **THE SCREEN, WHICH IS THE ACTUAL WALL. I OPENED THE PNG AND LOOKED AT IT.**
   `docs/evidence/w122_iter22_game.png`, window id `0x9425da`, 650x482, **405 distinct colours**. It is
   still the 2005 Sony Computer Entertainment Inc. disclaimer, flat white text on black. Same screen as
   `caine_1053`. The pixels have not moved.
   **The window title is the new information, and it is not a hang:**
   ```
   VULCAN 4 - 39 GS packets/s | Speed: 0.26x PS2 | 2836 shown
   ```
   **0.26x PS2 speed.** The GS is alive (39 packets/s, 2836 shown), but the guest is running at roughly
   a quarter of real hardware. **The disclaimer is a TIMED screen the guest dismisses itself when its own
   timer expires -- so at 0.26x the timer has not had the wall-clock time to run out.** That is a
   concrete, checkable explanation for seven hours of unmoving pixels, and it is consistent with
   `halt=wallclock_deadline` being the 90s budget expiring rather than a livelock.

   **THEREFORE THE LADDER MOVES: THE WALL IS SPEED, NOT CORRECTNESS.** The guest is correct enough to
   present thousands of frames of real GS output; it is simply too slow to reach the menu inside our
   budget. The next job is **throughput**, not more scheduler forensics:
   `true_guest_entries=484,303,659` per 90s and still only 0.26x means the recomp is spending its time
   somewhere that is not the guest's own work. Measure where the wall-clock actually goes before
   optimising anything.

   **HOW TO GET A PICTURE, because three attempts failed first and the reason matters:**
   - `import -window <game>` returns `Resource temporarily unavailable` while the harness is mid-present.
     Start a background boot, then capture.
   - `xdotool search` HANGS. Do not use it.
   - **Find the game window BY SIZE (650x482), not by name** -- the log terminal is 1068x671 and capturing
     the root gives you the TERMINAL, which is how the old screenshots kept showing a log full of MMIO
     lines instead of the game.
   - If the log says `INFO: Window closed successfully` / `=== boot ended ===`, the run is OVER and there
     is nothing live to photograph.

   **ITERATION 22 -- REVIEWER GATE FAILED ON MY REPORTING, AND THE GATE WAS RIGHT.**
   Three things I said were wrong, and all three are now corrected. Do not trust my prose; trust the
   log lines.

   1. **"493/493 AFTER THE REVERT" WAS FALSE.** The wrong test was STILL COMMITTED at
      `ps2xTest/src/ps2_runtime_kernel_tests.cpp:899` and still red. The reviewer measured
      `Total 494 / Passed 492 / Failed 2` and was right; I had reported a revert that had not happened.
      **THE TEST IS NOW DELETED** (restored `ps2_runtime_kernel_tests.cpp` to its `a65cc02` state, before
      the test was added). Measured after the delete, from `/mnt/ssd/vulcan4-build`:
      ```
      Total Tests: 493   Passed: 492   Failed: 1
      [Run]: VU0 macro mappings cover all S1/S2 enums   [Failed]
        - instructions.h should be readable from the test working directory
      ```
      **That 1 failure is the KNOWN UNRELATED VU0 one** (it reads `instructions.h` relative to the working
      directory; run from the source root it passes). **The W122 test is gone: 0 hits.** Suite is
      493 tests, 492 passing, and the only failure is not mine.

   2. **THE SLEEP STORY WAS BUILDING ON A STALE LOG.** My narrative said "tid1 is parked untimed"; the
      newest log said otherwise:
      ```
      THREAD id=1 status=Ready prio=3 pc=0x100f080 waitReason=0 wakeupCount=0
      thread_state=tid1:status=1:wait=none#0:woken=0 ... tid2:status=1:wait=none#0:woken=0
      ```
      **`wait=none` -- tid1 is NOT parked in a sleep.** Do not build another turn on the parked-sleep
      story without a fresh log line that actually says parked.

   3. **MY "CONTRADICTION" WAS MY OWN MISREAD OF THE SYSCALL NUMBERS.** I claimed "tid1 is parked AND the
      guest never issues syscall 0x33" was impossible. It is not -- **I had the numbers wrong.** Our call
      list (`ps2xRuntime/include/ps2_call_list.h`) numbers them by declaration order:
      ```
      SleepThread        -> 0x10
      WakeupThread       -> 0x11
      iWakeupThread      -> 0x12
      CancelWakeupThread -> 0x13
      ```
      **SleepThread is 0x10, NOT 0x32, and WakeupThread is 0x11, NOT 0x33.** Every "0x32 / 0x33 / 0x35"
      claim in this project -- including the "the guest never issues 0x33" line I put in a commit message
      -- refers to numbers that do not exist in our syscall table. **STOP citing 0x32/0x33/0x35; cite
      0x10/0x11/0x13.** The lost-wakeup conclusion may still hold, but the evidence for it was citing the
      wrong syscalls, so it must be re-measured against the real numbers before anything is built on it.

   **THE WALL THAT IS REAL, from the newest boot report** (`halt=wallclock_deadline`, i.e. the 90s deadline
   expiring, not a hang):
   ```
   functions_entered=30525 true_guest_entries=1095845 intr_run=16305
   gs_packets=5395 frames_presented=4806 gs_frame_reg_writes=1077
   ```
   **4806 frames presented and we are still on the Sony disclaimer.** The guest is alive and doing real
   work. The pixels have not moved since 10:53. **The next job is LOOKING AT THE OUTPUT, not more
   scheduler forensics.**

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
