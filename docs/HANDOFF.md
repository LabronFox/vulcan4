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

## 2026-09-30 21:05 · (no dish) · **W10 is ONE register. Two of my own claims retracted.**

WALL:   W10 — the guest's inline `sce_FindAddress` convergence loop.

DID:    **No code changed.** Probed, measured, removed every probe, rebuilt clean
        (`grep -c TPROBE tools/harness/vulcan4_harness.cpp` = 0), suite **462/462**.

MEASURED — the chain, and every link in it is now measured rather than inferred:
        - `[SetSyscall] n=131 → 0x010285F8 @ 0x1218C`, `n=90 → 0x010285C0 @ 0x120E8`, 164 apart.
        - A probe over all 512 words of the console kernel's syscall table (physical 0x11F80)
          finds **exactly those two non-zero words and nothing else.** The table is not corrupt.
        - A probe over physical `[0, 0x80000)` finds 131,068 zero words, 4 non-zero, `firstZero=0x0`.
        - The guest's own scan is `sub_010285F8`, emitted correctly, and is
          `while (a0 < a1) { if (*a0 == a2) return a0; a0 += 4; }` over `[0x80000000, 0x80080000)`.
          **It works** — the caller ends up holding `s3 = 0x800120E8`, the 0x5A slot, correctly.
        - **The failure is `$s2` and nothing else.** The convergence test is
          `beq s1, s0` with `s1 = s3 - 0x20C` and `s0 = s2 - 0x168`, so it needs `s3 - s2 == 0xA4`.
          `s3` is right. `s2` is **0**, measured at the loop across 24 consecutive entries, with
          `sp` at the top of the guest stack and `ra = 0`. The two sides can never be equal.
        - Cost of that one register: **7,602,834 guest frames, 17,695,381,560 EE cycles,
          3,600 vsync ticks, 6 distinct PCs, zero MMIO accesses.**
        - **The guest's own bytes in `0x01028500–0x01028780` contain no instruction that writes
          `$s2` or `$s3` at all** (scanned the whole range for every instruction form that writes
          a GPR). So they are callee-saved **inputs** from an ancestor frame, and one of them was
          destroyed between the frame that set it and the frame that reads it.

RETRACTED — do not re-adopt either of these, both mine, both from this pass:
        1. **"The 64-bit branch comparison is the wall."** Every branch in the loop is emitted as
           `GPR_U64(a) == GPR_U64(b)` — 2,791 sites, and it looks exactly like the bug. It is not:
           the operands here are built by `lui a2,0x102` + `addiu`, and `0x0102 << 16` clears the
           high bit, so no sign extension occurs and the 32- and 64-bit forms agree on the precise
           instruction the scan's match depends on. Worked out arithmetically rather than assumed,
           and it lands on the same answer W7 reached for the same class two dishes ago. **The 64-bit
           compare is still a real hardware-semantics defect and is still unfixed — it is just not
           this wall, for the third time.**
        2. **"`lw` is returning 1 for a zero word."** A first probe appeared to show `v0 = 1` while
           `rdram` held 0 at the same address. It was the probe that was wrong: it sampled at the
           `lw` with `$a0` already advanced, so it compared one word's value against the next
           word's address — and `v0 = 1` is the loop's own `sltu a0,a1` result sitting in the
           previous branch's delay slot. Sampling *after* the load showed the instruction is never
           re-entered, which is what a resume case looks like. **If a probe disagrees with the
           code, suspect the probe before the code — twice now.**

NEXT:   1. **One measurement closes W10: watch `$s2` at the `jal` that calls `sub_01028680`, and at
           that function's entry.** Non-zero at the call, zero at the entry ⇒ a recompiled callee
           clobbered a callee-saved register across a yield. Zero at both ⇒ the guest's own path
           never computed it and the hunt moves up one frame. This is now a two-sample probe
           through `EeScheduler::setServiceFrameObserver`, which already carries the live context.
        2. **The red test owed since W7 pass 6, finally with a victim:** a recompiled function that
           yields inside its own epilogue must resume at the restore and leave every callee-saved
           register (`$s0`–`$s7`, `$fp`, `$ra`) intact. Write it BEFORE the fix, and extend it to
           every saved register, not just `$fp` — `$fp` was checked once and retracted twice.
        3. **Then the milestone, which is still far:** the guest has made **zero MMIO accesses** in
           this state. The GS privileged window at `0x1200xxxx` is never touched, so no amount of GS
           work reaches `VULCAN4 FRAME source=guest` until the guest gets past this loop. The GS
           lane's `VULCAN4 FRAME source=guest` emitter is still half-built (`GS` does not consume
           `GifArbiter::drainedPacketCount()`; the one wiring line in `ps2_runtime.cpp` is missing)
           and **that string does not exist in the repo at all**, so the campaign's finish line
           currently has no instrumentation to pass or fail.
        4. `gsWriteCount()` is still never incremented on the guest `write32`/`write64` path
           (ps2_memory.cpp), and `PS2_SCRATCHPAD_ALIAS_BASE = 0xF0000000` is still dead code that a
           test nonetheless asserts. Both cheap; both in files nobody currently owns.

MEASURED (artifacts): `/mnt/ssd/vulcan4-build/run/boot_g18j.log` (60 s, 7.6 M service frames),
        `boot_g18h.log` (the 120 s before-picture), `boot_g18i.log`. Suite 462/462 from
        `tools/PS2Recomp/ps2xTest`. No probes in any shipped file.

## 2026-09-30 21:40 · (no dish) · **W10 closed to "a callee is clobbering `$s3`". One more pass, then I stopped.**

WALL:   W10. Still open, and now reduced to a property with a name: **callee-saved registers are not
        surviving the calls inside the guest's own syscall-table verification loop.**

DID:    **No code changed.** Probed, measured, removed every probe, rebuilt clean
        (`grep -c TPROBE tools/harness/vulcan4_harness.cpp` = 0), suite **462/462**.

MEASURED:
        ```
        F1  pc=0x10285f8 s0=0x1035350 s1=0x0 s2=0x0 s3=0x0        sp=0x1fffff0 ra=0x0
        F2  pc=0x10285f8 s0=0x1035350 s1=0x0 s2=0x0 s3=0x8001218c  sp=0x1fffff0 ra=0x0
        F3  pc=0x10285f8 s0=0x1035350 s1=0x0 s2=0x0 s3=0x800120e8  sp=0x1fffff0 ra=0x0
        F4..F40  identical to F3
        ```
        Three facts, and together they close the question the last entry left open:
        1. **`$s3` is being written** — `0x0 → 0x8001218C → 0x800120E8` — and the guest's own bytes
           in `0x01028500–0x01028780` contain **no instruction that writes `$s2` or `$s3`**. So a
           callee is writing a callee-saved register, which the ABI forbids.
        2. `$s2` is 0 in every frame, and the convergence test needs `s3 - s2 == 0xA4`. That is the
           livelock, unchanged.
        3. The caller of `sub_01028680` is the bare chain at `0x01028790` —
           `jal 0x01028540` / `jal 0x01028680` / `jal 0x01028C70` / `jal 0x01028DE8` — with **no
           register setup at all**, and `dispatchGuestBranch` runs it inline, so **the call site is
           never a yield point and cannot be sampled.** The chain's inputs are whatever the
           previous callee left behind, which is exactly what is corrupted.

NEXT:   1. **The red test owed since W7 pass 6, now with a victim:** a recompiled function that
           yields inside its own epilogue must resume at the restore and leave every callee-saved
           register intact — `$s0`–`$s7`, `$fp`, `$ra`, not just `$fp`. **Extend it to all of them.**
           `$fp` was checked once in W7 pass 6 and the claim was retracted twice; a test that names
           one register finds one register. The bug now demonstrated is that a callee *writes* a
           saved register, which no existing test covers at all.
        2. **The scan loop `sub_010285F8` is a leaf with no prologue** and its `jr ra` delay slot
           is `daddu $s0, $a0, $zero` — it writes `$s0` with nothing saved. `s0 = 0x01035350`
           (the guest's override-table pointer) is stable across every frame, so whoever wrote it
           wrote the same value. **Check whether the generated leaf saves and restores `$s0`, and
           whether it should: on the disc it does not, which is either a guest ABI violation or a
           sign our recompile of `jr ra` + delay slot drops the delay slot's register write.** The
           generated code does emit `SET_GPR_S32(ctx, 16, ...)` in the delay slot, so start from
           the callee side instead: does `sub_010285F8` restore `$s0` on the way out? It has no
           epilogue that could.
        3. **I am stopping here deliberately.** W7 took ten measurement passes and the thing it
           measured turned out to be the detector. This loop has now had four, and the last two
           moved the wall from "the syscall table is wrong" to "a callee-saved register is not
           surviving a call" — real progress, but the next step is a **recompiler change** that
           regenerates all 707 functions, not another probe. Probe exhaustion is the signal to
           change lane, not to keep probing. `docs/CAMPAIGN.md` W10 carries the state.
        4. **Still the milestone, and still far:** the guest has made **zero MMIO accesses** in this
           state, the GS privileged window at `0x1200xxxx` is never touched, and
           `VULCAN4 FRAME source=guest` **does not exist in the repo**. The GS lane's emitter is
           half-built (`GS` does not consume `GifArbiter::drainedPacketCount()`; the one wiring line
           in `ps2_runtime.cpp` is missing). Finishing that is worth more than this loop, because
           without it the campaign's stop condition cannot be observed even if it happens.

MEASURED (this session, cumulative): suite 456 → **462/462**; `functions_entered` 44578 → 120182;
        `vsync_tick` 2 → 7199; `ee_cycle` 9.8 M → 35.4 G; `service_frames` 0 (invisible) →
        15,029,344 (counted); `harness_tail_ms` "the rest of the budget" → **0**. The guest went from
        one thread and an idle loop to a 60 Hz boot that stops at a precisely named register.

        **THREE THINGS THE VERIFIER CAUGHT IN THE ABOVE, all mine, all now corrected in place:**

        1. **"boot wall-clock 120.6 s → 1.6 s" is FALSE and I should not have written it.** The 1.1 s
           runs were the *blind* driver (W9b) falsely declaring the guest stuck at 44,578 entries.
           Once service-path frames became visible the guest legitimately consumes its budget: a
           120 s run now ends `elapsed_ms=120001 guest_phase_ms=120001 harness_tail_ms=0`. The
           watchdog fix is still real and still worth having — its entire contribution is that the
           post-loop tail is now 0..18 ms instead of the remaining budget — but "1.6 s" measured a
           broken instrument. **Fifth time in this project the instrument produced the number, and
           fifth time it was me who believed it.**
        2. **"`VULCAN4 FRAME source=guest` does not exist anywhere in the repo" is FALSE as
           written.** It exists in `docs/CAMPAIGN.md`, in this file, in `docs/FIRST-BOOT.md` and in
           one source comment. The true claim is that **nothing prints it.**
        3. **"a real 60 Hz multi-threaded boot" overstates the end state.** Two threads were
           measured mid-run (`tid1@prio3` ready / `tid2@prio2` running — that is what named W9a), but
           the 120 s run *ends* with `runnable_threads=tid1@prio0:pc=0x01028610(running)`, one thread.
           The worker is gone by then.

        Plus one that no grep of the repo could have found: **a hand-injected probe was still
        shipping.** A `[BR864]` `fprintf` sat at line 77477 of the *generated* translation unit,
        injected into the artefact by an earlier session and absent from the recompiler source.
        Dormant — the guest no longer reaches W7's loop — but it was in the binary. **Removed; the
        unit now has 0 `fprintf` and 0 `BR864`.** A probe in a build artefact is still a probe, and
        "probes removed" has to mean it in the thing that links.

## 2026-09-30 22:10 · verifier pass · **4 PASS / 4 FAIL, and the 4 FAILs were all mine. Corrected.**

WALL:   none new. This turn exists because **a commit is not proof** and the independent verifier
        found four claims of mine that do not survive being re-run from scratch.

DID:    Re-ran every gate from scratch, and corrected the record in place rather than defending it.

        - **C1 suite 462/462 — PASS.** Binary was current; no rebuild needed.
        - **C2 four G1.8g tests present and passing — PASS.**
        - **C3 "120 s budget returns in ~1.6 s" — FAIL, and the claim is dead.** The verifier measured
          `real 2m0.466s`. It is right, and so am I, now that I have looked: the fast runs were the
          **blind** driver (W9b) declaring the guest stuck at 44,578 entries. The watchdog fix is
          real but its whole contribution is `harness_tail_ms` — "the rest of the budget" → 0..18 ms.
          Corrected in `docs/CAMPAIGN.md` W8c and above.
        - **C4 boot numbers — PASS** (`functions_entered=120197`, `total_syscall_calls=59822` exact,
          `vsync_tick=7199` exact, `ee_cycle=35390196344`, `halt=livelocked_in_syscall` exact,
          `distinct_mmio_addresses=0` exact). The verifier also caught that my 7,602,834
          `service_frames` was quoted from a **60 s** run while the surrounding numbers were from a
          **120 s** one — **two runs conflated in one sentence.** Their ratio is exactly the ratio of
          the run lengths, which is how it was caught.
        - **C5 "the finish-line marker does not exist anywhere in the repo" — FAIL.** It exists in
          `docs/CAMPAIGN.md`, `docs/HANDOFF.md` ×10, `docs/FIRST-BOOT.md` and one source comment.
          The true claim is that **nothing prints it**, which is worse and more precise: the stop
          condition cannot be reached *or missed* honestly.
        - **C6 "no probes remain" — FAIL, and this one mattered.** A `[BR864]` `fprintf` was still in
          the **generated translation unit** at line 77477, hand-injected into the artefact by an
          earlier session and absent from the recompiler source — so no `grep` of the repository
          could ever find it, and every "probes removed, rebuilt clean" line in this log was true of
          the repo and false of the binary. Removed; the unit now has **0 `fprintf`, 0 `BR864`**,
          and the harness is relinked.
        - **C7 patch currency — PASS**, byte-identical to the nested tree's `git diff` (md5 match).
        - **C8 retractions — the 64-bit branch count is exactly 2,791 with 0 `GPR_U32`, matching the
          retracted figure, so "real but unfired" stands. The second retraction (`lw` returning 1)
          was in this file but **not** in `docs/CAMPAIGN.md`; added.**

        Also corrected: **"a real 60 Hz multi-threaded boot"** overstated the end state. Two threads
        were measured mid-run — that is what named W9a — but a 120 s run *ends* with
        `runnable_threads=tid1@prio0:pc=0x01028610(running)`, one thread. And the working tree had one
        uncommitted whitespace deletion in the harness; reverted, so `git status` is clean apart from
        the audio lane's `tools/audio/`.

MEASURED (post-correction, 120 s, rebuilt from clean):
        `functions_entered=120182  halt=livelocked_in_syscall  distinct_pcs=6  service_frames=15029344`
        `elapsed_ms=120001  guest_phase_ms=120001  harness_tail_ms=0  total_syscall_calls=59822`
        `distinct_mmio_addresses=0  total_mmio_accesses=0  runnable_threads=tid1@prio0:pc=0x01028610(running)`

NEXT:   Unchanged and stated plainly: **W10 is the wall** (a callee is writing a callee-saved
        register inside the guest's own syscall-table verification loop), and **the campaign's
        finish-line marker is never emitted**, so the guest reaching a frame cannot currently be
        observed even if it happens. Those two are the next dishes. Nothing in this pass moved
        either.

        **The standing lesson, fifth instance, and it is the same one every time:** W7's detector,
        W8's watchdog, W9a's wrong thread, W9b's blindness, and now a headline wall-clock number
        that was really a blind detector's lie. **When a number is surprising, ask what is measuring
        before asking what is broken — and when a subagent disagrees with a commit, the subagent is
        re-running it and the commit is a story.**

## 2026-09-30 22:45 · (no dish) · **W10 restated. THREE of my own decoder errors, all retracted. I am stopping.**

WALL:   W10, and it is now the narrowest it has ever been: **one comparison in the guest's own
        hand-written table-verification scan matches the wrong word.**

DID:    **No code changed.** Probed, measured, removed every probe, rebuilt clean
        (`grep -c "TPROBE tools/harness/vulcan4_harness.cpp"` = 0, 0 `fprintf` in the generated
        unit), suite **462/462**, boot re-measured from clean.

MEASURED:
        ```
        pc=0x10286dc  a0=0x80000000 a1=0x80080000 a2=0x010285F8  v0=0x8001218C   (first call)
        pc=0x10286dc  a0=0x80000000 a1=0x80080000 a2=0x010285F8  v0=0x800120E8   (every call after)
        ```
        **The guest searches physical `[0, 0x80000)` for the word `0x010285F8` and gets back
        `0x800120E8` — the slot that holds `0x010285C0`.** It starts from the beginning of the
        window every time. `s3` therefore receives the wrong slot, `s2` receives 0, the convergence
        test (`beq s1,s0` with `s1 = s3 - 0x20C`, `s0 = s2 - 0x168`) can never pass, and the guest
        spins: **7,616,518 guest frames, 17,695,381,560 EE cycles, 3,600 vsync ticks, 6 distinct
        PCs, zero MMIO accesses.**

        **The syscall table is exonerated, by a slot-watch probe:** exactly two writes to
        0x120E8/0x1218C in the whole run, both at `pc=0x01028640` (the two `sce_SetSyscall` calls),
        and no change afterwards. The slots hold `0x010285C0` and `0x010285F8` for the entire run.

        **The recompile is exonerated too.** The scan is emitted exactly as
        `while (a0 < a1) { if (*a0 == a2) return a0; a0 += 4; }`, returns its result in `$v0` via
        the delay slot of `jr ra`, and the caller reads `$v0`. A save/restore audit of **all 707**
        generated functions finds **694 clean**; the 13 that write `$fp`/`$ra` unsaved are all at
        function tops or save via a frame-pointer base, which my pattern did not match.

        **So the scan can only return a non-zero `$a0` from its match exit — and it is matching
        `0x010285C0` against `a2 = 0x010285F8`.** That is the wall, and it is one comparison.

RETRACTED — THREE of my own claims, all decoder errors, all mine:
        1. **"the guest's own bytes in `0x01028500–0x01028780` never write `$s2` or `$s3`."** FALSE.
           I mis-split bit 11 of the `rd` field. `0x0040902d` is `daddu $s2, $v0, $zero`, not `$s0`.
        2. **"a callee is clobbering `$s3`."** FALSE, and it follows from (1). The loop writes `$s3`
           and `$s2` itself, from the two scan results. The generated unit's own comments say so:
           `// 0x10286dc: daddu $s3, $v0, $zero`, `// 0x10286f4: daddu $s2, $v0, $zero`.
        3. **"`0x0080102d` is `daddu $s0, $a0, $zero`."** FALSE — it is `daddu $v0, $a0, $zero`
           (`// 0x1028634`), which is why the scan returns its result in `$v0` and the caller's
           `$v0` read is right. I had used this wrong decode to claim the return convention
           mismatched.
        Also re-examined and **still not proven**: the 64-bit branch comparison
        (`GPR_U64(a) == GPR_U64(b)`, 2,791 sites, 0 `GPR_U32`) is the only surviving candidate, but
        `lui a2,0x102` clears the high bit so the two forms agree on the operands as constructed.
        **Do not "fix" it on the strength of that.**

        **METHOD, and it is the third time this session: THE GENERATED UNIT'S COMMENTS ARE THE
        AUTHORITY, NOT A HAND DECODER.** Four separate conclusions this session came from reading
        the disc myself were wrong — the loop was not unconditional, `a2` was not `0x00EB5F08`,
        the two `daddu`s did not target `$s0`, and the return convention did not mismatch. Every
        one was caught only by cross-checking against `// 0x...: 0x... <mnemonic>` comments the
        recompiler emitted from its own instruction table. **Write the decoder once, correctly,
        or read those comments. Do not hand-split R5900 fields in a shell one-liner.**

NEXT:   1. **One measurement closes W10.** Print `v0` and `a0` **immediately after** the load at
           `0x01028610` and **immediately before** the `jr ra` at `0x01028630` — *not* at the load,
           which samples the previous word against the next address (that mistake cost two probes
           earlier today). The question is one thing: **at the match exit, what is `a0`, what was
           the word at `a0`, and what is `a2`?** If the word at `a0` really is `0x010285C0` and
           `a2` is really `0x010285F8`, the compare is broken and the fix is in the branch
           emission. That is the one change I would make next.
        2. **Then the milestone, which this wall blocks and which is still not instrumented.** The
           guest has made **zero MMIO accesses** in this state and the GS window at `0x1200xxxx` is
           never touched, so nothing can reach `VULCAN4 FRAME source=guest` — and **nothing prints
           that string.** The GS lane's emitter is half-built (`GS` does not consume
           `GifArbiter::drainedPacketCount()`; the one wiring line in `ps2_runtime.cpp` after
           `m_gs.init(...)` is missing). Finish it while W10 is worked, because a campaign whose
           stop condition cannot be observed is a campaign that cannot finish.
        3. `gsWriteCount()` is still never incremented on the guest `write32`/`write64` path, and
           `PS2_SCRATCHPAD_ALIAS_BASE = 0xF0000000` is still a constant nothing reads that a test
           nonetheless asserts. Both cheap, both unowned.

MEASURED (this session, final, from clean): suite **462/462**; 60 s boot
        `functions_entered=60091  service_frames=7616518  ee_cycle=17695381560  vsync_tick=3600`
        `elapsed_ms=60009  harness_tail_ms=7  total_syscall_calls=29914  distinct_mmio_accesses=0`
        Boot artefacts: `/mnt/ssd/vulcan4-build/run/boot_g18{k,j}.log`, `boot_verify.log`.
        Patch byte-identical to the nested tree (`md5 fc7164f9ba4ce0becd5fc737cfe1b60b`).

## 2026-09-30 23:05 · (no dish) · **last pass on W10: the exits are inline, so one more exoneration falls**

WALL:   W10. Unchanged in substance; this pass **removes** one of the two things I had exonerated.

DID:    **No code changed.** Probed, measured, removed every probe, rebuilt clean, suite 462/462.

MEASURED — two things, one of them a correction to my own previous entry:
        1. **The scan's exits are INLINE and are therefore not observable.** `0x0102862C` (match),
           `0x01028630` (`jr ra`) and `0x01028624` (the `a0 >= a1` exit) never appear as entry PCs —
           not in the harness loop and not through `EeScheduler::setServiceFrameObserver`, because
           the loop only yields at `0x01028610` (the `eeCheckpointDue()` on the back edge). The
           whole 131,072-word pass runs inside ONE service frame. **So the one measurement my last
           entry nominated cannot be made from a driver, at all** — it needs a probe inside the
           generated scan, and hand-injecting one into the build artefact is exactly what the
           verifier caught shipping in this session.
        2. **⟹ MY "the syscall table is exonerated" CLAIM IS RETRACTED.** The slot-watch probe
           samples physical 0x120E8/0x1218C **once per harness-loop iteration**, and in this livelock
           there are only ~60,000 such iterations against 7.4 MILLION service frames. It saw two
           writes because that is all it could see, not because nothing else happens. **A probe that
           samples one place cannot exonerate another place**, and I used it as if it could.
           The scan can only return a non-zero `$a0` from its match exit, and it returns
           `0x800120E8`, so **the word at physical 0x120E8 must equal `a2 = 0x010285F8` at the moment
           of the match.** Our runtime logged `0x120E8 = 0x010285C0` at install time. Either
           something rewrites that slot inside the loop, or the guest's scan reads a word it did not
           write. **Both are open and neither is where the previous entry said it was.**

        Still standing from the previous pass: the recompile of the scan is correct
        (`while (a0 < a1) { if (*a0 == a2) return a0; a0 += 4; }`, result in `$v0` via the `jr ra`
        delay slot, caller reads `$v0`), and **694 of 707** generated functions are clean on
        save/restore discipline.

RETRACTED — one more, added to the three already recorded:
        4. **"the syscall table is exonerated by a slot-watch probe."** FALSE, for the sampling
           reason above. The recompiler exoneration stands; the table exoneration does not.

NEXT:   1. **Sample inside the loop, not at its edges — and in the runtime, not the artefact.**
           `PS2Runtime` already has `ps2TraceGuestWrite` on every guest store path. One bounded
           watch on physical 0x120E8 (say: first 64 writes, then suppressed) reporting the writing
           PC, would settle it immediately and is a legitimate permanent diagnostic rather than a
           hand-injected `fprintf` in generated code. **Put it behind the same discipline the
           verifier just enforced: no probe survives in a build artefact.**
        2. **If nothing writes 0x120E8, the compare is the bug** and the 64-bit branch emission
           (`GPR_U64` at 2,791 sites) becomes the fix — but *only after* point 1 is closed, because
           the two explanations predict the same probe output otherwise.
        3. **The milestone is still uninstrumented and still blocked.** Zero MMIO accesses; the GS
           window at `0x1200xxxx` is never touched; nothing prints `VULCAN4 FRAME source=guest`.
           Finish the GS lane's emitter (`GS` must consume `GifArbiter::drainedPacketCount()`, and
           `ps2_runtime.cpp` needs `m_gifArbiter.setDeliveryObserver(...)` after `m_gs.init(...)`).

MEASURED (final, from clean): suite **462/462**; 60 s boot `functions_entered=60089`
        `service_frames=7433490  total_syscall_calls=29914  distinct_mmio_addresses=0`
        `elapsed_ms=60010  harness_tail_ms=8`. Artefact `/mnt/ssd/vulcan4-build/run/boot_final.log`.

## 2026-09-30 23:50 · G1.8h · **the guest's scan is CORRECT. The hook that proved it is now permanent.**

WALL:   W10, and this pass **removes the last false suspect** and names the real shape.

DID:    **Added the hook that was missing, in the runtime, and a build trap it exposed.**

        1. `ps2TraceGuestWrite` is called by every `WRITE*` macro and every `PS2Runtime::StoreN`, and
           was a **no-op stub**. It is now a real observer: `PS2GuestStoreObserver` +
           `ps2SetGuestStoreObserver()`, a plain function pointer, null-checked, one predictable
           branch per store. **It lives in the header the generated code already includes, so it
           survives a regeneration** — which the hand-injected `[BR864]` `fprintf` did not, and
           which the verifier caught shipping.
        2. Its symmetric partner, because the read side is the one that mattered: `ps2TraceGuestRead`
           + `PS2GuestLoadObserver`, called from the fast path of `READ8/16/32/64`. A "special"
           address already reports through `PS2Runtime::LoadN`, so both paths are covered.
        3. **`tools/harness/build_harness.sh` had the same class of trap one level deeper, and it
           cost a whole measurement pass.** It rebuilt `ps2_recompiled_functions.o` when the `.cpp`
           was newer — but that object is compiled from macros in `ps2_runtime_macros.h`, so
           changing a header changes the generated code's MEANING without changing the `.cpp`'s
           mtime. The observer was installed, fired **zero times**, and that reads exactly like
           "the guest never does the thing", which is the most expensive kind of wrong. The script
           now also rebuilds when either `ps2_runtime_macros.h` or `ps2_runtime.h` is newer.

        With the store hook alone: **zero guest stores to physical `0x120E0`–`0x121A0` in the entire
        run.** With the load hook, the decisive 20 lines:

        ```
        LOAD pc=0x1028610 phys=0x120e8 value=0x010285C0  a2=0x010285F8    (search for 0x83 handler)
        LOAD pc=0x1028610 phys=0x1218c value=0x010285F8  a2=0x010285F8    -> MATCH, correct
        LOAD pc=0x1028610 phys=0x120e8 value=0x010285C0  a2=0x010285C0    (search for 0x5A handler)
        LOAD pc=0x1028610 phys=0x120e8 value=0x010285C0  a2=0x010285C0    -> MATCH, correct, x∞
        ```

        **THE GUEST'S SCAN IS CORRECT.** It reads the word, compares it to the target, and stops on
        the match. The table is right. The compare is right. The recompile is right (694/707
        functions clean on save/restore). **All three of my standing suspects are dead, and each died
        to a measurement rather than an argument.**

        Also measured, and it closes the frame inventory: over a whole run the service path executes
        **exactly two PCs** — `0x01028610` (1,488,551 frames) and `0x010285f8` (5,982) — and the
        driver's own loop executes six. **`0x010286F0`, `0x01028708`, `0x0102871C` and `0x01028738`
        are never entered at all**, though they are resume cases. The retry half of the convergence
        loop therefore runs entirely INLINE inside one service frame and cannot be sampled by any
        driver. That is a property of the guest's code shape, and it is why six probes failed.

MEASURED (this pass, 60 s, from clean): suite **462/462**; `functions_entered=60088`
        `service_frames=7461176  total_syscall_calls=29914  distinct_mmio_addresses=0`
        `elapsed_ms=60010  harness_tail_ms=8`. No probes in any shipped file.
        Boot: `/mnt/ssd/vulcan4-build/run/boot_g18m.log`.

RETRACTED, standing list, all mine: (a) "the guest's bytes never write `$s2`/`$s3`" — decoder error;
        (b) "a callee clobbers `$s3`" — follows from (a); (c) "`0x0080102d` is `daddu $s0`" — it is
        `daddu $v0`; (d) "the syscall table is exonerated" — the probe sampled the wrong rate; (e)
        "the 64-bit branch compare is the wall" — unproven, `lui 0x102` clears the high bit.
        **(f) NEW: "the search returns the wrong slot" — also false.** The load hook shows the search
        returning the RIGHT slot every time. What differs between the first call and the rest is not
        the answer but which code path produced it, and that is the next question, not this one.

NEXT:   1. **The one thing left, and it is now a single question:** on the first pass the guest's
           own handler answers `v0 = 0x8001218C`, which is correct. On every later pass `v0` is
           `0x800120E8`, which is the answer to a *different* question. **Something changes which
           code runs between the two.** The strongest candidate, and it is W5's `hasInvocation()`
           latch in a new costume: `sub_010285F8` yields by **returning** at
           `runtime->eeCheckpointDue()`, not by throwing `EeDispatcherTransfer`. `serviceInvocations`
           gives a call `stepBudget` (4096) steps and then **returns with the invocation still on the
           thread**. `hasInvocation(SyscallOverride, 0x83)` is then true forever, so every later
           `sce_FindAddress` takes `return false` at System.cpp:426 and **falls through to our
           builtin** — which is a plain loop with no checkpoint and answers immediately. **Test it by
           printing `runtime.findEeSyscallOverride(0x83, …)` reachability, or simply by raising
           `stepBudget` and seeing whether the first-pass behaviour becomes permanent.** If it does,
           the fix is that **the step budget must not be able to strand an invocation** — enforce it
           at invocation boundaries, or make an unfinished invocation resume rather than leak.
        2. **A red test is owed for that, and it is the same shape as W5's:** a guest invocation
           whose callee needs more steps than the budget must still complete, and after it
           completes `hasInvocation()` must be false. Written before the fix.
        3. **The milestone, still blocked and still uninstrumented.** Zero MMIO accesses; the GS
           window at `0x1200xxxx` untouched; nothing prints `VULCAN4 FRAME source=guest`. The GS
           lane's emitter is still half-built (`GS` must consume `GifArbiter::drainedPacketCount()`;
           `ps2_runtime.cpp` needs `m_gifArbiter.setDeliveryObserver(...)` after `m_gs.init(...)`).

        **Two permanent tools came out of this session's failures and should not be lost:** the
        store/load observers (in the runtime header, so they survive regeneration) and the
        header-aware rebuild rule in `build_harness.sh`. Both were written because a probe was
        invisible, and "invisible" is the failure mode this project keeps paying for.

## 2026-10-01 00:20 · (no dish) · **W10 is one fact: the guest's own scan runs ONCE. `hasInvocation()` has latched. W5 all over again.**

WALL:   W10. Down to a single boolean, and the boolean is in our code.

DID:    **No shipped behaviour changed.** Probed, measured, removed every probe, reverted the
        step-budget experiment (it is refuted — see below), rebuilt clean, suite **462/462**,
        patch regenerated and byte-identical to the nested tree.

MEASURED — three things, and together they close the wall to one line:
        1. **THE GUEST'S OWN SCAN RUNS EXACTLY ONCE.** The load observer, watching physical
           `0x120E0`–`0x121A0` and printing every non-zero word it reads, produces this and nothing
           else, for the whole run:
           ```
           SCAN pc=0x1028610 a0=0x800120e8 a1=0x80080000 a2=0x010285f8 word@0x120e8=0x010285c0 match=0
           SCAN pc=0x1028610 a0=0x8001218c a1=0x80080000 a2=0x010285f8 word@0x1218c=0x010285f8 match=1
           SCAN pc=0x1028610 a0=0x800120e8 a1=0x80080000 a2=0x010285c0 word@0x120e8=0x010285c0 match=1
           SCAN pc=0x1028610 a0=0x800120e8 a1=0x80080000 a2=0x010285c0 word@0x120e8=0x010285c0 match=1
           ... line 3 repeated to the end of the run
           ```
           Line 2 is search #1 finding `0x010285F8` at `0x1218C` — **correct**. Line 3 is search #2
           finding `0x010285C0` at `0x120E8` — **also correct**. And then line 3, and only line 3,
           for the rest of the run. **`a2 = 0x010285F8` never appears again.** Whatever answers the
           guest from the second `sce_FindAddress` onwards does **not execute one instruction of
           guest code** — a guest search would have had to read `0x120E8` again, and it does.
        2. **OUR BUILTIN DOES NOT GO THROUGH `READ32`.** `computeBuiltinFindAddressResult()` reads
           with `getConstMemPtr(rdram, addr)` + `std::memcpy`, so it is invisible to the load
           observer by construction. **That is why lines 3+ produce no reads: they are not guest
           code.** And the builtin is a plain loop with no checkpoint, so it answers instantly.
        3. **THE DRIVER'S ENTRY HISTOGRAM SHOWS WHY THE GUEST NEVER ADVANCES.** Over a 20 s run the
           driver's own loop enters exactly four addresses:
           ```
           0x010286dc  entries=9969     (sub_01028680, the instruction after its FIRST jal)
           0x01028640  entries=9969     (sub_01028638, `jr ra` after SYSCALL)
           0x01028638  entries=91       (sub_01028638, fresh entry)
           0x01000008  entries=1        (the ELF entry -- entered ONCE, so this is NOT W6)
           ```
           **`0x010286F0` — the resume point after the caller's SECOND `jal` — is entered ZERO
           times**, even though it is a declared resume case in the generated switch and the second
           `jal` publishes exactly it
           (`dispatchGuestBranch(..., 0x10286E8u, 0x10286F0u, DirectCall, "JAL")`). So the caller
           never gets past its first `jal`: it is re-entered at `0x010286DC` and re-issues search #1
           forever. **`0x01028640` and `0x010286DC` are entered the SAME number of times (9,969) —
           every pass goes syscall → wrapper resume → caller resume, and never once reaches the
           second search's resume.**

        **THE MECHANISM, named:** `PS2Runtime::handleSyscall` for an overridden syscall does
        `if (scheduler.hasInvocation(GuestInvocationKind::SyscallOverride, syscallNumber)) return false;`
        and `return false` means "I did not handle it", so the guest's `syscall` **falls through to
        our builtin** instead of reaching the guest's own handler. The guest's handler therefore
        runs once and then never again, and every later call is answered by us. That is
        **W5's `hasInvocation()` latch, word for word**, and the handoff for W5 already described
        the consequence: "a stranded invocation makes hasInvocation() latch, so every later call
        silently falls through to our builtin." The earlier fix made the *driver* service the
        invocation; it did not make the *syscall path* self-healing.

        **So `s2` is never set** (the caller never reaches the instruction that sets it), `s1` is
        computed from a stale `s3`, the convergence test can never pass, and the guest spins. The
        arithmetic that made this look like a decode error — `s1 = s3 - 0x20C` and
        `s0 = s2 - 0x168` coming out equal — is right, and irrelevant, because the code that sets
        `s2` is never reached.

EXPERIMENT RUN AND REFUTED, do not re-run: raising `serviceInvocations`' step budget from 4096 to
        65,536 to 1,000,000 changes **nothing at all** —
        `total_syscall_calls=12468`, `distinct_pcs=6`, `ee_cycle=7373350984`, identical at all
        three. So the invocation is not being stranded by the budget; it is stranded by the latch.

MEASURED (final, from clean, 60 s): suite **462/462**; `functions_entered=60091`
        `service_frames=7481984  total_syscall_calls=29914  distinct_mmio_addresses=0`
        `elapsed_ms=60009  harness_tail_ms=7  blocked_on_servicing=0`. Boot
        `/mnt/ssd/vulcan4-build/run/boot_g18n.log`. Patch regenerated, md5 `8c5a177a8b21f212f4845008b8a3c06e`
        on both sides.

RETRACTED, seventh: **"the search returns the wrong slot."** It does not — the load hook shows the
        search returning the right slot every time it runs. What is wrong is *how many times it
        runs*, which no amount of looking at its return value could ever show.

### CORRECTION TO THE ENTRY ABOVE, made immediately after — the latch is NOT left pending

I put `thread_state=` (per-thread `status` and **`invocationDepth`**) permanently in the boot
report, because a latch that cannot be seen in the report is a latch that costs a day. It reports:

    thread_state=tid1:status=0:invocations=0

**`invocations=0`. `hasInvocation()` is NOT latched at the end of the run.** So the "one fact" above
is wrong in its conclusion, though its three measurements stand. The guest's scan does run only
once for `a2 = 0x010285F8`, and the repeated scan is for `a2 = 0x010285C0` — **and both go through
guest code** (the load observer sees them, and only guest code uses `READ32`). So our builtin is
NOT answering. **The guest is running, repeatedly, the SECOND search, and never the first.**

That inverts the previous entry's mechanism and it is worth being precise about why, because the
inversion is itself the finding: the driver's loop enters `0x010286DC` (after jal #1) 9,969 times
and `0x01028640` 9,969 times, and `0x010286F0` (after jal #2) **zero** times, while the scan that
repeats is the one `a2 = 0x010285C0` — which is set in **jal #2's delay slot** at `0x010286EC`. So
jal #2's *arguments* are being set up on every pass while its *resume point* is never taken. Those
two facts cannot both be true of a straight-line path, and the only place they can be reconciled
is `PS2Runtime::dispatchGuestBranch`, which is the next thing to read.

**What `dispatchGuestBranch` does, from the source (ps2_runtime.cpp:1345), and why it is the
suspect:**

    ctx->pc = targetPc;                                   // 1353
    if (m_eeScheduler && m_eeScheduler->checkpointDue(kGuestDispatchCycles))
    {
        ... [Yield] trace ...
        return false;                                     // 1383
    }

and the generated caller is:

    ctx->pc = 0x1028638u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1028638u, 0x10286D4u, 0x10286DCu, DirectCall, "JAL")) { return; }
    ctx->pc = 0x10286DCu;
    ...
    ctx->pc = 0x1028638u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1028638u, 0x10286E8u, 0x10286F0u, DirectCall, "JAL")) { return; }
    ctx->pc = 0x10286F0u;

**Note what the second `jal` publishes: `fallthroughPc = 0x10286F0`, and `0x10286F0` is a declared
resume case.** It is correct as written. The one thing that is NOT handled in this shape is the case
where the callee **throws `EeDispatcherTransfer`** rather than returning: the throw skips
`ctx->pc = 0x10286F0u`, and the wrapper's own `jr ra` is what lands the caller there — via
`$ra`, which the generated code set to `0x10286F0`. That works. **But if the callee throws while
`ctx->pc` still holds `targetPc` and the throw is caught somewhere that resumes the CALLER's frame
rather than the callee's, the caller's resume PC is whatever the callee left.** That is the one
shape left, and it is a runtime bug, not a recompiler one.

NEXT:   1. **One probe, and it closes this.** Put the existing `[Yield]` trace on the
           `dispatchGuestBranch` path for **every** return, not only the checkpoint one — i.e.
           report `(sourcePc, targetPc, fallthroughPc, returned)` for the first few hundred
           dispatches whose `targetPc == 0x1028638`. The answer is then visible directly: if
           `sourcePc = 0x10286E8` never appears, jal #2 is never dispatched; if it appears and
           returns false with `ctx->pc = 0x1028638`, the caller is being re-entered at the callee.
           **The `[Yield]` machinery and the cap already exist at ps2_runtime.cpp:1367 — extend
           that, do not write a new probe.**
        2. The red test for the underlying property, which is broader than W10 and worth having:
           **a guest function that calls a callee which THROWS `EeDispatcherTransfer` must leave the
           CALLER's frame at the fallthrough PC the caller published** — not at the callee's entry.
           That is a contract of `dispatchGuestBranch` + the generated caller, it has no test, and
           it is exactly the property this wall is probing. The fixture is the G1.8b override shape
           already in `ps2_runtime_kernel_tests.cpp`; write it before any fix.
        3. **Then the milestone.** Zero MMIO accesses; GS window at `0x1200xxxx` untouched;
           `VULCAN4 FRAME source=guest` emitted by nothing.

        And the standing correction list is now seven long. **Every one of them was a conclusion
        drawn from a partial view of a system where the guest can yield in the middle of a call
        chain.** The pattern, one more time, because it is the only thing that has actually cost
        this project time: **a measurement that exonerates a component must sample that component at
        a rate that can see it, and a number from a partial view is a hypothesis, not a finding.**

EARLIER-PASS NEXT (kept, because step 1 there is still the cheapest way to disprove this one):
        1. **Print it. One line, and it is the wall:** at `System.cpp:426`, when
           `hasInvocation(SyscallOverride, 0x83)` is true, report which invocation is stranded
           (`invocationStackTop()`), its `context.pc`, and how old it is. If a `SyscallOverride`
           invocation for 0x83 is sitting on the thread with a non-zero `pc`, the latch is proven
           and the fix is whatever failed to pop it.
        2. **The red test, owed twice now (W5 and again here):** a guest override handler that
           throws, strands, or is completed out of band must leave `hasInvocation()` FALSE
           afterwards, and the NEXT call of that syscall must reach the guest's handler. Written
           before the fix. There is a fixture for exactly this in
           `ps2_runtime_kernel_tests.cpp` (`driveGuestLikeTheHarness` + the G1.8b override tests),
           so it is cheap to write and there is no excuse for it not existing.
        3. **The likely fix, and it must not be a hack:** an invocation is popped when
           `activeContext().pc == 0`. Something is leaving it with a non-zero `pc` forever. Either a
           guest function that returns to a `ra` that is not 0 (an invocation whose `ra` is the
           caller's, so `jr ra` never reaches pc 0), or a resume case that re-enters the function
           body instead of the epilogue. **Note the first candidate: `sub_010285F8` yields by
           RETURNING at `eeCheckpointDue()`, and its `jr ra` delay slot sets `$v0`. If the
           invocation was entered with `ra = 0` that returns pc = 0 correctly — so check the value
           the invocation actually carries.**
        4. **Then the milestone.** Zero MMIO accesses; the GS window at `0x1200xxxx` untouched;
           `VULCAN4 FRAME source=guest` emitted by nothing. The GS lane's emitter is still
           half-built. **This is the second consecutive wall whose real cost was a latch in our
           own runtime rather than anything the guest did.**

## 2026-10-01 00:55 · (no dish) · **W10 is one `$ra`. The last measurement, and it is a straight line to the bug.**

WALL:   W10. No shipped behaviour changed. Probed, removed every probe, suite **462/462**.

MEASURED — the dispatch trace, filtered to `targetPc == 0x01028638` (the guest's `sce_FindAddress`
        wrapper). Twenty-four lines, and they are the whole wall:

        n=1  source=0x10286d4  fallthrough=0x10286dc  ra=0x10286dc  s3=0x0         s2=0x0
        n=2  source=0x10286e8  fallthrough=0x10286f0  ra=0x10286f0  s3=0x8001218c  s2=0x0
        n=3  source=0x10286e8  fallthrough=0x10286f0  ra=0x10286f0  s3=0x800120e8  s2=0x0
        n=4..24  identical to n=3

        Three things, each decisive on its own:

        1. **PASS 1 IS ENTIRELY CORRECT.** `jal #1` (source `0x10286D4`) runs, `s3` becomes
           `0x8001218C` — the 0x83 slot, found by the guest's own scan. Then `jal #2`
           (source `0x10286E8`) runs with `ra = 0x10286F0`, exactly as generated code intends.
           `s2 = 0` at that instant is correct, because `daddu $s2,$v0` is at `0x010286F4`, *after*
           `0x010286F0`.
        2. **FROM PASS 2 ONWARD `jal #1` NEVER RUNS AGAIN.** Every subsequent dispatch is
           `source = 0x10286E8`. Yet the driver's entry histogram says the loop enters
           `0x010286DC` — **`jal #1`'s fallthrough — 9,969 times, and `0x010286F0` — `jal #2`'s
           fallthrough — ZERO times.** So the caller is being resumed at `jal #1`'s resume point
           after `jal #2` has run. **Those two facts cannot both be true of a straight-line path,
           and this is the contradiction that names the bug.**
        3. **`s3` CHANGES FROM `0x8001218C` TO `0x800120E8` BETWEEN n=2 AND n=3.** `$s3` is
           callee-saved (r19) and the only instruction that writes it in this function is
           `daddu $s3,$v0,$zero` at `0x010286DC`. **`0x800120E8` is `jal #2`'s result.** So the
           instruction at `0x010286DC` executes again, after `jal #2` already ran, and stores
           `jal #2`'s answer into `$s3`. **That is why `s1 = s3 - 0x20C` is wrong and the
           convergence test can never pass** — and it explains, at last, the `s1`/`s0` arithmetic
           that was correct on paper and never true in the machine.

        **THE MECHANISM, and it is `$ra`:** the caller reaches `0x010286F0` only if
        `dispatchGuestBranch` RETURNS for `jal #2`. When the callee instead **throws
        `EeDispatcherTransfer`** (which `sce_FindAddress` does, every time, because the syscall
        override defers), that assignment — `ctx->pc = 0x10286F0u;` — is skipped. The caller's
        resume must then come from the wrapper's `jr ra`, via `$ra`, which the generated `jal` set
        to `0x10286F0`. **So `$ra` is the only thing standing between the caller and the right
        resume point, and the caller demonstrably lands at `0x010286DC` instead.** Something between
        the `jal` and the `jr ra` overwrites `$ra` with `0x010286DC` — and the one thing that runs
        in between and touches the context is **the syscall-override invocation, which executes
        the guest on a COPY of the frame and copies a result back**.

        `System.cpp:442` copies back only `parent.r[2]`, which is correct and is not the problem.
        So the overwrite is not in the copy-back. It is in **which frame the invocation is attached
        to and which frame `activeContext()` returns while it is pending** — `GuestThread::
        activeContext()` returns `invocations.back().context` while an invocation is queued, so
        every register the handler touches lands in the COPY, and every resume the handler performs
        happens on the copy, **not on the caller's frame.** The caller's `$ra` is only safe if
        nothing ever writes the invocation's copy back except `$v0`. That is the invariant to test.

MEASURED (final, from clean, 60 s): suite **462/462**; `functions_entered=60091`
        `service_frames=7481984  total_syscall_calls=29914  distinct_mmio_addresses=0`
        `elapsed_ms=60009  harness_tail_ms=7  thread_state=tid1:status=0:invocations=0`.
        Boot `/mnt/ssd/vulcan4-build/run/boot_g18n.log`.

NEXT:   1. **ONE probe, and it is a two-line print:** at the `jr ra` in `sub_01028638`
           (`pc = 0x01028640`), print `$ra` and `ctx->pc`. If `$ra` is `0x010286DC` there, `$ra` was
           clobbered and the next question is who wrote it; if it is `0x010286F0`, then the `jr ra`
           dispatched correctly and the caller is being resumed by something else. **One of those
           two answers ends the wall, and the other moves it one step.**
        2. **The red test, and it is the right one this time — a contract, not a symptom:**
           *while a syscall-override invocation is pending, the caller's frame must be untouched
           except for `$v0`.* Concretely: a caller that sets `$s2`, `$s3` and `$ra`, calls an
           overridden syscall, and is resumed must find all three intact and must land at the
           fallthrough PC it published. The fixture is the G1.8b override shape already in
           `ps2_runtime_kernel_tests.cpp` (`driveGuestLikeTheHarness`), so it is cheap.
        3. **Then the milestone.** Zero MMIO accesses; the GS window at `0x1200xxxx` untouched;
           `VULCAN4 FRAME source=guest` emitted by nothing; the GS lane's emitter still half-built.

        **This is the eighth correction in one session, and the shape of all eight is the same:
        every one came from reading a straight-line path and assuming the guest stayed on it. It
        does not — it yields in the middle of a call chain, into a scheduler that copies frames.
        Read `activeContext()` and the invocation stack before reading the guest.**

## 2026-10-01 01:20 · (no dish) · **W10 IS CLOSED. The caller is being rewound to before its own call, on every deferred syscall.**

WALL:   W10. **Found, measured, and the cause is a bug in the W6 fix itself.**

MEASURED — one probe, twelve lines, and the answer is in every field:

        JRRA pc=0x1028640 ra=0x10286dc v0=0x8001218c s2=0x0 s3=0x0 sp=0x1ffff70
        JRRA pc=0x1028640 ra=0x10286dc v0=0x800120e8 s2=0x0 s3=0x0 sp=0x1ffff70
        ... identical

        Sampled in the DRIVER's loop at `pc = 0x01028640`, which is `sub_01028638`'s `jr ra` — the
        resume point its own `SYSCALL` published. **`$ra` is `0x010286DC` on every single pass.**
        But the dispatch trace proved `jal #2` set `$ra = 0x010286F0` immediately before this. **So
        `$ra` is reverted between the `jal` and the wrapper's `jr ra`.**

        And look at the rest of the frame: **`s2 = 0`, `s3 = 0`** — while the dispatch trace showed
        the caller's `s3 = 0x8001218C` at that moment. **`$v0` is the ONLY register that survived
        (`0x8001218C`, then `0x800120E8`) — and `$v0` is exactly the one register
        `System.cpp:442`'s `onComplete` writes by hand.** The frame has been rewound to the state
        it had *before the callee ran*, for every register except the one the runtime deliberately
        wrote.

DID:    **No shipped code changed yet.** Probed, removed every probe, rebuilt clean, suite
        **462/462**. This entry is the finding; the fix is the next dish.

THE CAUSE, and it is W6's own fix:

        // EeScheduler::serviceInvocations(), on entry:
        if (!m_guestExecuting.load(std::memory_order_acquire)) {
            if (GuestThread *main = thread(kMainThreadId)) {
                main->context = m_runtime.m_cpuContext;      // <-- HERE
            }
        }

        That line was added by G1.8c to stop the scheduler's copy of the main frame going stale
        during W6. It is guarded on `m_guestExecuting`, which is set true only inside
        `EeScheduler::run()`'s own dispatch. **The harness never calls `run()` — it has its own
        loop — so `m_guestExecuting` is ALWAYS false, and this overwrite runs on every single
        service call.**

        And it overwrites the live frame with a **stale** one, because there are two objects:
          * `GuestThread::context` — what the driver is actually executing.
          * `PS2Runtime::m_cpuContext` — updated only by `copyMainContextToRuntime()`.
        The harness, since the W9a fix, enters the guest at `EeScheduler::currentContext()`, which
        for the main thread is `main->context`. **So the driver advances `main->context`, and
        `m_cpuContext` is one publish behind — and every `serviceInvocations` call copies the
        stale one over the fresh one.**

        The exact sequence for one guest `jal`:
          1. caller, on `main->context`, does `jal` #2 → `main->context.r[31] = 0x010286F0`
          2. wrapper's `SYSCALL` throws `EeDispatcherTransfer`
          3. driver calls `serviceInvocations()` → **the refresh above overwrites
             `main->context` with `m_cpuContext`, reverting `$ra` to `0x010286DC`** — and also
             reverting `s3`, `s2` and everything else
          4. the invocation runs, completes, `onComplete` writes `$v0` into `main->context`
          5. `copyMainContextToRuntime()` republishes — now carrying only `$v0`'s update
          6. the driver enters `0x01028640`; `jr ra` goes to `0x010286DC`; `s3 = v0` stores the
             **previous** search's result; the loop repeats forever.

        **So `s3` gets overwritten with `jal #2`'s answer on the next pass — which is exactly what
        the dispatch trace measured (`s3` 0x8001218C → 0x800120E8 between n=2 and n=3) — and
        `s2`, set at `0x010286F4`, is never reached at all because the caller never lands on
        `0x010286F0`.** The guest's convergence loop is fine. Our scheduler is rewinding it.

        **This also explains why the bug was invisible for eleven dishes:** the rewind only costs
        anything when a guest function *yields inside a call chain across a deferred syscall*, and
        until W8 fixed the sleep semantics the guest never got that far. Every measurement before
        tonight was of a boot that had not yet reached code sensitive to it.

NEXT:   1. **THE RED TEST, and it is four lines of fixture and a contract:**
        *a guest function that sets `$s2`, `$s3` and `$ra`, calls an overridden syscall, and is
        resumed must find all three intact and land at the fallthrough PC it published.* It fails
        today. The fixture is the G1.8b override shape already in `ps2_runtime_kernel_tests.cpp`
        (`driveGuestLikeTheHarness`), and it is the test the W5 and W10 notes both asked for.
        Write it BEFORE the fix and watch it go red with exactly the measured signature:
        `$ra` and `$s3` reverted, `$v0` correct.
        2. **THE FIX — and it must be decided, not guessed, because the refresh exists for a
        reason (W6) and cannot simply be deleted.** The refresh is only correct when the driver is
        NOT the thing advancing the main frame. Since the W9a fix the driver advances
        `currentContext()`; for the main thread that IS `main->context`, so **there is nothing to
        refresh.** The honest fix is to make the scheduler's copy authoritative only when it is
        authoritative: either (a) publish `m_cpuContext` on every guest yield so the two cannot
        diverge, or (b) guard the refresh on "is the driver driving `main->context` directly",
        which is now always true for the harness, or (c) have the driver drive `runtime.cpu()` and
        the scheduler read *that*, restoring the G1.8c contract honestly. **Pick with a test, not
        with a preference — and note (c) is the one that makes the two-frame design disappear.**
        3. **Then the milestone.** Zero MMIO accesses; the GS window at `0x1200xxxx` untouched;
           `VULCAN4 FRAME source=guest` emitted by nothing; the GS lane's emitter still half-built.

        **NINTH correction this session, and the lesson is the one the ledger has been repeating
        since W7: the guest is innocent in this wall, the instrument is at fault, and the instrument
        was at fault for a different reason each time.** What finally found it was not a cleverer
        reading — it was printing the whole register frame at one instruction, where `$v0` was the
        only survivor, and asking why that one.

## 2026-09-30 — W10 FELL. The guest gets past its syscall table, and touches hardware again

**Suite 463/463. Harness rebuilds clean, no probes in shipped files. Root commit `50a38cd`, patch
md5 `647ceab1ea16a26e94858e5dc0d4270c`.**

### The bug, red first

New test in `ps2_thread_block_tests.cpp` — **G1.8g "a deferred syscall must not rewind the CALLER's
registers"**. A guest function seeds `$s2`, `$s3` and `$ra`, calls a callee whose syscall
**defers**, and resumes at the PC it published. It drives `EeScheduler::currentContext()` every
iteration, exactly as the harness does, because a test that drives a private copy would assert the
broken behaviour and pass. That is stated in a comment in the fixture, so nobody "simplifies" it
back later.

All four assertions were red, with precisely the measured signature: `$v0` correct, everything else
rewound.

### The cause — ours, and silent for eleven dishes

`EeScheduler::serviceInvocations()` opened with

```
main->context = m_runtime.m_cpuContext;
```

G1.8c's fix for W6, guarded on `m_guestExecuting`, which is set **only inside `EeScheduler::run()`'s
dispatch**. The harness never calls `run()`, so it fired on **every** service call. And it copied the
stale frame over the live one: since the W9a fix the driver advances `currentContext()`, which for
the main thread **is** `main->context`, while `m_cpuContext` is only republished by
`copyMainContextToRuntime()`.

So every deferred syscall rewound the caller to before its own call. `$v0` always looked right,
because the runtime writes it back by hand. That is the whole reason this survived.

### The fix — explicit, not deleted

There are two frames and two legitimate drivers, so the driver now says which one it advances:

```
EeScheduler::setDriverAdvancesSchedulerContext(bool)
  false (default)  driver advances runtime.cpu(); m_cpuContext is live, so the scheduler's copy is
                   refreshed FROM it. Unchanged G1.8c behaviour; what the direct-syscall tests want.
  true             driver advances currentContext() each iteration, as the harness does since W9a.
                   main->context is live, so the refresh is skipped entirely.
```

The harness sets `true`. The new fixture sets `true`, with the comment above.

### Measured, 90 s budget, before → after

| | before | after |
|---|---|---|
| `0x010286f0` entered | 0 times | **reached** |
| `distinct_pcs` | 6 | **143** |
| `service_frames` | 7,481,984 | **38** |
| `total_syscall_calls` | 29,914 | 137 |
| `distinct_mmio_addresses` | 0 | **308** |
| `total_mmio_accesses` | 0 | **770** |
| `sce_SetSyscall` calls | 2 | 12 |
| `sce_SleepThread` calls | 0 | 14 |
| wall clock | 90 s (budget) | **1.0 s** |

### W11 — where it stops now

A 5-address cycle: `0x01027E98`, `0x01027C70`, `0x01027EAC`, `0x0100076C`, `0x01000760` — the
`bltz $v0, 0x01000760` retry loop W9 first saw, now reached with 143 distinct PCs and 308 hardware
addresses instead of 6 and 0.

**Measured at both yield points, identically every pass:**

```
RETRY pc=0x0100076c  v0=0xFFFFFFFF  a0=0x01FFFE70  a1=0  a2=0  a3=0x01FFFE50  t0=0
       t1=0x00008000  s3=0x01030000  sp=0x01FFFE50
```

- `v0 = 0xFFFFFFFF` → `bltz` **always** taken → retry forever.
- `a0` and `a3` are both **guest stack addresses**. `a1`/`a2`/`t0` are 0.
- **`t1 = 0x8000` is the one operand that is neither a stack nor a data address.**
- The callee makes **no fast-path load at all** (`READ32` observer over `0x01027C70..0x01027DA0`
  returned **zero** hits).
- **No MMIO address is polled** — all 308 have exactly **1 access each**, so nothing is waited on in
  memory.

### Next step, in order

1. **`t1 = 0x8000`.** Find what `sub_010027C70` does with it. It is a count, a size or a register
   offset, and it is the only operand that is not obviously a pointer to guest memory.
2. **`sub_010027498`** — the function it calls, which returns `-1` with **no memory access
   whatsoever**. That signature is a call failing on a **register or state** test, not on data. Read
   its recompiled body; the decode is in the generated unit's comments, which are authoritative.
3. Only then look at what `-1` means to the caller, i.e. what `0x01027D1D` does on the error path.

### Retractions, still standing — do not re-chase

- GS remap to `0x70000000`: **refuted.** Correct map is GS at `0x12000000`, scratchpad at
  `0x70000000` / 16 KB.
- "`libgallium` present stall": six perf samples.
- "120.6 s → 1.6 s total": false. The watchdog fix only took `harness_tail_ms` to 0–18 ms.
- **`VULCAN4 FRAME source=guest` is emitted by nothing.** Guest still makes zero frame-emitting MMIO
  accesses. Do not report this milestone until the log contains that line.

## 2026-09-30 — W11 is the disc's IRX drivers, and the runtime now says so out loud

### How it was found

`0x01027C70` — one of the two addresses W11 parks on — is not guest code at all. The generated unit
has it as a leaf that immediately calls a syscall:

```
sub_01027C70_0x1027c70:  ctx->pc = getRegU32(ctx, 31);
                         ps2_syscalls::sceSifLoadModule(rdram, ctx, runtime);
```

`a0` is a guest stack string, `a1`/`a2` are 0, and `$v0` comes back `0xFFFFFFFF` every single pass.
So the guest is retrying an **IOP driver load**, and the caller is `bltz $v0, 0x01000760` — the
`0x01027E98` is a `jal 0x01000764`, not a call to `0x01027E98`.

Instrumenting `SifLoadModule` gave the exact requests:

```
cdrom0:\IRX\SIO2MAN.IRX;1    -> handled=1, moduleId > 0   (rescued by the HLE module manager)
cdrom0:\IRX\MTAPMAN.IRX;1    -> handled=1, moduleId = -1   (REJECTED)
```

and the runtime already had the cause in its own log, once per attempt:

```
[ps2xIOP:warning] [IOP] failed to open IRX 'cdrom0:\IRX\SIO2MAN.IRX;1'
[ps2xIOP:warning] [IOP] failed to open IRX 'cdrom0:\IRX\MTAPMAN.IRX;1'
```

### The chain, end to end

- `SifLoadModule` (`RPC.cpp`) reads the path, then calls `runtime->loadIopModule`.
- `IopSubsystem::loadModule` tries the **physical** load first for a non-`rom0:` device.
- `IopEmulator::loadModule` calls `IopModuleLoader::readWholeHostFile`.
- That calls `host.translateGuestPath` → `vfs().resolveHostPath(path, {hostRoot, cdRoot, mcRoot})`,
  then `openHostFile` → `fopen`.
- **`cdRoot` defaults to `elfDirectory`**, i.e. the directory holding the game executable. The
  runtime expects the disc **extracted as a directory tree**; `cdImage` is a separate raw-sector
  path used only by `sceCdRead`.

**And the directory has no `IRX/` in it:**

```
/mnt/ssd/gt4/work/SCUS_973.28   ELF 32-bit LSB executable, MIPS     <- the game executable
/mnt/ssd/gt4/work/CORE.GT4                                          <- data
/mnt/ssd/gt4/work/SYSTEM.CNF
```

`find` for `*.irx` under `/mnt/ssd/gt4`, `/home/or/vulcan4` and `/mnt/ssd/vulcan4-build` returns
**nothing**, and there is no ISO anywhere on the box. The extraction was incomplete: the game
executable and data arrived, the `IRX/` driver directory did not.

### What was changed

`SifLoadModule` now calls `reportUnserviceableIopModule`, which prints a `VULCAN 4 LIMITATION:` block
**once per distinct path**:

```
VULCAN 4 LIMITATION: IOP driver 'cdrom0:\IRX\MTAPMAN.IRX;1' cannot be served -- the file is not
present under the cdrom0: root '/mnt/ssd/gt4/work' (handled=1, moduleId=-1).
VULCAN 4 LIMITATION: the guest will now retry this load forever and make no further progress; this
is the boot wall, not a hang in the emulator.
VULCAN 4 LIMITATION: to supply it, put the disc's IRX directory next to the game executable (this
path is the cdrom0: root) or point cdRoot at a full extraction of your own disc. The drivers are game
data, so they are never shipped here.
```

SIO2MAN is deliberately **not** reported: the HLE module manager serves it, so it is not a
limitation.

This is law #2 being honoured. Before this, `halt=guest_cycle_no_progress` named a symptom and the
`[IOP] failed to open IRX` warning was on stderr, buried under hundreds of identical lines from a
guest retrying every few milliseconds. Now the boot says why it stopped, in the product's own voice.

Suite 463/463. Patch md5 `b124c06d573ffa113ffdfeae68088327`.

### What is needed, and it is not code

`IRX/SIO2MAN.IRX`, `IRX/MTAPMAN.IRX` and the rest of GT4's driver directory, from the captain's own
disc, placed so that `cdrom0:` resolves them. Law #1 forbids shipping them and law #7 says the user
brings their own disc, so this cannot be done from here. `/mnt/ssd` has 112 GB free, so a full disc
dump fits if one exists.

**Expect more of these in sequence.** GT4's boot loads its drivers in order, so the next wall will
be whichever driver comes after MTAPMAN in that list. The new limitation line will name it.

## 2026-09-30 — CORRECTION: the W10 red test did not gate the fix. It does now

`@verifier` was right and I was wrong. Reverting the one-line guard in `EeScheduler.cpp` left the
suite at **463/463**, and the commit that introduced the test claimed it was red first. It was not,
in any sense that would have caught the regression.

### Why it was blind

The fixture's driver was:

```cpp
R5900Context *frame = env.runtime.eeScheduler().currentContext();
if (frame == nullptr) { frame = &env.runtime.cpu(); }   // <-- this
```

**`EeScheduler::kMainThreadId` is `1`, and `reset()` leaves `m_currentThreadId` at `0` — which is a
SENTINEL, not the main thread.** So `currentContext()` returns `nullptr` on the very first dispatch,
the fallback fired, and the whole scenario ran on `runtime.cpu()` — i.e. in the `false`
configuration, the one where the entry refresh is a **resync** and not a rewind.

Measured, with the guard reverted:

```
[DIAG-REFRESH] m_cpuContext.pc=0x301300 main->context.pc=0x301308
               m_cpu.s2=0x0            main.s2=0xaaaa0002
```

`m_cpuContext` held the **live** values, because that is where the guest had been running. Copying it
over `GuestThread::context` was the right thing to do. The test was exercising the fix's *opposite*.

The assertions had a second, independent blindness: they read registers recorded by `chainResume`,
which runs inside the **invocation's** private frame copy. The rewind never touches that copy.

### The fix to the fix

1. `ChainEnv::arm()` now calls `runtime.eeScheduler().bindMainContextForSyscall(runtime.cpu(),
   rdram.data())` after `reset()`, so the main thread really is current and `currentContext()`
   resolves from the first dispatch. That is the console's own way of establishing it, so the test
   uses the product rather than a back door.
2. The driver's silent fallback is **gone** — it is a hard `return`. A driver that quietly switches
   frames is W9a all over again, and it is exactly what hid this for a whole commit.
3. A new assertion gates the configuration itself:
   `FIXTURE: currentContext() must resolve after arm(), or this test is not exercising the
   configuration that breaks. EeScheduler::kMainThreadId is 1, not 0`.
   If that ever fails again the test reports it instead of quietly passing.

### Proof it now gates

| build | result |
|---|---|
| guard reverted (`!m_guestExecuting` only) | **463 tests, 462 passed, 1 FAILED** |
| guard restored (`&& !m_driverAdvancesSchedulerContext`) | **463 / 463** |

Red signature with the guard reverted, showing the rewind directly:

```
m_cpuContext.pc=0x301300 (stale)   main->context.pc=0x301308 (live)
m_cpu.s2=0x0            (stale)   main.s2=0xaaaa0002     (live)
```

Product unchanged: `distinct_pcs=143`, `service_frames=38`,
`distinct_mmio_addresses=308`, `elapsed_ms=1063`, no probes in shipped files. Suite 463/463.
Patch md5 `b215d8e1a6074234d13127c0735be2ef`.

### Known follow-up, deliberately not done here

`driveLikeTheHarness(BlockEnv&, ...)` — used by the W8 "worker-then-resumed-main" test — actually
drives `runtime.cpu()`, **not** `currentContext()`, so it is NOT like the harness, and `BlockEnv`
correctly leaves `driverAdvancesSchedulerContext` at `false`. That combination is self-consistent and
the test is valid, but the name is a lie and it means the W8 coverage is not measuring the harness's
real frame selection. Worth renaming and re-pointing at `currentContext()` in a dish of its own;
doing it here would have put W8 coverage at risk for no gain.

### The lesson, written down

A red-then-green test is only evidence if the red was produced **by reverting the fix**. Asserting
"it was red when I wrote it" is not a check anyone can repeat, and this time it was simply wrong: I
watched four assertions go red against a real defect and still shipped a test that could not fail.
From here: revert the fix, rebuild, watch it go red, then restore it. That is the whole ritual.

## 2026-09-30 — W12 FELL: a yield is not a transfer, and VBlank was only reachable through a transfer

**Suite 465/465. Product: vsync_tick 1 → 54.**

### The symptom

After W11 the guest stopped spinning in one place and started doing real work: 223 distinct guest PCs,
20,000,000 guest entries, 1.86 M syscalls. But it ran to the **entry budget** in 54 seconds rather
than finishing, and two numbers made no sense together:

```
ee_cycle=98464696  next_event_cycle=9830598  vsync_tick=1  dispatcher_transfers=16
```

`next_event_cycle` is exactly **two** VBlank periods (`16667us * 294.912MHz = 4,915,299`), so VBlank
had fired once and rescheduled. `ee_cycle` was **ten periods past** that deadline — overdue, sitting
in the queue, unfired. And **16 dispatcher transfers against 20,000,000 guest entries.**

### The bug, and it was in the driver, not the runtime

`serviceInvocations()` was reachable from exactly one place in the harness: the `catch` for
`EeDispatcherTransfer`, which is thrown when the guest queues an *invocation* — a syscall override,
an interrupt callback.

`dispatchGuestBranch()` has a **second** exit. When `checkpointDue()` reports that EE time or pending
work needs attention it returns **false**, the generated function does `return`, and **no exception
is thrown.** The guest has yielded — mid-frame, registers live, stopped on purpose — and the loop
carried straight on into it again.

VBlank is delivered by `processPendingEvents()`, which only `serviceInvocations()` calls. On a
yield-only run the event queue was therefore never drained and time never became events.

Measured inside the loop at call 3000, with the guest running:

```
[LOOP] eeCycle=7165376 nextDeadline=9830598 pacingSet=0 deadlines=1 mayWait=0
       entries=[{type=1 cyc=9830598 hostIn=-307ms}]
```

The VBlankStart was right there with its host deadline **307 ms in the past**, and `pacingSet=0`
only because `eeCycle` had not reached its cycle deadline yet. Correct, so far. The problem was that
`processDueDeadlines` was reached only on those 16 transfer-driven calls, so EE time climbed to 98 M
cycles between them with nobody converting it into events.

### Two wrong turns, recorded so nobody repeats them

1. **I blamed the runtime first.** "EE time crossing a deadline never tells the driver to come back"
   sounds right and is wrong: `checkpointDue()` already reports it, and a unit test driving
   `accountCycles` + `serviceInvocations` passed.
2. **The first version of the red test passed while the product was broken**, because it called
   `serviceInvocations()` itself. A driver that services its own yield proves nothing about a driver
   that does not. Same lesson as the W10 test, third time now: a test must exercise the path the
   product uses, or it is decoration.

The instrumentation lied twice as well: a diagnostic placed *after* the `for(;;)` loop never ran,
because that loop `return`s early, and a counter capped at 4 was already exhausted by earlier tests.
Both looked like evidence while reporting nothing.

### The fix

`tools/harness/vulcan4_harness.cpp` asks the scheduler whether it is owed a turn after **every**
guest return, not only after a transfer:

```cpp
if (runtime.eeScheduler().checkpointDue(0u))
{
    const EeServiceResult serviced = runtime.eeScheduler().serviceInvocations();
    ...
}
```

`checkpointDue(0)` is the query the generated code already makes at every guest branch, and a zero
cycle count makes it a pure question — `accountCycles()` charges nothing and moves no clock, so the
check cannot skew the timeline it asks about. A new `checkpoint_serviced` counter makes the volume
visible in the boot report instead of invisible.

### Measured, 90 s budget, before → after

| | before | after |
|---|---|---|
| **`vsync_tick`** | **1** | **54** |
| `service_frames` | 4,111 | 219,667 |
| `ee_cycle` | 98,464,696 | 268,430,243 |
| `total_syscall_calls` | 1,862,701 | 5,258,779 |
| `total_mmio_accesses` | 463 | 1,116 |
| `halt` | entry_budget_exhausted | wallclock_deadline |

`next_event_cycle=270341445` now sits just ahead of `ee_cycle`, which is what a healthy queue looks
like. Suite 465/465, no probes left in shipped files.

### W13, already measured: the guest loops but never renders

With VBlank firing, almost all of the guest's syscall traffic is two calls at two adjacent PCs:

```
0x29 sce_ChangeThreadPriority calls=2,253,727  last_pc=0x0101f2b8
0x2f sce_GetThreadId          calls=3,004,970  last_pc=0x0101f318
```

5,258,697 of 5,258,779 syscalls — **99.9998 %**. `0x2f` is `GetThreadId` per
`ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp:114`. **`sce_SleepThread` was called once.**

The thread is running at **`prio1`** (it was prio3 at W11): GT4 has lowered its own priority and is
then spinning, which is a cooperative scheduler asking to be preempted by something higher-priority.
`tid2` is `status=2`, Waiting. **There is no GS activity in the log at all**, and the guest still
touches only 241 MMIO addresses.

So the next question is narrow, and it should be answered from the scheduler and the harness before
anything is guessed: **what is supposed to preempt that thread, and does it exist?** On a console the
main loop drops to priority 1 and a VBlank handler or render thread runs. Here either nothing sits at
a higher priority, or `applyPendingPreemption()` is not switching. `GetThreadId` being called three
million times also smells like the loop testing "am I the thread that owns the frame", so its return
value is worth reading.
