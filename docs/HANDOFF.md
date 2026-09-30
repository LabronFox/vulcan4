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
