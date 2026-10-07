# VULCAN 4 — STATUS

**As of 2026-10-07 (dish W252).** Rewritten, not amended. Every number below names the log, commit or
command that produced it. If a line has no evidence, it is not here.

**TL;DR for a stranger:** GT4's own machine code is translated ahead of time (19,403/19,403 entry
points), it **executes with no BIOS anywhere**, and the loader gets the disc's `CORE.GT4` payload all
the way to a digest check. It fails that check by **one wrong byte in our own DEFLATE decoder**, and
that is the whole remaining distance to the engine image. **G1 — first boot — is the wall.** No part
of the game renders; no part of it plays.

---

## Snapshot — what is true today

| Fact | Evidence (runnable / on disk) |
|---|---|
| **The recompiler emits every entry point** | W251: `check_engine_symbols.py` (independent of the recompiler's own report) → `csv=19403 emitted=19404 matched=19403`. The +1 is the ELF entry `0x00100008`, named LOUD. Commit `91e20a0`. |
| **GT4's code executes, no BIOS** | `VULCAN4 BOOT REPORT functions_entered=3740 true_guest_entries=1529872 halt=stuck_in_syscall bios_files=0 frames_presented=1142` — `/mnt/ssd/vulcan4-build/run/w252-dump.log`. The harness has **no BIOS loading path at all**. |
| **The engine image is entered ZERO times** | Every boot log to date: `[Dispatch] target_pc` inside `0x00100000..0x00617A14` = 0. The engine is not running; the loader is. |
| **The test suite is green** | 497/497, `bash .auto/verify-dish.sh` → exit 0. |
| **The mechanical dish gate passes** | same run: `=== RESULT: MECHANICAL CLAIMS HOLD ===`. Picture gate (below) does not. |
| **The picture is still the 2005 disclaimer** | `verify-menu.sh`: 3 colours, non-black fraction 0.1173 vs reference 14 / 0.1150 → structural match to the disclaimer. |
| **The GS rasterises primitives and samples textures** | G2.4; `bash tools/gs/build_gs_probe.sh` → 37,275 distinct colours, PSMCT32 + PSMT8/CLUT textures through real GIF REGLIST packets. *(Not re-run for this rewrite.)* |
| **The project cross-compiles to ARM64** | commit `5509151`; `docs/ANDROID-FEASIBILITY.md`. *(Not re-run for this rewrite.)* |

---

## The wall — G1, and it is now one byte wide

The loader reads `cdrom0:\CORE.GT4;1` (`fd=5`, 2,020,861 B), extracts the container
(6-byte header + **raw DEFLATE**, `wbits=-15` — the reference `zlib.decompressobj(-15)` on
`CORE.GT4[6:]` yields exactly the container's `u24@2`), and builds the engine image
`0x00100000..0x00617A14` (size `0x517A14`). **The fio read and the container walk are correct —
proven against hardware.** The decode is not.

| Measured | Value |
|---|---|
| first differing byte | payload offset `0xD521C` = engine **`0x001D51EC`** |
| hardware vs ours there | `0x7C` (`7c 00 45 8c`) vs `0x2D` |
| extent | **11,365 of 6,118,856** bytes, 517 runs, span `0xD521C..0xD80B1` |
| mechanism | hardware copies **12 B at distance `0x18`**; ours copies **21 B at `0x48`** — a DEFLATE match-copy error, writer `FUN_0100F390` (pc `0x100F800`) |
| consequence | SHA-512 digest at `0x1004748` = ours `0x893d7a82` vs the game's `0x4bb5e6bf` → `FUN_01004500` false → guest spins in the fail-trap `0x01000638` (388/409 entries) instead of `ExecPS2` |

**A/B control that closes the diagnosis:** inject the hardware blob (`VULCAN4_W229_ORACLE`) and the
fail-trap is taken **0** times; `run/w252-orc.log` reaches
`VULCAN4 EXECPS2 -> unified resolve entry=0x00100008 (engine 0x00100008,0x00617a14)` and halts
`guest_blocked pc=0x005ad8c8`. **The engine does start when the blob is right — the decode is the sole
loader blocker, and the fix is W253's work.**

Acceptance (architect, T2 FINAL): our dump vs `w229-oracle.bin` = 0 differing bytes → ≥1 `[Dispatch]`
target in the engine range, `bios_files=0` → `verify-menu.sh` exit 0 → `verify-dish.sh` exit 0.

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
| **G1 — first boot** | ⏳ **the wall** | engine emitted 19,403/19,403 but entered 0 times; blocker named to one byte (above); fix = W253 |
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
- Earlier wrong claims stay on the record rather than being deleted: the `-0x01000000` memory-map bias
  that manufactured three goals' worth of decoy (removed in G1.8); the `FindAddress` scan-cost bug; and
  a gate that read the three *oldest* logs. A measurement that was itself broken is worth more written
  down than tidied away.
