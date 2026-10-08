# 🎬 THE GOAL: REACH THE MENU.

**This is the only thing we want.** GT4's own menu screen, drawn by our own runtime, on the screen.
Everything below is just the ladder to it — rungs exist so nobody drifts, not as ends in themselves.
You do not stop, you do not report a rung as "solved", you do not wait for a new order:
**you keep climbing until the picture is not the disclaimer.**

Captured on camera, mechanical proof: `bash .auto/verify-menu.sh` exits 0 on a fresh capture — and the
captain looks at the picture with his own eyes.

---

# THE ROADMAP TO THE MENU — VULCAN 4, the standing campaign

**The standing goal: reach GT4's own menu.** Not "fix this bug". The crew works this list top-down,
forever, until the picture is not the disclaimer. A rung is done only when its **gate** passes. When a
rung passes, the crew marks it and **starts the next rung in the same run** — no stopping to report
"done", no waiting for a new order. When a rung is blocked, the crew writes the named limitation and
**picks another unblocked rung**; the only reasons to stop are the milestone or a hard stop.

Captain's rule quoted into every rung: *"If we give them a task like 'reach this part', they will work
forever — which is what we need. If we just say 'solve this', they report back solved and we start over."*

---

## WHERE WE ARE (measured, 2026-10-08)

- Engine **fully linked**: 19,404 functions, 273,876 engine symbols in the harness (347 MB binary).
- Engine image **placed byte-perfect** in RDRAM `0x00100000..0x00617A14` (identical to the hardware dump).
- **`ExecPS2` FIRES — R1 done.** `count=1`, entry `0x00100008`, `functions_entered=10417` (up from 751),
  `bios_files=0`. The wakeup chain is closed: zero-QWC GIF chain completes (STR clear + DMAC cause) →
  handler clears the busy byte → `SleepThread` is untimed (W161 fixed) → tid2 wakes → `SignalSema(7)` →
  `ExecPS2`. Two DMAC/sleep fixes carried in `ps2recomp-linux-w275-*.patch`.
- Picture: the 2005 disclaimer. `bash .auto/verify-menu.sh` → exit 1.

---

## THE RUNGS

| # | Rung | Gate (mechanical) | Status |
|---|------|-------------------|--------|
| **R1** | **The hand-off fires** — the unserviced DMAC interrupt invocation is serviced, the busy byte clears, sema 7 is signalled, `ExecPS2` runs | `grep -ac EXECPS2 boot_*.log` > 0 **and** the entry PC is recorded | ✅ `ExecPS2` count 1, entry `0x00100008` (boot_w275sleept.log) |
| **R2** | **The engine actually executes** — entering the image does not immediately die; the first engine functions run and are named | boot log lists >0 executed PCs inside `0x00100000..0x00617A14`, and the halt is a named reason, not a crash | ⬜ |
| **R3** | **The loader completes its own sequence** — file I/O, thread setup, GS/DMA init finish; the game's scheduler starts | no new LOUD limitation; threads run instead of parking; loader functions wind down | ⬜ |
| **R4** | **GT4's own init reaches its main loop** — the game's state machine runs and advances | the game's state words advance across frames; frames presented keep climbing past the disclaimer's count | ⬜ |
| **R5** | **Our GS presents GT4's own frames** — the picture changes from the disclaimer into something the game drew | a fresh capture whose structural signature differs from `.disclaimer-reference.png` beyond noise — **the captain's eyes confirm it is GT4 drawing** | ⬜ |
| **R6** | **The intro sequence runs** — logo → title screens advance on their own | two consecutive distinct captures, each matched against the same moment in the PCSX2 oracle | ⬜ |
| **R7** | **🎬 THE MENU** — GT4's own menu screen | `bash .auto/verify-menu.sh` **exit 0** on a fresh capture + the captain looks at it | ⬜ |

## ENABLERS (pull one in whenever a rung stalls — they are never "someone else's problem")

- **E1 · Interrupt & sync delivery** — DMAC/INTC invocations serviced, VSync reaching the guest (this is
  R1's theme; it will bite again for R5/R6).
- **E2 · File I/O over the disc's own formats** — ISO reads, `CORE.GT4`, `GT4.VOL` (the menu reads files).
- **E3 · VU1/VIF path** — needed for the intro's 3D before the menu is reached.
- **E4 · Input** — needed to *navigate* the menu; not needed to *see* it. Do not prioritise it early.
- **E5 · Nothing lives only in the working tree** — every rung ends with the record captured (law 8).

## HOW THE CREW RUNS THIS (the "forever" mechanics)

1. Take the **topmost rung whose gate is not passing** and whose work is not blocked.
2. Split it into parallel slices (all seats busy, duplicates competing, nobody parks).
3. Prove or kill each hypothesis with **two independent angles** before writing a fix — the oracle
   (PCSX2) counts as one; our own logs alone count as none.
4. When the gate passes: **mark it in this file**, put the raw gate output in `docs/W<NNN>-RESULTS.md`,
   capture the record (patch + commit), then **immediately start the next rung**. Do not stop to report
   "solved".
5. When a rung is blocked: write `STUCK: / TRIED: / BLOCKED BY: / NEED:` on the board with a number,
   then **move to any other unblocked rung or enabler** and keep working.
6. Stop only for: **the milestone (R7)** — or a hard stop the crew cannot route around (then ping the
   captain with the shape above).

**A rung may not be claimed on a commit, a diff or prose — only on its gate.**
