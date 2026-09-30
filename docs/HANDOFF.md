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
