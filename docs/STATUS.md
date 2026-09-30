# VULCAN 4 — STATUS

**As of 2026-09-30.** This page is a snapshot. It is rewritten, not amended, so a later sweep can
show progress instead of rewriting history. Every claim below names the commit, artefact or command
that backs it. If a line has no evidence, it is not here.

**TL;DR for a stranger:** the whole toolchain builds from a fresh clone, GT4's own machine code is
translated ahead of time and **executes** with no BIOS, and the Graphics Synthesizer skeleton moves
real computed pixels. **No part of the game renders, and no part of it plays.** The boot wall moved
this week: **3 guest functions → 25**, and the halt is no longer a livelock but a named restart loop.

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
| **GT4's two kernel-syscall overrides really land in the kernel's syscall table** | `/mnt/ssd/vulcan4-build/run/boot_final.log` → `[SetSyscall] n=131 handler=0x10285f8 slot=0x1218c readback=0x10285f8` and `n=90 handler=0x10285c0 slot=0x120e8 readback=0x10285c0`; `VULCAN4 SYSTABLE n=0x83 slot=0x1218c handler=0x10285f8`, `n=0x5a slot=0x120e8 handler=0x10285c0` — the two slots **164 bytes apart**, the gap GT4's convergence loop waits for. Test `the 0x01000000 window is identity mapped` pins the mapping this depends on |
| **The wall is named, three times over, and the third name is a bug of ours that was removed** | commit `a628419` named the spin; commit `4b444be` refuted the KSEG0-translation theory. **G1.8 found that G1.6's own `-0x01000000` memory-map bias was manufacturing the decoy** the first two names were chasing — it dragged the guest's `.data` 16 MB down into the window the guest searches. Removed, with a test that fails 5 assertions if reintroduced |
| **The GS moves real, computed pixels** | commits `bcb0882`, `ccd9981`; `/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png`, 512×512, 119,302 B, **43,804 distinct colours** (G2.0 produced 1 — a black square) |
| **The GS decision is made, with reasons and rejected alternatives** | commit `d63a6f2`; `docs/GS-PLAN.md` |
| **The project cross-compiles to ARM64** | commit `5509151`; `/mnt/ssd/vulcan4-build/android/gs_probe_arm64` (5,263,568 B, ELF64 AArch64) with **707 guest functions** and **0 x86/SSE symbols**; `docs/ANDROID-FEASIBILITY.md` |
| **Prior art has been read, not guessed** | commit `d467818`; `docs/PRIOR-ART.md`, 14 cited sources |
| **The boot got 8× further, and the halt is no longer a livelock** | `/mnt/ssd/vulcan4-build/run/boot_g18b_final.log` → `VULCAN4 BOOT REPORT functions_entered=25 halt=guest_cycle_no_progress bios_files=0`, `dispatcher_transfers=25 serviced_invocations=25`. Was `functions_entered=3 halt=livelocked_in_syscall` |
| **The guest never touches hardware — this is our control flow, not the console** | same run → `distinct_mmio_addresses=0 total_mmio_accesses=0` |
| **The test suite is green** | 449/449 (`ps2x_tests`); 3 entry-vs-resume tests proven to have teeth by mutation, plus `the 0x01000000 window is identity mapped` and two `G1.8b` driver tests, all proven to have teeth the same way |

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

- **The boot wall has MOVED — G1.8b.** It is no longer the syscall-override frame loss. The guest
  now runs its whole init sequence 25 times and the driver only ever sees **one** PC, `0x01000008`,
  the CRT0 entry: it is being resumed at its entry point instead of a resume point. Measured cause:
  `GuestThread::context` is a *copy* of the main frame, and a driver that is not
  `EeScheduler::run()` advances `m_cpuContext` directly, so that copy goes stale. The invocation is
  a child of the stale frame, and publishing it back overwrites the live one. A fix was written and
  **hung the suite, so it was removed rather than shipped half-done**; it is the next dish's work.
- `total_mmio_accesses=0` and `distinct_mmio_addresses=0`. The guest has **never touched a hardware
  register.** It is still in initialisation.
- No IRX module has been loaded, and the runtime has no BIOS path. That is unchanged and intended:
  the runtime serves the console's OS calls itself.
- Because the override lookup can never satisfy the guest's convergence test, the guest **retries
  until the deadline**. That is a livelock, not a crash.

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
| **G1.8** | ✅ (diagnosis) / ❌ (gate) | **our own `-0x01000000` memory-map bias was manufacturing the decoy three goals chased. Removed + tested. Both overrides now really land in the syscall table; the wall is a scheduler frame-loss defect, named at the register level** |
| **G1.8b** | ✅ | **the driver was not honouring the guest's queued invocations. Red test first, then `EeScheduler::serviceInvocations()`. Boot: `functions_entered` 3 → 25, halt `livelocked_in_syscall` → `guest_cycle_no_progress`. `docs/G1.8b-RED.md` has the red proof** |
| **G2.0** | ✅ | GS approach decided and the path proved |
| **G2.1** | ✅ | the GS computes pixels — 43,804 distinct colours |
| **G2.2** | ✅ | the guest draw sequence, documented (and the fault it hit localised) |
| **G2.3** | ✅ | the texture register sequence, documented; PSMT8 swizzle proven by test |
| **G2.4** | ✅ | **the fault was OURS — a hard-coded `PRIM=2` that is a LINESTRIP. Geometry and a sampled texture now draw: 37,275 distinct colours** |
| **G3.1** | ⬜ | **the VU1.** GT4's vertex microcode handled, a 3D scene rendered |
| G3.2 | ⬜ | a real car on a real track, natively |
| G4.1–G4.4 | ⬜ | menus, saves, input, and a full race that completes |
| G5.1 | 🟡 | Spec II support — researched, parked by the captain's call |
| G5.2a | ✅ | Android/ARM64 feasibility — answered with a build |
| G5.2 | ⬜ | the Android port itself (not started; this dish did not start it) |
| G5.3 | ⬜ | regional and special builds |

**The GS now rasterises primitives and samples textures through the real register path.** G2.4 found
that two dishes of "broken rasteriser" were a single wrong constant in our own probe — `PRIM = 2`,
which is `GS_PRIM_LINESTRIP`, not `GS_PRIM_TRIANGLE` (3) — plus two more of ours (`TEST_1 = 0`
meaning `ZTEST = NEVER`, and `PRMODECONT = 0` silently discarding `TME`/`IIP`). All three are fixed,
pinned by a test, and the frame now carries **37,275 distinct colours with a PSMCT32 texture and a
PSMT8+PSMCT16-CLUT texture sampled through real GIF REGLIST packets** — 589 and 24 distinct colours
inside the two quads, which is UV interpolation rather than a flat fill. The full register sequence
a guest must perform is in [`docs/GS-PLAN.md`](GS-PLAN.md) §12.6, and the absence of filtering,
mipmaps, blending, a Z test, CLUT animation and the EE→GS DMA path is named in
[`docs/LIMITATIONS.md`](LIMITATIONS.md), along with three **runtime** divergences that will bite the
real guest (ZTE not honoured, `PRMODE` semantics flattening textured geometry, and texture base
units 32× off for a genuine guest).

**The next real work is the stale main frame, not the renderer and not the values.** G1.8 removed a
memory-map bug that had been manufacturing the boot wall; G1.8b removed a second one — the driver
was catching the scheduler's transfer signal without ever running the invocation the guest had
queued, so every syscall override silently lost the guest's registers. Together they took the boot
from 3 functions to 25 and changed the halt from a livelock to a named restart loop. What remains
is one stale copy: the scheduler's `GuestThread::context` has to be reconciled with the context the
driver is actually advancing, so the guest resumes at a resume point instead of its entry.
After that: **G3.1, the VU1.** A frame on disk that our own code computed is still not a rendered
game.

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
- **The verifier in the G1.8b ticket was wrong and said "FAIL" about a passing build.** It reads the
  log with `sorted(..., key=getmtime)[:3]`, which is the three **oldest** logs. Fixed to
  `reverse=True` it reports the real line. A gate that only ever inspects stale evidence is worse
  than no gate, and this is the second time a measurement in this project has been the thing that was
  broken.
- **One earlier *fix* was wrong, and is recorded at greater length than the fix it replaces.** G1.6
  added a `-0x01000000` bias to `PS2Memory::translateAddress`. It was self-consistent, so nothing
  crashed, and it read as a careful correction. It was wrong, and it **created** the boot wall that
  G1.6, G1.7 and G1.3 then investigated as if it were the game's. G1.8 removed it and left the G1.6
  entry standing with a retraction, because a plausible fix that made things worse is more useful
  than a tidy history.
