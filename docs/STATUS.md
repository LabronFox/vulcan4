# VULCAN 4 — STATUS

**As of 2026-09-30.** This page is a snapshot. It is rewritten, not amended, so a later sweep can
show progress instead of rewriting history. Every claim below names the commit, artefact or command
that backs it. If a line has no evidence, it is not here.

**TL;DR for a stranger:** the whole toolchain builds from a fresh clone, GT4's own machine code is
translated ahead of time and **executes** with no BIOS, and the Graphics Synthesizer skeleton moves
real computed pixels. **No part of the game renders, and no part of it plays.** The wall is
unchanged since the first hour: the guest cannot get past 3 functions.

---

## What works today

Each line is a claim with its evidence. Commands are runnable as written.

| Works | Evidence |
|---|---|
| **The toolchain builds on Linux** — recompiler, analyzer, runtime, IOP | commit `9185e69`; binaries `ps2_recomp` (2,196,928 B), `ps2_analyzer` (13,747,672 B), `ps2EntryRunner` (38,075,096 B) under `/mnt/ssd/vulcan4-build/` |
| **The whole chain reproduces from a fresh clone** into a new build root, byte-for-byte | commit `7c88d95`; `/mnt/ssd/vulcan4-cleanroom/cleanroom.log` (1,019 lines). All four generated artefacts md5-identical to the incremental tree |
| **GT4's executable is mapped and understood** | commit `72d1266`; `docs/DISC-MAP.md` — entry `0x01000008`, 707 functions, `.text` 187,408 B |
| **A GT4 function is readable two ways** and the translation is faithful | commit `ffd0519`; `docs/FUNCTION-ANATOMY.md` |
| **The recompiler runs to completion** on the real 707-function executable, all bodies written | commit `ef3e183`; `ps2_recompiled_functions.cpp` = 8,899,740 B, `RECOMP_EXIT=0` |
| **GT4's own code executes, with no BIOS anywhere** | commits `4958a83`, `d1dc241`; `/mnt/ssd/vulcan4-build/run/boot.log` → `VULCAN4 BOOT REPORT functions_entered=1 halt=waiting_on_unnamed_value bios_files=0`. The harness contains **no BIOS loading path at all** |
| **The wall is named, twice over, and both names are measured** | commit `a628419` names the spin: the guest polls `sce_FindAddress` (0x83) over KSEG0 `0x80000000`–`0x80080000` for the code pointers `0x010285F8` and `0x010285C0`, exiting only when the two are **164 bytes apart**. The only record in memory has them **8 bytes apart**, so the walk runs off the end of the window. Commit `4b444be`/`d1dc241` refuted the KSEG0-translation theory by measurement: the search **hits 16 times** from `0x80000000`, so the alias resolves correctly. What is missing is a table entry **nothing in the system writes** |
| **The GS moves real, computed pixels** | commits `bcb0882`, `ccd9981`; `/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png`, 512×512, 119,302 B, **43,804 distinct colours** (G2.0 produced 1 — a black square) |
| **The GS decision is made, with reasons and rejected alternatives** | commit `d63a6f2`; `docs/GS-PLAN.md` |
| **The project cross-compiles to ARM64** | commit `5509151`; `/mnt/ssd/vulcan4-build/android/gs_probe_arm64` (5,263,568 B, ELF64 AArch64) with **707 guest functions** and **0 x86/SSE symbols**; `docs/ANDROID-FEASIBILITY.md` |
| **Prior art has been read, not guessed** | commit `d467818`; `docs/PRIOR-ART.md`, 14 cited sources |
| **The test suite is green** | 444/444 (`ps2x_tests`); 3 new entry-vs-resume tests proven to have teeth by mutation |

### Reproduce the boot yourself

```bash
bash tools/gs/build_gs_probe.sh                                   # the GS frame
/mnt/ssd/vulcan4-build/gs/vulcan4_gs_probe                        # prints distinct_colours=43804
```

The full end-to-end recipe — fresh clone, patches, build, boot — is
[`docs/TOOLCHAIN.md` §10](TOOLCHAIN.md). It was executed verbatim, and the transcript is the proof.

---

## What does not work yet

**The two walls, and they are unchanged.**

1. **The GS rasteriser produces no pixels.** The skeleton proves the *transfer*, *memory*,
   *readback* and *present* routes — 43,804 distinct colours move through VRAM losslessly. But no
   **primitive** is drawn. A guest that draws triangles still cannot be rendered. This is now the
   single biggest gap in the GS, and it is undiagnosed.
2. **The VU1 is untouched.** Nothing about the vector unit has been driven by a guest. Its compiler
   is `ps2_vif1_interpreter.cpp` and no guest path has ever reached it. Goal **G3.1**, untouched.

**And the concrete reasons the guest stops:**

- `total_mmio_accesses=0` and `distinct_mmio_addresses=0`. The guest has **never touched a hardware
  register.** It is still in initialisation.
- No IRX module has been loaded, and the runtime has no BIOS path, so the function pointer the guest
  is searching for does not exist anywhere in memory. The `FindAddress` scan is a brute-force
  heuristic standing in for the hardware's loaded-module export-table lookup.
- Because that lookup can never succeed, the guest **retries until the deadline**. That is a
  livelock, not a crash — and it is the reason the report was renamed from the generic
  `stuck_in_syscall` to `livelocked_in_syscall`.

**Also not done:** no CD/DVD or disc I/O path has been driven by a guest · no ADPCM audio decode ·
no input from a real controller · no menus, no save data, no race logic, no 3D · no APK · no NDK or
bionic build · no texture sampling, CLUT, blending, alpha or Z-test.

The durable, itemised list is [`docs/LIMITATIONS.md`](LIMITATIONS.md). It is part of this project's
definition of done and is never empty.

---

## Where the project is heading

The ladder, in order, with the honest state of each rung. Full detail in
[`docs/GOALS.md`](GOALS.md).

| Goal | State | What it is |
|---|---|---|
| G0.1–G0.4 | ✅ | toolchain, disc map, function anatomy, complete recompiler output |
| G0.5 | ✅ | clean-room reproducibility, no hidden state |
| G1.1–G1.2 | ✅ | guest code executes, wall named |
| G1.3 / G1.3b | 🟡 | syscall 0x83 served properly; still did not get past |
| G1.4 | 🟡 | investigation completed, then **corrected by G1.5** — the premise was wrong |
| G1.5 | ✅ | proved the stub body ran; found the real wall (a `FindAddress` livelock) |
| **G2.0** | ✅ | GS approach decided and the path proved |
| **G2.1** | ✅ | the GS computes pixels — 43,804 distinct colours |
| **G3.1** | ⬜ | **the VU1.** GT4's vertex microcode handled, a 3D scene rendered |
| G3.2 | ⬜ | a real car on a real track, natively |
| G4.1–G4.4 | ⬜ | menus, saves, input, and a full race that completes |
| G5.1 | 🟡 | Spec II support — researched, parked by the captain's call |
| G5.2a | ✅ | Android/ARM64 feasibility — answered with a build |
| G5.2 | ⬜ | the Android port itself (not started; this dish did not start it) |
| G5.3 | ⬜ | regional and special builds |

**The next real work is G3.1**, and before it, the GS rasteriser. A frame on disk that our own code
computed is not a rendered game; the honest next step is making a *primitive* rasterise.

---

## What a user needs

**Your own legally-obtained copy of the disc. That is the whole list.**

- **No BIOS.** Not optional, not recommended — *not required*. The runtime serves the console's OS
  calls itself, the harness contains no BIOS loading path at all, and every boot report line reads
  `bios_files=0`. This is the property that separates a recompilation from an emulator, and it is
  the captain's bucket-list item.
- **No emulator, no console, no modchip, no other tools.** Native x86-64 binary.
- **No game data in this repository, ever.** The guest image is referenced by path.
- To build: CMake ≥ 3.21, GCC 13, `pkg-config`, FFmpeg dev headers, and X11 + OpenGL development
  headers (the runtime builds raylib from source). See [`docs/TOOLCHAIN.md` §3](TOOLCHAIN.md).

**To actually play it today: you cannot.** The honest position is that this is a working
recompilation toolchain attached to a guest that reaches three functions. What exists is the
foundation and the proof that the foundation is sound — not a game.

---

## Honest notes on this page

- The GS frame is **our own code**, not a game screenshot. It is a computed pattern pushed through
  the GS's transfer path, read back and encoded by our own PNG writer. It demonstrates the
  pipeline, not the game. The G2.0 gate was too loose and let a **black square** pass; G2.1's gate
  requires ≥ 64 distinct colours and this frame carries 43,804.
- The GS decision was checked against the industry in `docs/PRIOR-ART.md` rather than asserted.
- Where a claim could not be evidenced it was downgraded, not dropped: G1.0 is 🟡, not ✅, because
  the no-BIOS property is demonstrated but the guest has not run far enough to prove every kernel
  call is served.
- Two earlier claims were **wrong and are recorded as wrong** rather than quietly deleted: that the
  GS triangle "never submits" (it does — `XYZF2` and `XYZ2` both kick), and that PS2Recomp's author
  says it "does not work properly" (unverifiable; grep of the pinned tree finds nothing).
