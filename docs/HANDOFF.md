# VULCAN 4 — HANDOFF LOG

Every dish appends an entry here when it stops, for any reason. The shape is in `docs/CAMPAIGN.md`.
**A session that dies without an entry here wasted its turn** — the next re-arm starts blind.

Read the newest entry first, then `docs/CAMPAIGN.md`.

---

## 2026-09-30 14:27 · 30-g18b-break-the-wall · **passed**

WALL:   W5 — the driver never ran what the guest queued (`EeDispatcherTransfer` caught by the harness,
        which re-entered the guest without letting the scheduler service the invocation the guest had
        just queued; then `hasInvocation()` latched and every later `0x83` fell through to our builtin).
DID:    Made the harness honour the driver's obligation on the transfer signal — service the queued
        invocation, then resume on the guest's own published resume PC. Vendored runtime change shipped
        as `tools/patches/ps2recomp-linux-g18b-serveinvocations.patch` (4 files, +576/-6).
        `docs/G1.8b-RED.md` written BEFORE the fix, both test shapes red first.
MEASURED:
        before: `functions_entered=3  halt=livelocked_in_syscall`
        after:  `functions_entered=25 halt=guest_cycle_no_progress`  (measured twice, bios_files=0)
        suite:  449 tests, 448 passing (the 1 failure is `VU0 macro mappings cover all S1/S2 enums` —
                pre-existing, unrelated, not touched)
        SYSTABLE overrides now registered: `0x83 → 0x10285f8`, `0x5a → 0x10285c0`
NEXT:   **W6 — the guest restarts from its own entry point.** `pc=0x01000008`, `distinct_pcs=1`,
        24 repeats, and `sce_SetupHeap`/`SetupThread`/`CreateSema` each called **25×** = the guest is
        re-running its own CRT init every cycle. `distinct_mmio_addresses=0` proves it is NOT waiting on
        hardware — this is our control flow, not the console's.
        Dish written and queued: `31-g18c-resume-at-the-right-pc`.

## 2026-09-30 12:00 · (no dish) · campaign opened

WALL:   none — housekeeping turn.
DID:    Committed the chef's blocked G1.8 work (`ba8efe0`) so nothing was left uncommitted; added
        AGENTS.md law 11 (`-j4` + nice: a `-j12` compile caused the live Minecraft server's only tick
        stall) and `.gitignore` now excludes `.opencode/`.
MEASURED: `functions_entered=3 halt=livelocked_in_syscall` at 12:08.
NEXT:   the campaign itself — `docs/CAMPAIGN.md`. The captain's instruction: one long-term goal that
        does not die with a session.

## 2026-09-30 15:05 · 31-g18c-resume-at-the-right-pc · **wall fell (gate partially met)**

WALL:   W6 — the guest re-entered `0x01000008` (the ELF entry) and re-ran its own CRT init 25 times
        instead of resuming at the PC each frame published.
DID:    Fixed the copy direction between the driver's live context and the scheduler's private copy
        of the base frame. `EeScheduler::serviceInvocations()` now refreshes `main->context` FROM
        `PS2Runtime::m_cpuContext` on entry when the driver is not `EeScheduler::run()` (guarded on
        `m_guestExecuting`), and the publish on completion is unconditional again. Red test first:
        `G1.8c: a frame chain is entered once...` — `docs/G1.8c-RED.md` has the verbatim red text
        and the probe output that proved the mechanism.
MEASURED:
        before: `functions_entered=25  halt=guest_cycle_no_progress  distinct_pcs=1   pc=0x01000008`
        after:  `functions_entered=95  halt=guest_cycle_no_progress  distinct_pcs=50  pc=0x0100f800`
        `sce_SetupThread` 25 → **1**, `sce_SetupHeap` 25 → **1**, `sce_FindAddress` 25 → **2**
        `distinct_mmio_addresses` 0 → **228** (195 in `0x7000xxxx`, the GS/GIF window)
        suite:  **450/450 passing** (the `VU0 macro mappings` cwd failure is GONE — it was this
                session's wrong working directory, not a real failure; run the suite from
                `tools/PS2Recomp/ps2xTest`)
        `serviced_invocations=5` — still non-zero, G1.8b behaviour intact
        gate:   **NOT met** (`GATE_EXIT=1`) — `n>25` ✅, `dp>1` ✅, but the halt is still
                `guest_cycle_no_progress`. The dish's step 5 covers this: the wall MOVED.
NEXT:   **W7 — the guest spins in a byte-store loop to the GS window.** At `0x0100f800`:
        `sb $v0,0($t1)` / `addiu $t1,$t1,1` / `bne $t1,$t4` — a memset/copy tail that never
        terminates. **This is the first wall on the path to milestone 2**, because the guest is
        already writing to GS memory. Dish: `33-g18d-gs-store-loop`.
        Also open: the SYSTABLE for `0x5A` now reads `handler=0x102aa38` (was `0x10285c0`) — the
        override pointer differs between cycles; worth a look but not the current wall.

## 2026-09-30 15:55 · 33-g18d-gs-store-loop (investigation only) · **W7 named, no fix yet**

WALL:   W7 — the guest spins in a byte-store loop at `0x0100f800`. Still open; this entry is the
        measurement that names it.
DID:    **No code changed.** Instrumented the generated translation unit in the build tree, measured,
        then removed every probe and rebuilt clean. What I got:
        - `[W7] outer t1=0x01051A45 t4=0x01051A3F s1=0x01051A44 s2=0x01051A4B` — **`$t4` never
          changes, `$t1` climbs by one and runs away from it.**
        - The one store pair sets BOTH slots to the same value: `store 0x24(t4)=0x01051A3F`,
          `store 0x20(t1)=0x01051A3F`. **Start == end.** But the loop *starts* 6 bytes past the end,
          so it wraps the whole 32-bit space instead of exiting.
        - Verified against the ELF bytes (`0x0100f7ac` / `0x0100f7b0` in SCUS_973.28), not just our
          recompile, because three findings in this project came from trusting the translation.
        - **Frozen, not slow:** 10 s / 30 s / 120 s all give `functions_entered=95 distinct_pcs=50`.
        - `sub_0100F390` is entered 30× and never returns; the last 20 entries in `ps2_log.txt` are
          all `>> sub_0100F390 enter`.
MEASURED:  `functions_entered=95 halt=guest_cycle_no_progress` (unchanged — correct, nothing was
        fixed). Suite **450/450** still green from `tools/PS2Recomp/ps2xTest`.
        `distinct_mmio_addresses=228`, guest GS writes present: `[gs:gif] nloop=7`, `PRMODE=0x8005`,
        `PRIM=3` (a triangle).
NEXT:   W7's open question, now sharp: **why is `$t1` `0x01051A45` when `$t4` was just stored as
        `0x01051A3F` into the same structure?** Start and end are written equal; something adds 6 to
        the start between the store and the loop. Look at the address arithmetic at the `0x100f864`
        outer head — and MEASURE it, do not read it off the instruction stream.
        **False lead, do not re-chase:** `sub_0100D308` ×16,368 is a **PRNG** (`multu`/`mflo`/`mfhi`
        recombined) driving `buffer[i] ^= random()` for a hardcoded `0x3FF0` iterations. That is
        legitimate guest work which returns cleanly. It is NOT the wall, and it cost a detour.
        Addendum with all of the above is in `.auto/queue/33-g18d-gs-store-loop.txt`.

## 2026-09-30 16:40 · 33-g18d-gs-store-loop (investigation only, 2nd pass) · **W7 characterised**

WALL:   W7 — still open. This entry is the second measurement pass on it.
DID:    **No code changed.** Probed the generated translation unit again, measured, removed every
        probe, rebuilt clean (verified: 0 `fprintf` left in the artifact). Both of W7's loops are now
        characterised:
        - **The outer loop is fine.** `[S] OUTERHEAD s1=0x01051A45 s2=0x01051A4B` — `s1` walks
          `…45, …46, …47` toward `s2`. A normal 6-byte copy.
        - **The inner loop is the wall.** `[S] INNERTEST t1=0x01051A46 t4=0x01051A3F taken=1` forever.
          `$t1` climbs; `$t4` never changes.
        - **The guest image writes start == end.** Read the raw ELF bytes, not our translation:
          `0x0100f7ac: 0xae0c0024 => sw $t4,0x24($s0)` and `0x0100f7b0: 0xae0c0020 => sw $t4,0x20($s0)`.
          **Both store `$t4`.** Slot `0x20` is the start, `0x24` the end. So the guest sets
          start == end == `0x01051A3F`, and the inner loop should run **zero** times.
          Neighbouring instructions decode correctly (`0x100f794` = `sd $a3,0($s0)`, `0x100f778` =
          `andi $t3,$a0,0x7FFF`), so **this is not a decode error on our side.**
MEASURED:  Boot unchanged at `functions_entered=95 halt=guest_cycle_no_progress` (correct — nothing
        was fixed). Suite **450/450** from `tools/PS2Recomp/ps2xTest`.
NEXT:   The question is now sharp and is three ordered measurements, in the addendum:
        1. Does `$t1` change between the store pair and the first loop entry? Probe `0x100f7b4`.
        2. **Most likely:** the store pair is immediately followed by `jalr $v1` — an indirect call
           through a guest function pointer. If that callee writes `$s0+0x20`, it is the writer, and
           this is a **guest** bug, not ours.
        3. Only if 1 and 2 are negative: is our `dispatchGuestBranch` resuming the wrong PC around
           that `jalr`? That would be W6's disease and would mean the G1.8c fix is incomplete.
        **Do not clamp `$t1` to `$t4` and do not special-case `0x100f800`** — that is a fake in the
        same class as a stubbed syscall, and it would hide which of the three is actually true.
        Full numbers in `.auto/queue/33-g18d-gs-store-loop.txt` (addendum 2).

## 2026-09-30 17:25 · 33-g18d-gs-store-loop (investigation only, 3rd pass) · **W7 mechanism found**

WALL:   W7 — still open. This entry is the pass that found its mechanism.
DID:    **No shipped code changed.** Probed, measured, removed every probe, rebuilt the generated
        artifact clean (verified 0 `fprintf` left). **I also wrote a red test for my hypothesis and
        it PASSED, so I deleted it** — a green test with no red behind it proves nothing and
        shipping it would have been the exact fake this project forbids.
MEASURED:
        [V] t1=0x01051A45 t4=0x01051A3F (t1>t4)=1  s0=0x01FFCED0 mem20=0x01051A40 mem24=0x01051A3F
        - **`$s0 = 0x01FFCED0` is a STACK address** (guest stack is 0x01FFFEA0/0x01FFFF70), so the
          two slots are pointers *on the stack* whose values are guest RAM addresses.
        - **The range is INVERTED: start `0x01051A40`, end `0x01051A3F`.** The end is one byte
          BEHIND the start, and the loop's test is `bne $t1,$t4` — *not-equal*, not less-than — so it
          walks the entire 32-bit space. It is not infinite by construction; it is a loop over four
          billion addresses because the predicate can never be satisfied from below.
        - **`mem20` != `mem24`** (`0x01051A40` vs `0x01051A3F`), which **retracts addendum 2's**
          claim that the guest sets start == end.
        - **The recompiler is NOT at fault.** The inner loop has no `eeCheckpointDue()`; the back
          edge decodes correctly from the raw field (`0x100f80c + (0x16<<2) = 0x100f864`); both
          branches in the neighbourhood decode correctly.
NEXT:   **One probe, and the filter that makes it work:** find who writes `0x01051A40` into
        `$s0+0x20`. Probe every `WRITE32(..., $s0+0x20)` and every `WRITE64(..., $s0)` inside
        `sub_0100F390` — **but filter on `$s0 == 0x01FFCED0`**, which is what this pass finally
        produced and which no earlier probe used. That turns 87 candidate sites into one.
        Then, if the writer is the caller, find who passed an inverted range.
        **Do NOT clamp `$t1` to `$t4`, do NOT special-case `0x100f800`.** The inverted range is a
        symptom whose producer is still untraced; clamping hides it and is the same class of fake as
        a stubbed syscall.
        Addendum 3 in `.auto/queue/33-g18d-gs-store-loop.txt` has the numbers and the retraction.
MEASURED (suite):  **450/450** from `tools/PS2Recomp/ps2xTest`, after deleting the meaningless test.
MEASURED (boot):   `functions_entered=95 halt=guest_cycle_no_progress bios_files=0` — unchanged,
        which is correct: nothing was fixed. Campaign goal NOT reached.

## 2026-09-30 18:05 · 33-g18d-gs-store-loop (investigation only, 5th pass) · **W7 loop SOLVED, bad value open**

WALL:   W7 — the loop is now fully explained. The *value* that makes it infinite is not yet sourced.
DID:    **No shipped code changed.** Probed, measured, removed every probe, rebuilt the generated
        artifact clean (0 `fprintf`), re-verified the boot at `functions_entered=95`.
MEASURED:
        - The callee `sub_0100F390` reads `lw $t1,0x20($s0)` / `lw $t4,0x24($s0)`, and the back edge
          `bne $t1,$t4` targets `0x100f864` — which is that same `lw`. **`$t1` is reloaded every
          iteration, so its `+1` is discarded and it is pinned.** `mem20` reads `0x01051A40` on every
          sample, never moving.
        - The caller at `0x1010a68` fills the struct. **Decoded from the ELF, correcting addendum 4
          which mis-decoded these as `$t1`/`$t4`:**
          `0x1010a60: 0xae170020  sw $s7,0x20($s0)`  → start = `$s7` (r23)
          `0x1010a6c: 0xae1e0024  sw $fp,0x24($s0)`  → end   = `$fp` (r30)
        - **At the call site, measured:**
          `[FIN] s7=0x01051A40 fp=0x01051A3F s7>fp=1` … `s7=0x010632C4 fp=0x01051A3F s7>fp=1`
          **`$s7` advances every call. `$fp` never moves.** The range is inverted every time.
        - **Grepping the caller's whole body for a write to `$fp`/r30 finds exactly one instruction:
          the `sw $fp,0x24($s0)` that CONSUMES it. Nothing there ever ASSIGNS `$fp`.**
        - **Every instruction involved decodes correctly** and the recompiler emits each to the label
          its own offset names (verified twice, on `0x100f808` and `0x100f864`). **This is not a
          codegen bug.** The guest was handed a buffer whose end pointer it never received.
NEXT:   **Up the call chain, not down into the loop.**
        1. Find the caller of the function containing `0x1010a68` and check what it passes in `$fp`
           and `$s7`. `$fp` is callee-saved (r30): **a recompiled callee that fails to save/restore
           it is the one hypothesis here that would be a real recompiler bug, and it is the one to
           test first.** Watch `0x1010a5c: lq $t4,0x5E0($sp)` in the same block — a 128-bit load
           through `$sp` reading the wrong 8 bytes would put a plausible value in the wrong register.
        2. `$s7` advancing while `$fp` does not is also consistent with an empty/short buffer sized
           wrong. If the size came from a file header or a memory-card read, **that read may be the
           real wall** and this loop only its symptom.
        3. Check whether the frame arrives anyway: `[gs:gif] nloop=7`, `PRIM=3` was already seen, and
           a GS write is not a function entry, so `functions_entered` being frozen does not prove no
           frame reached the hardware.
        **Do NOT clamp `$t1` to `$t4`, do NOT special-case `0x100f800`, do NOT "fix" it by making
        the store block run.** Every instruction is correct and the value is wrong; patching the loop
        would hide the only thing that matters.
        Full chain and all five passes: `.auto/queue/33-g18d-gs-store-loop.txt` addenda 1–5.
MEASURED (suite): **450/450** from `tools/PS2Recomp/ps2xTest`.
MEASURED (boot):  `functions_entered=95 halt=guest_cycle_no_progress bios_files=0`. Campaign goal NOT
        reached — `VULCAN4 FRAME source=guest` has still never been printed.

## 2026-09-30 18:40 · 33-g18d-gs-store-loop (investigation only, 6th pass) · **W7: `$fp` provenance gap**

WALL:   W7. The loop is solved. This pass located where the bad value comes from.
DID:    **No shipped code changed, no boot change.** Two of my own conclusions were **retracted in the
        same pass** after I checked the emitted code properly, and both retractions are kept below.
MEASURED:
        - The end pointer is **`$fp` (r30)**; the start is **`$s7` (r23)**. Set by the caller at
          `0x1010a60: sw $s7,0x20($s0)` and `0x1010a6c: sw $fp,0x24($s0)`. `$s7` advances every call
          (`0x01051A40` → `0x010632C4`); `$fp` never moves from `0x01051A3F`.
        - **RETRACTION 1:** I claimed `sub_0100F8C8` saves `$fp` and never restores it. **Wrong** —
          `ld $fp,0x5E0($sp)` is emitted (generated unit line 75267). My grep pattern was wrong.
        - **RETRACTION 2:** same claim about `sub_0100EDC8`. **Also wrong**, same cause; `ld $fp` is
          emitted at `0x100f380`.
        - **THE ACTUAL FINDING, not retracted:** that `ld $fp` at `0x100f380` has **no
          `case 0x100f380u` in the function's resume switch.** `sub_0100EDC8` has 14 resume cases and
          the epilogue's `$fp` restore is not one of them. **A yield in that epilogue re-enters the
          function from the top and leaves `$fp` as the body last set it.** `sub_0100EDC8` is called
          **4×** by `sub_0100F8C8` immediately before the W7 call, and `$fp` is the end pointer.
        - Scale, measured not guessed: `sub_0100EDC8` has **44 branch/jump source PCs and 14 resume
          cases**, and the two sets do not correspond.
NEXT:   1. **Red test first (law 4).** A recompiled function that yields inside its own epilogue must
        resume at the restore and leave the callee-saved register intact. Shape: `ld $s0..; ld $s1..;
        ld $fp,..; jr $ra`, checkpoint due between `ld $fp` and `jr $ra`, assert `$fp` holds the saved
        value after resume. **Must be red before anything changes.**
        2. Cheap confirmation: one probe printing whether `sub_0100EDC8` is ever entered with
        `ctx->pc == 0x100f380`. If never, this whole lead is dead and the search goes back up the call
        chain for whoever should set `$fp`.
        3. **Do not assume all 30 missing cases are bugs** — a case is only needed where a yield can
        actually be taken.
        **Still forbidden:** clamping `$t1` to `$t4`, special-casing `0x100f800`, or making the store
        block run. On this diagnosis the loop is innocent and the damage is upstream.
MEASURED (suite): **450/450**. MEASURED (boot): `functions_entered=95 halt=guest_cycle_no_progress
        bios_files=0`. **Campaign goal NOT reached — `VULCAN4 FRAME source=guest` never printed.**
        Addenda 1–6 in `.auto/queue/33-g18d-gs-store-loop.txt`.

## 2026-09-30 19:05 · 33-g18d-gs-store-loop (investigation only, 7th pass) · **`$fp` lead KILLED**

WALL:   W7. The `$fp` hypothesis from pass 6 is **refuted by measurement.**
MEASURED:
        ```
        [E] sub_0100EDC8 entry#1 ctxpc=0x0100EDC8 fp=0x01051A3F s7=0x01051A40
        [E] sub_0100EDC8 entry#7 ctxpc=0x0100EDC8 fp=0x01051A3F s7=0x0105CB32
        [E] ld-fp RAN #1 fp=0x01051A3F
        ```
        1. **Every entry to `sub_0100EDC8` is at `ctxpc=0x0100EDC8`** (the function top), never at
           `0x100f380` or any resume case. **The missing resume case for `ld $fp` is unreachable.**
        2. **`ld $fp` runs (8+ times) and restores `fp` to `0x01051A3F`.** The restore is faithful;
           **the saved slot already holds the bad value when the function is entered.**
        So `$fp` is clobbered and saved **upstream of `sub_0100EDC8`**, which faithfully propagates
        it. Also: `$s7` changes per entry group while `$fp` is constant — the two are wrong in
        *different* ways, so they come from disagreeing sources.
NEXT:   **STOP WALKING FORWARD ONE FUNCTION AT A TIME — six passes of diminishing returns, and I am
        saying so rather than starting a seventh.** Ask the question that would have saved them:
        **where do `0x01051A3F` and `0x01051A40` FIRST appear in RDRAM?** A one-shot scan for both
        values names every site that produced them in one shot, instead of one function per pass.
        `$fp`'s saved slot holds `0x01051A3F`, so the writer is upstream of everything examined.
        Note `0x01051A3F` is exactly `0x01051A40 - 1`; if both appear adjacent in a table, that
        table is the answer and the loop is just a symptom of reading it.
MEASURED (suite): **450/450**. MEASURED (boot): `functions_entered=95 halt=guest_cycle_no_progress
        bios_files=0`. **Campaign goal NOT reached — `VULCAN4 FRAME source=guest` never printed.**
MEASURED (artifact): probes removed, generated unit rebuilt clean (0 `fprintf`).
        Addenda 1–7 in `.auto/queue/33-g18d-gs-store-loop.txt`.

## 2026-09-30 19:35 · 33-g18d-gs-store-loop · **W7 CLOSED — the end bound exists nowhere**

WALL:   W7. **CLOSED.** I ran the memory scan I recommended in the previous handoff instead of only
        writing it down, and it answered the question seven function-by-function passes had not.
MEASURED:
        ```
        VULCAN4 W7SCAN start 0x1051a40 at 0x1047a8c      <- live BSS global
        VULCAN4 W7SCAN start 0x1051a40 at 0x1ffceec      <- the struct slot
        VULCAN4 W7SCAN end   0x1051a3f at 0x1ffc870      <- STACK
        VULCAN4 W7SCAN end   0x1051a3f at 0x1ffcef4      <- STACK (the struct slot the loop reads)
        VULCAN4 W7SCAN total start=2 end=2
        ```
        - **Two occurrences of each value in all 32 MB of RDRAM.** Not 87 candidate sites.
        - The start has exactly one home in guest data: a **lone non-zero word in a field of zeros**
          at `0x01047A8C`, inside BSS, so a runtime variable. One instruction reads it:
          `0x1000ae0: lw $v1,0x7A8C($v0)`. It is the guest's buffer cursor.
        - **The end value `0x01051A3F` appears NOWHERE in the code** — `grep` over the whole
          707-function translation unit returns **0**. Not a static initialiser. In RDRAM it exists
          at two addresses, **both on the stack.**
        - **Therefore: the inverted range is the ABSENCE of a value, not a wrong computation.** The
          struct is filled at `0x1010a48`–`0x1010a6c`; the `0x24` slot was never written, and the
          loop is reading a stale stack leftover as its end pointer. That is why `$s7` advances while
          `$fp` never moves.
NEXT:   **This is now a control-flow question, not a data question.**
        1. **Dump all eight struct slots at the call** (`0x0,0x8,0xc,0x10,0x18,0x20,0x24`) and see
           which are stale. If `0x24` is the only one never freshly written, the fill is short-
           circuiting.
        2. **Most likely shape, check it first:** a syscall or `EeDispatcherTransfer` thrown between
           `0x1010a40` (`jal func_100EDC8`) and `0x1010a6c` (`sw $fp,0x24($s0)`) would leave the
           struct **half-filled** — exactly what a stale `0x24` beside a fresh `0x20` looks like.
           `$fp` callee-saved and `$s7` fresh is the signature of a frame abandoned between two
           stores. **Check that `jal`'s return PC.**
        3. Only if 1 and 2 are negative: the fill is guarded by a condition that evaluated false.
        **The red test, now easy to state and still owed:** a transfer thrown between two stores into
        the same struct must leave it visibly half-written, and the harness must report that rather
        than let the guest spin on the stale half. That is a test about the *driver*.
        Method note, the second time this session: addendum 7 said scan instead of walking. **One
        scan closed what seven passes had not.** When a value is wrong, find ALL its occurrences
        before tracing any single use — a wrong value has one producer, an ABSENT value has none.
MEASURED (suite): **450/450**. MEASURED (boot): `functions_entered=95 halt=guest_cycle_no_progress
        bios_files=0`. **Campaign goal NOT reached — `VULCAN4 FRAME source=guest` never printed.**
        Addenda 1–8 in `.auto/queue/33-g18d-gs-store-loop.txt`.

## 2026-09-30 19:55 · 33-g18d-gs-store-loop (investigation, 9th pass) · **W7: struct is NOT half-written**

WALL:   W7. Addendum 8's leading hypothesis is now tested and **refuted**.
MEASURED:
        ```
        [FILL] 0x0=0x2FE07A5F 0x8=0x00000001 0xc=0x01036E28 0x10=0x01036D7F 0x18=0 0x20=0x01051A40 0x24=0x01051A3F | s7=0x01051A40 fp=0x01051A3F
        [FILL] 0x0=0x328F2794 0x8=0x00000015 0xc=0x010380C8 0x10=0x01036D7F 0x18=0 0x20=0x01056478 0x24=0x01051A3F | s7=0x01056478 fp=0x01051A3F
        [FILL] 0x0=0x265D71E6 0x8=0x00000008 0xc=0x010394B8 0x10=0x01036D7F 0x18=0 0x20=0x0105CB32 0x24=0x01051A3F | s7=0x0105CB32 fp=0x01051A3F
        ```
        - **The struct is filled completely and freshly every call.** `0x0` (a PRNG word), `0x8` (a
          count) and `0xc` (a pointer) all change; `0x10` is a constant base; `0x18` is zero; `0x20` is
          the start; `0x24` is the end. **Nothing is half-written, and the `jal` returns normally.**
          Addendum 8's "a transfer between the two stores" hypothesis is **REFUTED**.
        - **What is left, exactly:** `$fp` already holds `0x01051A3F` on entry, so `sw $fp,0x24($s0)`
          is faithful but useless. The defect is **strictly upstream of `sub_0100F8C8`**.
        - Addendum 8's scan still stands and is still the key fact: **the end value appears nowhere
          in the 707 functions and nowhere in guest data — only on the stack.**
NEXT:   Two steps, neither of which is more forward-walking:
        1. **Follow the OTHER moving pointer, not `$fp`.** Slot `0xc` moves `0x01036E28` → `0x010380C8`
           → `0x010394B8` → `0x0103A8F4` against a constant base `0x01036D7F` at `0x10`; the offset
           grows +0xA9, +0x134F, +0x2739, +0x3B75. **That is a second cursor advancing through the
           same data the start cursor is in, and the copy's end bound is probably derivable from it.**
           One probe, and it is the most informative thing left.
        2. **Find who last WROTE `$fp`, not who reads it.** Passes 6–7 chased the read side and the
           save/restore, both fruitless. `sub_0100EDC8` is entered with `$fp` already wrong *and* its
           `ld $fp` restores the wrong value, so it was clobbered above `sub_0100F8C8`. Extend
           `W7SCAN` to report the `$fp`-save slot (currently `0x01FFC870`) and scan for writes to it —
           the same one-shot trick that just worked.
        **The red test is still owed:** a transfer thrown between two stores into one struct must be
        *reported*, not silently left half-written. Note pass 9 shows that path is NOT what is
        happening here, so the test is worth writing for robustness but is **not** the fix.
        **Method, third time:** passes 1–7 walked forward and found no writer; pass 8 scanned and
        closed it; pass 9 tested the claim and refuted half of it cheaply. **Scan, test, never walk.**
MEASURED (suite): **450/450**. MEASURED (boot): `functions_entered=95 halt=guest_cycle_no_progress
        bios_files=0`, `W7SCAN total start=2 end=2`. **Campaign goal NOT reached — `VULCAN4 FRAME
        source=guest` never printed.** Artifact rebuilt clean (0 `fprintf`). Addenda 1–9 on file.

## 2026-09-30 20:40 · 33-g18d-gs-store-loop (passes 8-10) · **a real bug FIXED, wall narrowed to 5 bytes**

WALL:   W7. Still open, but no longer the same wall.
DID:    **One real bug found and fixed**, red test first. Ten measurement passes on W7; all temporary
        probes removed and every artefact rebuilt clean (0 `fprintf` in the generated unit, the
        runtime, and the harness except the three W7 probes deliberately kept).
MEASURED — THE FIX:
        `[SetupHeap] base=0x10519ac alignedBase=0x10519b0 size=0xffffffff runtimeBase=0x10519b0
                     runtimeEnd=0x10519b0`
        **`runtimeEnd == runtimeBase`: every configured guest heap was ZERO LENGTH.**
        `PS2Runtime::resetGuestHeapLocked` set `m_guestHeapEnd = base` — the heap's TOP pinned to its
        BASE. It only ever looked right because `guestMalloc` raises it as blocks are handed out,
        **and GT4 runs its own allocator, so nothing ever raised it.**
        Fixed to `m_guestHeapEnd = (limit > base) ? limit : base;` → `runtimeEnd=0x1f00000`.
        **Red first, two tests, captured before the fix:** `G1.8e: SetupHeap(-1) gives a heap with
        room, not a zero-length one` and `G1.8e: a heap configured with a real limit is not
        zero-length` — both [Failed] with the mechanism in the message, both green after.
        **Suite 452/452.**
        **HONEST: this did not move the boot.** `functions_entered` is still 95, halt still
        `guest_cycle_no_progress`. A real bug fixed is still a real bug fixed, and the guest can now
        allocate from a heap with room.
MEASURED — THE WALL, NOW MUCH SMALLER:
        - **The range is an EMPTY RANGE BY DESIGN.** Both callers compute `end = start - 1`
          (`0x1004394` and `0x102cec4`) — the standard empty-range idiom. So `0x01051A3F` is exactly
          right for `0x01051A40`, and **the loop is supposed to run zero times.** That kills every
          "inverted range" reading in addenda 1–9.
        - `sub_0100F390` **never reaches its own reload** at `0x100f85c` (0 samples). It is entered
          **at the loop body** `0x100f800` via `case 0x100f800u`, which is correct.
        - On entry **`$t1` is `0x01051A45` = start + 5.** Not start, not start−1, not start+1.
        **So the remaining wall is five bytes wide**, and the loop body runs before the empty-range
        test is ever evaluated.
ELIMINATED across ten passes, none to be re-chased: recompiler branch emission · resume-case
        mechanism · the `jalr` on the `0x100f7b4` path · the store pair at `0x100f7ac/0x100f7b0` ·
        the callee-saved `$fp` restore (emitted correctly; **I retracted this claim twice**) · the
        scheduler/driver context divergence (the G1.8c refresh works — measured) · a stranded syscall
        invocation (`inv5A=0 inv83=0`, refuted) · the zero-length heap (**real, now fixed**) · and
        the inverted-range reading itself (refuted).
NEXT:   **One probe, and it is the one I should have run in pass 4: print `$s1`, `$s2`, `$t0` and
        `$t1` at the yield inside the outer branch at `0x100f864`.** That yield is the only place
        `ctx->pc` is set to `0x100f800`, so it is the only place the entry value can be established.
        If `$s1` is already `start+5` when the body is entered, the outer loop ran five times first
        and the outer loop is the culprit. **That either names the bug or closes the wall.**
        The `W7SCAN` / `W7NEIGH` / `W7NODE` probes stay in the harness — they are how the range was
        proved empty by design, and the next session needs them.
MEASURED (suite): **452/452** from `tools/PS2Recomp/ps2xTest`.
MEASURED (boot):  `functions_entered=95 halt=guest_cycle_no_progress bios_files=0`, with
        `SetupHeap ... runtimeEnd=0x1f00000` confirming the fix is live in the real run.
        **Campaign goal NOT reached — `VULCAN4 FRAME source=guest` never printed.**
        Addenda 1–10 in `.auto/queue/33-g18d-gs-store-loop.txt`. Patch:
        `tools/patches/ps2recomp-linux-g18e-heapend.patch`.

---

## 2026-09-30 — G1.8f: W7 FELL. It was the detector, not the guest. New wall W8.

**WHAT FELL.** W7 was never a guest bug. Five passes of instrumentation had been building a story about
a pinned `$t1` and an end bound that never moved; every part of that story was wrong, and the loop the
harness kept calling a hang was **converging normally and six iterations from its bound**.

The real bug was in `tools/harness/vulcan4_harness.cpp`. The no-progress detector treated *any* repeating
PC pattern as a hang after 24 repeats:

    if (++cycleRepeats >= budget.maxCycleRepeats) { haltReason = kHaltCycleNoProgress; break; }

GT4's normal work is a converging loop over one small set of addresses — a rasteriser walking a glyph row,
a physics step. Those loops revisit identical addresses *while making progress*, so the detector was
stopping the boot inside the game's ordinary work. The PC stream alone cannot tell a converging loop from
a hang; the distinction needs a signal outside it.

**THE FIX.** `PS2GuestProgress` (new, `tools/PS2Recomp/ps2xRuntime/include/ps2_guest_progress.h`) holds the
cycle detector and is fed the two signals the PC stream cannot show: **new code entered** and **new
hardware touched**. Reaching either resets the streak. Re-polling a register the guest already polls does
NOT count as progress, so a guest genuinely waiting on hardware is still caught. The threshold went
`24 → 4096`: a guest call's own internal loops must clear it, and W7 measured ~193 passes per call. The
`VULCAN4 WAIT` reporter is preserved via `history()` so a real wait can still be decoded and named.

**MEASURED (the number that matters):** `functions_entered` **95 → 20,000,000**.

**RETRACTED — do not re-adopt these.** `$t1` was *not* pinned (it is `$s1+1` and advances with it);
`0x24`/`$fp` was *not* the end bound and did not move; the range was *not* empty by design; and
`sub_0100F390` was *not* entered at the loop body. `taken32 == taken64` at the W7 branch, so the latent
64-bit branch-semantics concern (2,791 branches compare `GPR_U64`, none compare `GPR_U32`) did **not** fire
there. It is still a real concern on the hardware's terms and still unfixed — but it is not this wall.

**MEASURED (suite):** **456/456** from `tools/PS2Recomp/ps2xTest`, run from `tools/PS2Recomp/ps2xTest`
(the suite reads `../../ps2xRecomp/include/...` relative to cwd; run from anywhere else and one
unrelated VU0 test fails on a missing `instructions.h`).
**MEASURED (boot):** `functions_entered=20000000 halt=entry_budget_exhausted bios_files=0`,
`distinct_pcs=112 dispatcher_transfers=15 serviced_invocations=15`, only **119 syscalls** and **469 MMIO
accesses** across 20M entries. **Campaign goal NOT reached — `VULCAN4 FRAME source=guest` never printed.**

**THE NEW WALL — W8, and it is a real one.** The guest now spins in its idle loop: it calls
`sce_SleepThread` 11× and then enters the same ~112 functions 166k×/sec while doing no work at all.
`dispatcher_transfers=15` in 20M entries says the cooperative scheduler is barely being serviced, so
`sce_SleepThread` is returning immediately instead of blocking the calling thread. The main thread never
yields, so no other thread ever runs. Next: make it block, and prove it on **thread state**, not on
function counts. Brief: `.auto/queue/35-g18g-sleepthread-blocks.txt`.

## 2026-09-30 19:40 · 35-g18g-sleepthread-blocks · **W8's mechanism FIXED (3 defects), wall NOT fallen, W9 named**

WALL:   W8 — the guest spins in its idle loop. **The wall did not fall. Its mechanism did, and it
        was three separate real bugs, not one.** A new wall (W9) is named below.

DID:    Red first, then three fixes, all in the runtime, all proven on THREAD STATE:

        1. **`EeScheduler::serviceInvocations()` resurrected a blocked thread.** It did
           `main->wait = {}` then `makeRunning(*main)` "so an invocation has somewhere to attach",
           guarded by `main->status == Ready || main->invocations.empty()`. `invocations.empty()`
           is true for every plain syscall, so the guard was always true and the thread that had
           just called `sce_SleepThread` was made Running again in the same call.
        2. **`EeScheduler::bindMainContextForSyscall()` re-ran `reset()` on EVERY syscall.**
           It tested `m_executorThread == std::thread::id{}` to mean "not set up yet". That is
           permanently true for a driver that never calls `run()` — and the harness never does.
           So every syscall destroyed every thread, semaphore, event flag, alarm and queued event
           the guest had created, and rebuilt the main thread record as `Ready` before parking it
           again. **This is the one that actually explains the boot**: `sce_CreateThread calls=1`
           and `sce_CreateSema calls=7` cannot coexist with eleven `sce_SleepThread` calls on a
           working kernel. Now the "not set up yet" test is `m_threads.empty()`.
        3. **`serviceInvocations()` slept on the host clock** — `processPendingEvents()` paces
           itself to the next VBlank deadline, correct in `run()` and wrong in a service call.
           Now `processPendingEvents(bool mayWait)`; `run()` passes true, a service call false.
        4. **The harness's own watchdog held every boot open for the whole budget.** It slept out
           the deadline then called `requestStop()`, and the harness `join()`ed it. Now it watches
           a `driverFinished` flag.
        New API: `EeServiceResult serviceInvocations(int)` (RanNothing/Ran/Blocked/Stopped) and
        `EeScheduler::canDispatchGuest()`, so a driver never re-enters a parked frame and can tell
        "the guest moved" from "the guest is parked". The harness halts with a new
        `halt=guest_blocked` that names the wait reason and the runnable set.

        **4 new tests, `ps2xTest/src/ps2_thread_block_tests.cpp`, all red before the fix:**
        a thread in `sce_SleepThread` is Waiting/Sleep · a driver that services the transfer does
        not re-enter the sleeping thread · with the main thread asleep the runnable set must
        change and the OTHER thread must run · a woken thread resumes at the PC it published.

        Two of my own conclusions were WRONG on the way and are retracted here: (a) I read
        `perf`'s "99 % libgallium" as a GL present stall — it was 99 % of **6 samples** in GL
        context creation, and the real block was `pthread_join` on my own watchdog; (b) I tried to
        add a `canDispatchGuest()` gate to the shared `driveGuestLikeTheHarness` fixture and broke
        G1.8b/G1.8c, because those tests never call `reset()` and correctly have no threads. The
        gate belongs to a driver that HAS initialised the scheduler.

MEASURED:
        suite:  **462/462 passing** (was 456 + 2 pre-existing GS depth failures; the GS lane
                found those 2 were a stray `ZBUF_1` write in its own test helper, and the depth
                / ATE / AFail / DATE gate is now genuinely green rather than accidentally green)
        boot:   `functions_entered=44579→44578` — **UNCHANGED, and honestly so.** The guest
                reaches the same place it always did; W8 was never what was stopping it.
        **but the instrument is now trustworthy:** 120 s budget → **1.6 s process**,
        `elapsed_ms=1143 guest_phase_ms=1125 harness_tail_ms=18`. Before: 120.6 s.
        guest phase: 44578 entries, distinct_pcs=112, 119 syscalls, 754 MMIO accesses,
        15 transfers, `blocked_on_servicing=0` (a worker was always runnable — the fix is live).

        **GS lane, by the way, is where this got interesting.** It reported the memory map was
        wrong (GS should be at 0x70000000, scratchpad at 0x1F800000) and wrote a red test for it.
        **That was refuted, and the refutation is in the ledger: GT4's own code writes GS
        registers at 0x12000000 (21 `lui 0x1200`, and a CSR-revision probe at 0x100a444 that reads
        0x12001000 and shifts right 16), and uses 0x70002000–0x70003030 as a DMA scratch struct.**
        Our map was already right. The test was removed and the truth pinned by two green tests.
        *A proposed fix that a measurement refutes gets deleted, not softened.*

NEXT:   **W9 — the guest's own idle loop.** The entire guest phase is 1.13 s; nothing is slow, the
        guest simply stops progressing at `0x01000760`, which is `while(1) { sub_010027F0(); }`
        with no syscall, no MMIO and no load in the body. **The boot log contains no write to
        `0x1200xxxx` at all — the GS window is never touched, so milestone 2 is out of reach
        until the guest gets past this loop.**
        1. **What is `sub_010027F0` waiting for?** Decode its body from SCUS_973.28 and find every
           load it makes. If it polls a memory word, name the address and ask who was supposed to
           write it — GT4 created exactly one thread (`sce_CreateThread calls=1`) and now that
           thread survives its sleeps, so check whether the worker is the producer.
        2. **The harness only delivers events when the guest yields.** `processPendingEvents()` is
           reached from `run()` and from a service call, and this loop never yields. If
           `sub_010027F0` is waiting on an EE timer or a vsync interrupt, the driver has to pump
           the scheduler on a clock of its own — that is a driver obligation, not a guest change.
        3. **`VULCAN4 FRAME source=guest` does not exist anywhere in the repo** (`grep` over
           `tools/`: zero hits). The campaign's finish line has no instrumentation, so it cannot
           be reached or missed honestly. The GS lane has half of it: `GifArbiter` now carries a
           delivery observer (`drainedPacketCount()`) whose only real feeder is
           `PS2Memory::submitGifPacket`, which is a genuine guest/synthetic discriminator.
           `GS` does not consume it yet, and one line is missing in `ps2_runtime.cpp` after
           `m_gs.init(...)`: `m_gifArbiter.setDeliveryObserver([this]{ m_gs.noteGuestGifTraffic(); })`.
           **The GS lane ran out of steps mid-change and compiled nothing; the tree was verified
           building and 462/462 green by hand afterwards.** Finish this before trusting any
           "no frame" claim.
        4. **`gsWriteCount()` is structurally always 0 for guest traffic** — the counter is only
           incremented in `writeIORegister()`, but the real guest path is `write32`/`write64`,
           which take the `isGsPrivReg` branch and return without counting. `ps2_memory.cpp` is
           not the GS lane's file and not yet fixed.

        **Do NOT re-chase:** W7's `$t1` / `$fp` / inverted-range story (retracted in G1.8f, and
        the 0x0100f800 loop is 78 % of entries and converging normally) · the 0x70000000 GS
        remap (refuted by GT4's own disassembly, above) · the `libgallium` present stall (6
        samples of GL context creation) · the `sleepCurrent()` code, which was always correct.

MEASURED (artifacts): probes removed and every binary rebuilt clean (`grep -c TPROBE` = 0 in
        tools/harness/vulcan4_harness.cpp; 0 `fprintf` in the generated unit).
        Patch: `tools/patches/ps2recomp-linux-g18g-sleepthread-blocks.patch` (22 files, +2234/-25).
        Boots: `/mnt/ssd/vulcan4-build/run/boot_g18g.log`, `boot_W8_baseline.log` (the 120 s
        before-picture), `boot_W8_final.log`. New: `tools/harness/build_harness.sh` — the harness
        is not a CMake target, so its build was four hand-typed g++ lines and `--target ps2xRuntime`
        silently builds nothing, which measures the OLD runtime and reports a fix that is not in
        the binary. It happened once here.

## 2026-09-30 20:10 · (no dish) · **W9a + W9b fell, W10 named. The guest is now multi-threaded and getting 60 Hz.**

WALL:   W9 → W10. The W8 wall's mechanism is fixed and committed (`de2b3dd`). This pass worked
        the wall W8's fix exposed, which is the honest consequence of fixing W8.

DID:    Two more driver bugs, both found by making the report tell the truth, and both of the
        same family as the ones before: **the instrument, not the thing measured.**

        1. **The driver advanced the main thread's frame while the scheduler ran another.**
           The harness bound `R5900Context &ctx = runtime.cpu()` once, outside its loop. That is
           the MAIN thread's frame. GT4 is genuinely multi-threaded by this point — W8's fix is
           what let the worker thread survive its own sleeps at all — and the moment the scheduler
           switched threads, the driver kept entering the old one. The new report fields said
           exactly that and nothing else:
           `runnable_threads=tid1@prio3:pc=0x0100d8f8(ready),tid2@prio2:pc=0x0100de58(running)`.
           Fixed by re-reading `EeScheduler::currentContext()` EVERY iteration. A reference
           captured outside the loop would be a stale frame wearing the right name, which is worse
           than the bug. `nameCycle()` now takes the context as a parameter, because it reports the
           address a loop polls and the value there — read from the wrong context it prints a
           plausible wrong address.
        2. **The harness was blind to most of the guest.** `serviceInvocations()` enters the guest
           itself; the driver only counted its own loop. So `functions_entered`, `distinct_pcs`,
           the progress detector and the cycle detector all ignored every service-path frame.
           New `EeScheduler::setServiceFrameObserver()` hands the driver each of those frames with
           its live context, and the harness feeds the same counters — one accounting, no second
           opinion.

MEASURED, 120 s budget, before -> after:
        functions_entered       44578 -> 120180
        total_syscall_calls       119 -> 59822
        ee_cycle              9.8 M -> 35,390,188,280
        vsync_tick                  2 -> 7199        (the guest is now getting 60 Hz)
        distinct_mmio_addresses   308 -> 0
        halt  guest_cycle_no_progress -> livelocked_in_syscall
        service_frames (new)        — -> 5,610,851   (NONE of these were visible before)
        suite **462/462**, probes removed (`grep -c TPROBE tools/harness/vulcan4_harness.cpp` = 0)

NEXT:   **W10 — the guest's inline `sce_FindAddress` never converges.** This is the wall, and it
        is now measured rather than inferred:
        - GT4 installed its own handlers: `[SetSyscall] n=131 → 0x010285F8 @ 0x1218C` and
          `n=90 → 0x010285C0 @ 0x120E8`, i.e. 164 bytes apart.
        - A probe over all 512 words of the console kernel's syscall table (physical 0x11F80)
          found **exactly those two non-zero words and nothing else** — the table is not corrupt.
        - Measured **at the guest's own handler's entry**, through the new service-frame observer:
          `a0=0x80000000 a1=0x80080000 a2=0x010285F8`, then `a2=0x010285C0`, with `s3` taking the
          values `0x8001218C` and `0x800120E8`. **So the guest finds BOTH slots, correctly, 164
          bytes apart — exactly as designed — and then re-issues the `0x010285C0` search from
          `0x80000000` with `s2 = 0` forever.** The convergence test is `beq s1,s0` with
          `s1 = s3 - 0x20C` and `s0 = s2 - 0x168`; `s2` never leaves 0, so the two sides can never
          be equal. 5.6 M guest frames and 35 billion EE cycles buying nothing.
        1. **What is `s2` set from, and why is the 0x83 search's result not stored there?** The
           registers are now visible, so this is one probe: watch `s2` and the two `v0` results
           across the whole loop and say which instruction is supposed to move the 0x83 result
           into `s2`. **Do NOT read it off the instruction stream — decoded intent has been wrong
           THREE times on this loop** (it told me `a2` was 0x00EB5F08 and 0x010285A0, and it told
           me the loop was unconditional `while(1)`; both were wrong, and the register measurement
           was right both times). The project's own rule, earned three times.
        2. **The GS lane left its work half-done and nothing has been compiled since.** `GS` does
        not yet consume `GifArbiter::drainedPacketCount()`, and the one line in `ps2_runtime.cpp`
        that wires it is missing. Until both exist, `VULCAN4 FRAME source=guest` **cannot fire
        from a real boot** — and that string does not exist in the repo at all yet, so the
        campaign's finish line currently has no instrumentation. That is the next thing after
        W10's register question, because it is the milestone.
        3. **`gsWriteCount()` is structurally always 0 for guest traffic.** The counter is only
        incremented in `writeIORegister()`; the real guest path is `write32`/`write64`, which take
        the `isGsPrivReg` branch and return without counting. Not fixed — it is in ps2_memory.cpp.
        4. **`PS2_SCRATCHPAD_ALIAS_BASE = 0xF0000000` is dead code.** `ps2IsScratchpadAddress` only
        ever tests `addr >= 0x70000000 && addr < 0x70004000` after an `addr & 0x7FFFFFFF` strip, so
        0xF0000000 is masked *into* the primary test and the constant is never read. A test asserts
        the alias, so it is asserting a constant nothing consumes. Hardware has no fixed 0xF0000000
        alias; the scratchpad is reached through a TLB EntryLo.S bit.

        **Do NOT re-chase:** the GS window at 0x70000000 (refuted twice — by an independent
        research pass and then by GT4's own disassembly, which writes GS at 0x12000000) · the
        `libgallium` present stall (6 samples of GL context creation) · the syscall table's
        contents (probed: correct) · W7's copy-loop story.

        **Method, fourth time:** every wall in this project so far has been the instrument.
        Detector too trigger-happy (W7), watchdog holding the boot open (W8c), driver running the
        wrong thread (W9a), driver blind to the guest (W9b). **When a number looks wrong, ask what
        is measuring before asking what is broken.**
