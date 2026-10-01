# PRIOR ART — the other PS2 recompilations, and what we take from them

Written by Caine, 2026-10-01, at the captain's request. Read this before planning any G2 (GS) or
G3 (VU1) work. Nothing here is a promise that their approach works on GT4 — it is a map of what has
been proven possible on *some* PS2 game, with the sources named so you can check my claims.

## The short version

`GTTeancum/Timesplitters` is the furthest-along PS2 recomp in public. It boots to its menu, enters
Story mode, moves, turns, aims and fires (ammo 15 -> 14), with a working GS renderer, an emulated
IOP, a modelled SPU2 and VU1/VU0 support. It is NOT a finished port and its own docs say so. That
makes it the best study material on the planet for us, and it is public: github.com/GTTeancum/Timesplitters

## 1. GS — the thing we have not solved yet

Three backends, in `source/PS2Recomp/ps2xRuntime/src/lib/gs/`:

- `gs_cpu_backend.cpp` (95 KB) — a CPU rasteriser, and the reference for correctness.
- `gs_gpu_backend.cpp` (70 KB) — **OpenGL 3.3**, GL pointers borrowed from raylib's glad, sharing
  raylib's context. The header is honest: "Results are close to, but not bit-identical with, the
  CPU rasterizer." Experimental, env-gated `TS_GS_GPU=1`.
- `gs_threaded_backend.cpp` (27 KB) — worker-thread submission.
- `ps2_gs_memory.cpp` (17 KB) + `gs_frontend.cpp` (61 KB) + CLUT/texture/memory cache tests.

**The architecture that matters, quoted from `gs_gpu_backend.h`:** GS local memory stays
authoritative. Uploads, CLUT loads, transfers and readbacks go through the CPU backend; GPU-rendered
buffers are written BACK to local memory before anything reads the pages they cover. The renderer
never owns the pixels.

**The idea I most want you to steal:** `project/game/gs_replay.cpp`. It replays a captured GS
command stream through the rasteriser headless, and hashes GS local memory plus every presented
frame. Identical hashes prove a rasteriser change did not change the picture. For us that converts
"does my GS work?" from an opinion into a test — which is Law 4.

## 2. The bug I want you to check on OUR side first

From their KNOWN-ISSUES.md: VU0's VF0 register was zero in every guest thread except the main one.
Hardware says VF0 = (0,0,0,1). That silently broke the game's soft-float doubles —
`1.5 - 2.25` returned `15.25` — which broke atan2, and therefore AI facing and animation timing.
They found it because NPCs stopped facing the player.

Second, in the same entry: they fixed `BLTZ/BGEZ/BLEZ/BGTZ` being translated with a 32-bit test.
The R5900 tests the FULL 64-bit register. 724 generated files had to be regenerated. If our branch
translation is 32-bit anywhere, we have this bug and do not know it.

Third: they had been running libm/libvu0 through SDK HLE with the wrong ABI (doubles read from
FPRs instead of GPRs, W wrongly included in Normalize). They replaced the host shortcuts with the
GAME'S OWN routines and added `TS_LIBM_SELFTEST=1` to check those against host libm. Prefer the
game's own maths over a convenient host call.

## 3. FileIO — the shape we already built, and their one hint for our current wall

Their `Kernel/Syscalls/FileIO.cpp` is the same chain we have:
`fioOpen -> vfs().open(ps2Path, flags, currentVfsMounts(), runtime->romDevice())`.
The mounts are `{hostRoot, cdRoot, mcRoot}` — a VFS with roots, not directories we must pre-create.

Our current wall is a missing disc directory (GT4 assembling `cdrom0:\GAMEDATA\...`). Their chain
proves the fix shape is a MOUNT/PREFIX question, not a "make the folder" question. Please test that
before treating it as a data problem.

Their `fstat` is a fake: `memset(statBuf, 0, 128); setReturnS32(ctx, 0);`. That is the exact Law 2
violation you caught in ours. Do not copy it, and do not "fix" ours the way they fixed theirs.

## 4. VU1 — upstream is genuinely unfinished, and there is a way around it

Open on ran-j/PS2Recomp: #254 compile whole VU1 programs + run sound IRX natively; #200 reordered
instruction pairs must both take the pre-pair VF and Q; #189 MAC/STATUS/CLIP flag registers; #165
float precision in VU0/VU1; #161 VU1 instruction unit tests. Our long pole is real.

The way around: `nathanialf/ico-recomp` (MIT) has its OWN GS renderer doc (183 KB
`docs/GS_RENDERER.md`) and its own VU1 toolchain. Read it before we commit to a VU1 approach.

## 5. Discipline worth copying verbatim

- `hand_edited_generated_bodies: false` is a stated invariant. 724 files were REGENERATED when the
  branch bug was fixed, not patched.
- Every feature env-gated and off by default: `TS_GS_GPU=1`, `TS_NATIVE_MUSIC=0`,
  `TS_LIBM_SELFTEST=1`, `TS_DISPLAY_MENU=0`. Same posture as Law 12.
- Their sound-memory fix is our Law 2 shape: the old handler returned success without copying bytes.
  The repair validated ranges and FAILED rather than reporting success. Test counts 7/25 -> 25/25,
  always stated before/after.
- Their scale, for reference: 2,376 original EE functions, 2,673 generated files reproduced
  byte-identical, 705 checks passing, 34 framework files changed from base.

## What is NOT here

- `GoomiiV2/TS-ReSplit` is a C# Unity ASSET engine, not a recomp. 2021, 8 stars. Wrong project.
- `HFTSRedux/TS2Redux` is the old Homefront port, also not a recomp.
- Do not read `project/generated/` in their repo. 3,753 paths, nearly all generated. Start at
  STATUS.md, then KNOWN-ISSUES.md, then the CANONICAL-HANDOFF, then only the gs/ and vu/ directories.

## How to use this

This is REFERENCE, not a dish. Do not start porting their renderer tonight; G2 is still behind the
boot wall. When we do reach G2, the two things to try first, in order, are (a) the GS replay +
frame-hash harness, so our renderer becomes testable, and (b) CPU-rasteriser-first, GPU-second.
Report back if any of this is wrong or unverifiable — I would rather be corrected than agreeable.
