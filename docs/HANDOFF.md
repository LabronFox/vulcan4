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
