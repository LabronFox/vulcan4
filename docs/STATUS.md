# VULCAN 4 — STATUS

**As of 2026-10-08 (dish W275 / rungs R1–R3).** Rewritten, not amended. Every number below names the
log, commit or command that produced it. If a line has no evidence, it is not here.

**TL;DR for a stranger:** GT4's own machine code is translated ahead of time (19,403/19,403 entry
points) and **executes with no BIOS anywhere**, and the loader places the disc's `CORE.GT4` engine
image into RDRAM `0x00100000..0x00617A14` **byte-perfect** — verified whole-image against the disc's
own zlib decoder, 0 mismatches in 5,339,668 bytes. The hand-off **now fires**: `ExecPS2` runs (rung
R1), a dispatch miss inside the engine resolves through the engine's own table (rung R2), and the
engine's SIF boot-ready poll — syscall `0x7A` `SifGetReg`, previously unwired and returning 0 forever —
**exits** (rung R3). The engine now executes: **31 distinct engine-image code PCs**, `ee_cycle`
**498 M** in a 15 s boot (`2,082 M` in 45 s). **G1 — first boot — is no longer "does anything run": it
is the next unwired thing.** No part of the game renders; no part of it plays.

---

## Snapshot — what is true today

| Fact | Evidence (runnable / on disk) |
|---|---|
| **The recompiler emits every entry point** | W251: `check_engine_symbols.py` (independent of the recompiler's own report) → `csv=19403 emitted=19404 matched=19403`. The +1 is the ELF entry `0x00100008`, named LOUD. Commit `91e20a0`. |
| **GT4's code executes, no BIOS** | `VULCAN4 BOOT REPORT functions_entered=10543 true_guest_entries=13252077 halt=stuck_in_syscall bios_files=0 frames_presented=848` — `/mnt/ssd/vulcan4-build/run/boot_w275r3.log` (15 s, probes off). 45 s: `functions_entered=10786 true_guest_entries=52847539 ee_cycle=2081907532` — `boot_w275r3long.log`. The harness has **no BIOS loading path at all**. |
| **The engine image IS placed, byte-perfect** | W273 (`5774b00`): `VULCAN4_RDRAM_DUMP=00100000:517A14` vs `zlib.decompressobj(-15)` over `CORE.GT4[6:]` → **0 mismatches in 5,339,668 bytes** (the unpatched baseline was **all zeros**). Site: `W30BIGCOPY seq=27 op=memcpy src=0x12bf234 dst=0x100000 size=5339668 pc=0x10048c8`. |
| **The engine RUNS** | R1 `ExecPS2` fires (`ee_cycle=628916`, entry `0x00100008`); R2 lets `lookupFunction` fall through to `g_ps2EngineFunctionTable` → `No exact recompiled function` = **0**; R3 wires syscall `0x7A` → the SIF boot-ready poll **exits** (0x7A calls 195100 → **4**, `Unimplemented` 195117 → **17**). **31** distinct engine-image code PCs (`0x00100000..0x00617A14`), up from 27. |
| **Two unwired syscalls remain, neither the wall** | `0x0B` — **16 calls**, `PC=0x101f0b8 RA=0x10183e0`, genuinely absent from the `Dispatcher.cpp` switch (v0 left 0). `0x07` — 1 call, is `ExecPS2`: implemented, special-cased in `System.cpp`, only *logs* as unimplemented. Both identical in the R2 baseline. |
| **The test suite is green** | 497/497, `bash .auto/verify-dish.sh` → exit 0. |
| **The mechanical dish gate passes** | same run: `=== RESULT: MECHANICAL CLAIMS HOLD ===`. Picture gate (below) does not. |
| **The picture is still the 2005 disclaimer** | R3 `verify-menu.sh` on a **fresh** capture (`w275r3long`, `frames_presented=2525`): 1 colour / non-black 0.1111 vs the disclaimer's 14 / 0.1150 → structural match, byte-identical to the R2 capture (md5 `ad5a5c50…`). R3 is a CPU/behaviour win, NOT a graphics one. |
| **The GS rasterises primitives and samples textures** | G2.4; `bash tools/gs/build_gs_probe.sh` → 37,275 distinct colours, PSMCT32 + PSMT8/CLUT textures through real GIF REGLIST packets. *(Not re-run for this rewrite.)* |
| **The project cross-compiles to ARM64** | commit `5509151`; `docs/ANDROID-FEASIBILITY.md`. *(Not re-run for this rewrite.)* |

---

## The wall — G1: the engine RUNS; the wall is now the next unwired call, not the hand-off

*(This section's W273 framing — "the hand-off never fires / loader parked on `sema#7`" — is **superseded**:
R1–R3 below. The placement facts still hold; the conclusion does not.)*

The loader reads `cdrom0:\CORE.GT4;1` (`fd=5`, 2,020,861 B), walks the container (6-byte header +
**raw DEFLATE**, `wbits=-15`) and copies the engine image `0x00100000..0x00617A14` (size `0x517A14`).
**The image is placed byte-perfect** — and the hand-off fires and the engine executes.

| Measured (W273, `5774b00`) | Value |
|---|---|
| engine image vs the disc's zlib decode | **0 mismatches in 5,339,668 bytes** — `VULCAN4_RDRAM_DUMP=00100000:517A14` vs `zlib.decompressobj(-15)` over `CORE.GT4[6:]` |
| the unpatched baseline | **all zeros — 0 of 5,339,668 bytes placed** (engine `0x00100008` reads `00`; the disc says `28`) |
| placement site | `W30BIGCOPY seq=27 tid=2 op=memcpy src=0x12bf234 dst=0x100000 size=5339668 pc=0x10048c8` |
| cause of the baseline zeroes | the **driver's frame selection** (a frozen shadow) — not the decode, not the copy; the harness `liveFrame` fix **alone** yields 0 mismatches |
| the hand-off that never runs | `ExecPS2` (re-exec into the engine at entry `0x100008`; handler `vulcan4_harness.cpp:2706`) — **0 fires** |
| why nothing else can enter the engine | the loader's generated code has **zero** `dispatchGuestBranch` targets in `0x00100000..0x00617A14`; `0x00100008` does not occur in it (static fact) |
| where the loader parks instead | main thread on `sema#7`, `pc=0x0101f468`, `woken=0`; `sce_SignalSema` `a0` = 5, 6, 6, 2, 4, 5 — **never 7**; tid2 on `sleep#0` at `0x0101f348` |
| best lead | `VULCAN 4 LIMITATION: syscall 0x5b override handler 0x80075000 has no generated function` — **6×** |

**The next dish is the wakeup, not the image:** find who should `SignalSema(7)` — a dropped signal, or
an `intc`/interrupt delivery (`intr_queued=93 intr_run=97 irq_attach=0 irq_done=0 pending_hi=1`).
The W252 blob-injection A/B survives on the record in [`docs/W252-RESULTS.md`](W252-RESULTS.md) as what
was seen with the instruments of the day; its conclusion is retracted by the W273 banner there.

**Success (unchanged):** `ExecPS2` reaches `0x00100008` → ≥1 `[Dispatch]` target inside
`0x00100000..0x00617A14` with `bios_files=0` → `verify-menu.sh` exit 0 → `verify-dish.sh` exit 0.

---

## What does not work yet

- **No engine, so nothing downstream.** No menus, no 3D, no race, no audio, no saves, no input — the
  engine that would do all of it has never run.
- **The VU1 is untouched** (G3.1). No guest path has reached `ps2_vif1_interpreter.cpp`.
- **No CD/DVD path driven by a guest beyond the loader read**; no ADPCM audio decode; no APK/NDK build.
- The itemised durable list is [`docs/LIMITATIONS.md`](LIMITATIONS.md) — it is part of the definition
  of done and is never empty.

---

## The ladder

| Goal | State | What it is |
|---|---|---|
| G0 | ✅ | toolchain, disc map, function anatomy, complete recompiler output, clean-room reproduce |
| **G1 — first boot** | 🟡 **running, not yet a screen** | engine emitted 19,403/19,403, image placed byte-perfect, `ExecPS2` fires (R1), 0 dispatch misses in the engine (R2), SIF boot poll exits (R3). W277 r30: **23 IOP drivers load, every PDI service binds, `halt=wallclock_deadline`** (`functions_entered=21347`, 20 s) — the boot RUNS to the deadline instead of parking in a syscall. The next wall is the **first DVD read rpc** (the streamer has only init'd), then the Adhoc script loader |
| G2 — a picture | 🟡 | GS layer draws and rasterises (our own probe, not the game); the game's screen is still the disclaimer |
| G3 — 3D (VU1) | ⬜ | plan in `docs/VU1-PLAN.md`; nothing driven by a guest |
| G4 — playable | ⬜ | menus, a race you can drive, audio from the disc, saves |
| G5 — Spec II / Android | 🟡 | ARM64 feasibility proven (`5509151`); the port itself not started |
| G6 — mods | ⬜ | registered; add-a-car and custom music, all **OFF by default** |
| G7 — distribution | ✅ in force | code only; the user brings their own disc |

Full ledger with the reasoning and rejected alternatives: [`docs/GOALS.md`](GOALS.md).

---

## What a user needs

**Their own legally-obtained disc. That is the whole list.**

- **No BIOS** — not optional, not recommended, *not required*. The runtime serves the console's OS
  calls itself and every boot report reads `bios_files=0`. That property is what separates a
  recompilation from an emulator, and it is the captain's bucket-list item.
- No emulator, no console, no modchip. Native x86-64 binary.
- No game data in this repository, ever — the guest image is referenced by path.
- Build: CMake ≥ 3.21, GCC 13, `pkg-config`, FFmpeg dev headers, X11 + OpenGL dev headers.
  Recipe: [`docs/TOOLCHAIN.md`](TOOLCHAIN.md) §10.

**To actually play it today: you cannot.** What exists is a working recompilation toolchain and the
proof that it is sound — not a game.

---

## Honest notes on this page

- The GS frames are **our own code**, not game screenshots. They prove the pipeline, not the game.
- Two probe instruments in W252 were **blind** (`VULCAN4_W229_OW`, `VULCAN4_W229_CP` saw 0 events in a
  region the dump proves is written) and the `halt=` string is **not a comparator** — a harness
  `throw` unwinds past the syscall-id restore, so `0x44` latches and the halt reads
  `stuck_in_syscall` nondeterministically. Compare runs by hardware events or a picture, never by halt
  or `functions_entered`.
- `verify-dish.sh` reads the newest `$B/run/*.log`, which is a **seat's** log when a seat is running;
  its `halt=` line is "newest boot on the box", not the gate's own boot.
- The `[Dispatch]` printout is capped at **n=400**; absence past that is the cap, not evidence.
- W252's headline ("the DEFLATE decode is the sole loader blocker") is **retracted**: W273's
  whole-image A/B showed the baseline engine region is **all zeros** and that a *driver*
  frame-selection fix alone makes it byte-perfect. The buffer W252's probes diffed was not the
  finished image. [`docs/W273-RESULTS.md`](W273-RESULTS.md) supersedes it.
- Earlier wrong claims stay on the record rather than being deleted: the `-0x01000000` memory-map bias
  that manufactured three goals' worth of decoy (removed in G1.8); the `FindAddress` scan-cost bug; and
  a gate that read the three *oldest* logs. A measurement that was itself broken is worth more written
  down than tidied away.
