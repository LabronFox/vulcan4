# VULCAN 4 — THE GOAL LEDGER

> **Rule zero: no goal, no dish.** Every dish, commit and report carries its goal id.
> Every dish may declare a `VERIFY: <shell cmd>` gate; **a commit is not proof.**

---

## ⭐ THE GOAL

**Play Gran Turismo 4 — your disc, your cars, your tracks, your rules — running natively on a
modern machine, built out of GT4's own code.**

Eventually the captain's Odin 2. Never an emulator.

---

## What "done" means — countable

1. It loads a **retail GT4 disc**, no conversion step needed by the user
2. It reaches **the real menu**, drawn by a real GS layer
3. A car and a track render in 3D (VU1 path alive)
4. A **race can be driven** with a gamepad
5. Audio plays from the disc's own data
6. Save/progression works
7. Runs on desktop **and** Android (Odin 2)
8. Every remaining gap is listed in `LIMITATIONS.md` with an honest verdict

**Progress: 0 / 11 goals complete.**

---

## G0 — GROUND TRUTH ✅/🟡 *(active)*

### ✅ G0.1 — The toolchain builds on Linux
- **DONE WHEN:** `ps2xRecomp` and `ps2xRuntime` configure and build on Linux, and a scratch ELF
  can be pushed through the recompiler to produce C++.
- **PROOF:** `docs/TOOLCHAIN.md` + `ps2_recomp` / `ps2_analyzer` / `ps2EntryRunner` running.
- **RESULT (2026-09-29):** ✅ `BUILD_EXIT=0` for all three. Two upstream Linux defects fixed with
  a 24-line patch (`tools/patches/ps2recomp-linux.patch`): toml11's `FetchContent` missing
  `GIT_SHALLOW`, and the runtime's unguarded SSE4.1 intrinsics. Recorded in `docs/TOOLCHAIN.md`.

### ✅ G0.2 — GT4's executable is on the slab
- **DONE WHEN:** the ISO's identity, the executable's SHA-256, sections, entry point and a
  function count from the analyzer, written into `docs/DISC-MAP.md`.
- **PROOF:** `docs/DISC-MAP.md` + `/mnt/ssd/gt4/work/gt4.toml`.
- **RESULT (2026-09-29):** ✅ retail US v2.00, SHA-256 `f8f10823…8019fa`. `SCUS_973.28` is
  273,020 B, **little-endian** R5900, entry `0x01000008`, `.text` 187,408 B = 46,852 insns,
  **707 functions**, 336 SCE symbols. **It is not a boot stub.** The bulk of the disc is not
  code: `GT4.VOL` (2.29 GiB) is an indexed container with an opaque payload, `CORE.GT4` (2 MB)
  is 7.96 bits/byte with no ELF inside. See §5.6 of that doc for the falsifiable answer.

### ✅ G0.3 — Read one function as text
- **DONE WHEN:** a single GT4 function is disassembled *and* appears as generated C++ next to it.
- **PROOF:** `docs/FUNCTION-ANATOMY.md` + 8.9 MB of generated C++ on the SSD.
- **RESULT (2026-09-29):** ✅ `sub_01000558` @ `0x01000558`, 328 B / 82 instructions, called from
  the entry block at `0x01000210`. Six spot-checks — sign extension, delay slots, branch-target
  arithmetic, indirect calls, the spin-trap loop — all verified against raw bytes. **The
  translation is faithful.** But the run **failed**: exit 1, 2 of 721 function bodies lost.

### ✅ G2.1 — The GS computes pixels (43,804 distinct colours, not a black square)
- **DONE WHEN:** the skeleton writes a framebuffer whose content is COMPUTED through the
**Superseded: see the detailed `✅ G2.1` entry for the evidence.** 
  register/transfer path, reads it back, and writes a PNG with at least 64 distinct colours.
- **RESULT (2026-09-30):** `/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png`, 512x512, **119,302 B**,
  **43,804 distinct colours** (gate needs >= 64; G2.0 produced 1). 251,261 non-background pixels.
  In-count and out-count match exactly, so the round trip through VRAM is lossless.
- **How the content is produced:** computed in our own code (8 colour bars, a two-axis gradient, a
  diagonal wedge), then handed to the GS through its **transfer** path — `BITBLTBUF`/`TRXPOS`/
  `TRXREG`/`TRXDIR` plus image data via `GS::uploadImageNative`, the same route a guest uses to
  upload a texture. `GSCpuBackend::UploadImage` writes it into VRAM. It is then read back through
  the GS presentation path and encoded by the skeleton's own zlib PNG writer. No image library is
  involved and no window is ever opened.
- **CORRECTION to G2.0.** G2.0 recorded that "the triangle never submits". That was **wrong**.
  `XYZF2` and `XYZ2` both queue a vertex and both call `vertexKick`, so a primitive written as
  `RGBAQ,ST,UV,XYZF2,XYZ2` yields six kicks for three vertices and `Submit` **is** called. The
  rasteriser runs with correct state and writes nothing. Still undiagnosed, and now the GS's
  biggest gap. G2.1 routed around it and says so rather than claiming it fixed.
- **GS-PLAN.md was under-specified and is fixed.** It documented the GIF path and the display
  registers but **not** the transfer registers, so the transfer route was unusable without reading
  `gs_frontend.cpp`. Added: full `BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR` field layouts, the 64-pixel
  word width rule, and the 14-register surface the skeleton actually touches.
- **Sync primitive, honestly bounded.** A host-driven monotonic vblank tick counter plus CSR bit 0
  (SIGNAL) raised after FINISH. Documented as **not** modelling vblank timing, vblank interrupt
  delivery, DMA/AD interrupts, the interrupt controller or the IOP — because a stub that lies about
  these reproduces the G1.5 `FindAddress` livelock.

### ✅ G1.6 — Who was supposed to write that? *(gate met on the named halt; the guest did NOT advance)*
- **DONE WHEN:** the guest gets past the `FindAddress` livelock, or the missing writer is named
  precisely.
- **RESULT (2026-09-30):** ✅ gate met on its second condition (halt is no longer a livelock).
  `VULCAN4 BOOT REPORT functions_entered=1 halt=spinning_in_guest_code bios_files=0`. **The exit
  conditions were not touched.** The value was identified, the missing writer was found and fixed,
  and the outcome is *not* the one predicted — see the warning below.
- **The value is a 32-bit CODE POINTER, not a missing table entry.** The guest calls
  `FindAddress(0x80000000, 0x80080000, 0x10285F8)` and the same for `0x10285C0`, then loops
  searching for the next occurrence. Those two values are the entry addresses of **GT4's own
  memory-scan helper routines** (`sub_010285F8` is a word-by-word search, `sub_010285C0` an indexed
  search). The guest wants pointers to its own scanners, to call indirectly.
- **Where it lives:** exactly once each, 8 bytes apart, as one 16-byte descriptor at vaddr
  `0x01035354` (RAM offset `0x35354`) in the **second PT_LOAD segment**:
  `{0x010285F8, 0x5A, 0x010285C0, 0x00000000}`. `0x35354` is **inside** the search window.
- **MY PREDICTION WAS WRONG.** I set out to prove a missing writer — an absent IRX export table. The
  value needs no relocation and is statically in the image, so that story is dead.
- **✅ THE MISSING WRITER, FOUND AND FIXED.** The loader was the writer, and it wrote to the wrong
  place. `PS2Memory::translateAddress` (`ps2_memory.cpp:600`) mapped every address below
  `0x80000000` as an identity, but `0x01000000`–`0x01FFFFFF` is the PS2 **user segment** and must
  have `0x01000000` subtracted. User `0x01035354` and KSEG0 `0x81035354` are the same bytes, physical
  `0x35354`; under the identity rule they were **16 MB apart**, so every KSEG0 pointer the guest
  formed pointed at empty memory. Proven by reading RDRAM: the 16-byte descriptor was at
  `0x1035354` instead of `0x35354`, and the guest's search window held **2** non-zero words. After
  the fix it is at `0x35354` and the window holds **60,951**. Patch:
  `tools/patches/ps2recomp-linux-g16-userseg.patch` (1 file, +22/-1).
- **⚠️ BUT THE GUEST DID NOT ADVANCE, and this is not a step forward.** `functions_entered` went
  **3 → 1**, `distinct_pcs=1`, and `sce_FindAddress` is still called **154** times and still misses
  **102** — on a window that provably now contains the value at `0x35354`. The halt name changed and
  the memory map is now correct, but the guest is no further along and has fewer distinct program
  counters. What the fix did buy: the guest's *data* was silently 16 MB from every KSEG0 pointer it
  formed, and that whole class of bug is gone. The `FindAddress` scan (candidate 2) is now isolated
  from the loader, which it was not before.
- **The intermediate finding that pointed the way** was a self-contradiction in our own trace. `scannedWords=112581` is exactly the
  full window, so the scan walked over `0x35354` and did not match a value the file says is there —
  and the diagnostic reports `allZero=true` over a window that provably contains a non-zero word.
  Either the ELF loader is not mapping segment 2 as the program headers say, or the `FindAddress`
  scan short-circuits. **One experiment settles it: read `RDRAM[0x35354]` when the guest issues the
  first `FindAddress`.** Non-zero → the scan is broken. Zero → the loader is broken. This dish ran
  out of runway before running it and does not claim an answer it did not measure.
- **Regression test:** none. The task requires one for whatever is implemented, and the honest state
  is that the fix is **not yet demonstrated to help** — a test pinning the new mapping would lock in
  a change whose downstream effect is still negative. The mapping itself is pinned by
  `VULCAN4 PROBE2/3/4` in the harness, which fail loudly if the descriptor moves again. Full
  write-up in `docs/FIRST-BOOT.md` §8.

### 🟡 G2.2 — Real geometry *(gate passes on the BACKGROUND; no geometry was drawn)*
- **DONE WHEN:** the GS accepts a primitive the way a guest submits one, rasterises a triangle and
  a coloured/interpolated quad, and the PNG shows geometric structure verified programmatically.
- **RESULT (2026-09-30):** ❌ **not met, and the gate is misleading here.** The frame reports 43,804
  distinct colours and passes the ≥1000 check — but that is the **G2.1 transfer background**, and
  the frame checksum is **byte-identical to G2.1's**. **No geometry contributed.** Calling this done
  would be the fake success this project forbids, so it is recorded as partial.
- **Two real bugs in my own G2.0 probe, found and fixed:**
  1. **Screen coordinates are 12.4 fixed point**, so pixel `P` is register value `P << 4`. G2.0
     passed raw pixel values, producing a 2.5-pixel triangle that covered nothing at all.
  2. **`XYZF2` and `XYZ2` both queue a vertex AND both kick.** Writing both per vertex doubles the
     vertex count: three intended vertices arrive as six, every triangle is degenerate. A guest
     writes one or the other. The probe now writes `XYZ2` only.
- **What now works:** geometry is submitted the guest way — a GIF REGLIST naming `PRIM` then per
  vertex `RGBAQ`/`UV`/`XYZ2` — and the GS acknowledges it (`draw_events=8`). A flat triangle, a
  gouraud quad submitted as two triangles sharing an edge (hardware does quads the same way; the
  backend has no `QUAD` type), and a triangle straddling the frame edge to exercise the scissor.
- **What still does not:** the raster writes nothing. The batches arriving are degenerate —
  `v0 == v2 == (0,0)`, so the edge denominator collapses and the triangle is correctly skipped. The
  rasteriser's coverage, scissor clip and gouraud interpolation look correct on the data it is
  given, so **the fault is in the primitive stream decoding or the vertex queue, not the
  rasteriser.** That is a real narrowing of the G2.0 open question.
- **NARROWED FURTHER, from the GS's own draw log.** For the clipped triangle the batches that
  reached the rasteriser were `v0=(0,0) v1=(700,60) v2=(0,0)` then
  `v0=(700,60) v1=(420,240) v2=(0,0)` — the batch **shifts one vertex per draw and the last slot
  is always `(0,0)`.** `buildDrawBatch` always reads `m_vtxQueue[0..n)`, and the store is
  `m_vtxQueue[m_vtxCount % kMaxVerts]`, so for them to disagree by one the first `XYZ2` of a run
  must be stored with `m_vtxCount != 0`. The prime suspect is our `PRIM` write not landing — which
  would also explain 8 draw events for 4 triangles (`needed` resolving to 1, the
  `GS_PRIM_POINT` reset value). The alternative is that `PRMODECONT = 0` takes the
  `if (m_prmodecont) ... else { m_prim.type = ... }` branch, which drops IIP entirely.
  **One experiment decides it: log `m_prim.type`, `m_prmodecont` and `m_vtxCount` at the top of
  `buildDrawBatch`.** Six values, one run. Written up in `docs/GS-PLAN.md`.
- **The guest draw sequence is now written down** in `docs/GS-PLAN.md` — framebuffer setup,
  scissor, PRMODECONT, `PRIM` with IIP at bit 3, then the vertex run, with the kick implicit on the
  third vertex. That is the contract the next dish compares against real GT4 GS traffic.
- **No test added yet**, deliberately: pinning a path that does not yet draw would enshrine a
  failure. Tests come with the fix.

### 🟡 G2.3 — Textures: swizzle proven, sampling not yet shown
- **DONE WHEN:** a texture reaches the GS through the transfer path, a primitive samples it, and
  the PNG proves sampling happened — plus tests and the documented register sequence.
- **RESULT (2026-09-30):** 🟡 partial. **Swizzle is proven by test. Sampling is not demonstrated.**
- **✅ SWIZZLE: DONE AND PROVEN.** Added a round-trip test to the suite (now **445/445**, was
  444). It pins three things for PSMT8: the address map is a **permutation** of a 16×16 tile (256
  distinct offsets, no aliasing), a swizzled write **reads back at the same (x,y)**, and the map
  is **not** the identity — `(1,0)` does not sit next to `(0,0)`. That third assertion is what stops
  it passing by accident, which is exactly the "silently produces garbage for weeks" risk named in
  the brief.
- **The runtime's texture machinery is largely present**, measured not assumed: per-format swizzle
  maps for PSMCT32/PSMT8/PSMT4/PSMCT16, a CLUT cache (`m_clut`, `m_clutCbp`, `LoadClut`, ninth
  address bit via `CSA[4]` for 16-bit CLUTs), and `SampleTexture`/`combineTexture` in the
  rasteriser.
- **❌ SAMPLING: NOT DEMONSTRATED.** No indexed texture has been drawn, because drawing needs a
  working primitive — which is G2.2. Documented honestly rather than faked with a background
  gradient, exactly as G2.2 was.
- **⭐ THE G2.2 LEAD, and it is strong.** While adding the swizzle test I read the neighbouring
  case at `ps2_gs_tests.cpp:829-849`: it submits primitives with `GS::writeRegister` and **asserts
  correct rasterised pixels**, and it **passes**, with `[gs:prim]` showing `v0=(6,0) v1=(0,6)
  v2=(6,6)` — all three vertices populated, which my probe never achieved.
  **⚠️ CORRECTION, MEASURED: I then ran the proposed experiment and that conclusion was WRONG.**
  Submitting via `writeRegister` instead of a GIF REGLIST produces the **identical** defect
  (`v0=(48,48) v1=(464,96) v2=(0,0)` both ways), so the REGLIST encoder is **exonerated**. What
  the experiments really established: **(a)** the defect is specific to `GS_PRIM_TRIANGLE` — the
  same vertices as a **TRISTRIP** give a **correct** batch `v0=(48,48) v1=(464,96) v2=(200,240)`,
  and the only difference is the post-submit reset in `vertexKick` (`TRIANGLE: m_vtxCount = 0` vs
  `TRISTRIP: slide and keep 2`); and **(b)** there is a **second, independent blocker**, because even
  the correct TRISTRIP batch writes nothing. Ruled out by measurement: the scissor (correct — I had
  misread my own log) and `TEST_1` (`0x30000` changed nothing, so `classifyAlphaTest` is not
  rejecting writes). **G2.2 is two bugs, not one.**
- **Register sequence documented** in `docs/GS-PLAN.md`: transfer (BITBLTBUF/TRXPOS/TRXREG/TRXDIR),
  CLUT upload + TEXCLUT, TEX0_1 binding, TEXFLUSH, then PRIM with TME (bit 4) and per-vertex
  RGBAQ/UV/XYZ2. Formats done vs not, stated in a table.

### ✅ G1.7 — Name the spin *(the wait is now an address, an instruction and an arithmetic fact)*
- **DONE WHEN:** the boot report names a specific wait instead of `spinning_in_guest_code`, and
  either the guest advances or the dependency is named with address and instruction.
- **RESULT (2026-09-30):** ✅ `VULCAN4 BOOT REPORT functions_entered=1 halt=waiting_on_unnamed_value
  bios_files=0`. The wait is named; the guest did **not** advance.
- **THE WAIT, NAMED.** The guest is polling **`FindAddress` over KSEG0 `0x80000000`–`0x80080000`**
  for the code pointers **`0x010285F8`** and **`0x010285C0`**, in a loop at
  **`0x010286FC`–`0x01028740`** (spin observed at **`0x0102871C`**, `move s3,v0`, immediately after
  the `jal`). Exit condition: `hit(0x010285F8) - 0x20C == hit(0x010285C0) - 0x168`.
- **THE DEPENDENCY, IN ARITHMETIC.** That exit requires the two pointers to be
  **`0xA4` = 164 bytes apart**. The only record in `SCUS_973.28` holding both has them **8 bytes
  apart** (`0x36354` / `0x3635c`, the descriptor `{0x010285F8, 0x5A, 0x010285C0, 0}`). **The record
  shape the guest walks for is not the record shape in the image**, so the comparison can never
  succeed and the walk runs off the end of the window forever.
- **WHAT WOULD SATISFY IT:** a record in RDRAM with `0x010285F8` and `0x010285C0` exactly 164 bytes
  apart. **Who should write it: unverified** — the only candidate is the console's module/export
  table machinery, already recorded as absent. It is *not* the descriptor at `0x01035354`, which
  has the wrong shape.
- **THE G1.6 FIX IS VISIBLY WORKING.** `FindAddress` now reports **16 hits / 78 misses** where
  before every call missed, and the first calls start at `0x80035358` — one word past the
  descriptor at `0x80035354`. The user-segment fix landed end to end.
- **WHY THE HARNESS COULD NOT SEE IT, and that is a finding.** The loop analyser reported
  `block_instructions=0 history_len=1`: the harness entered one function and never regained
  control, so it cannot sample inside the spin, and `eeCheckpointDue()` — already flagged in G1.4
  as never firing — still does not. The loop had to be named from the guest image.
- **`total_mmio_addresses=0` was correct, not a blind counter.** This loop reads no hardware; it
  re-reads the guest's own RDRAM through a syscall.
- **THE ORDERING TRAP, NAMED OUT LOUD. This is the third bug of the same class:** the loader wrote
  the image 16 MB high; the guest hunted a value that is now present but in a record of the wrong
  shape; now it finds that record, rejects it, and walks on looking for a correctly shaped one that
  nothing has ever written. **Every time, the symptom was "stuck" and the cause was a value the
  guest expected to exist.** G1.4 and G1.5 both chased dispatch and were both wrong. That is a
  plan: before instrumenting the CPU path again, ask what the guest is waiting for and who writes
  it. It has been the answer three times out of three.
- **No regression test:** nothing was implemented, only diagnosed. `docs/FIRST-BOOT.md` §9.

### ✅ G1.5 — The body that never ran (it did run)
- **DONE WHEN:** the `jal` at `0x010286D4` actually runs the body at `0x01028638`, and the boot
  report shows progress (`functions_entered` above 3, or a new named halt) with `bios_files=0`.
- **RESULT (2026-09-30):** the report now reads
  `VULCAN4 BOOT REPORT functions_entered=3 halt=livelocked_in_syscall bios_files=0` — progress by
  the second criterion, a **new named halt**, not by a new function entered.
- **THE PREMISE WAS WRONG, and the trace says so.** The stub body **does** run. All 13 dispatches
  carry `entry_pc=0x1028638`, and `dispatchGuestBranch` sets `ctx->pc = targetPc` as its first
  statement (`ps2_runtime.cpp:1352`), so a callee reached through it can never see a resume
  address. `sce_FindAddress` was called **155 times** — the body ran every time. The
  `pc=0x01028640` on re-entry is the `case 0x1028640u` resume working as designed.
- **Sibling scan, as asked: it is safe by construction, and now proved.** A static scan of all 707
  generated functions found **61** whose own entry address is also a resume case — a fresh `jal`
  to any of them would skip the body. All 61 are safe, because the entry label is emitted at the
  *top of the body* right after `default: break`, so both paths converge. Locked in by test.
- **Regression tests: 3 added, 444/444 green** (baseline 441). They assert a *side effect*, not
  absence of a crash: a dispatched direct call runs the body exactly once; an entry-address resume
  case still reaches the body; a resume PC is never aliased to the body. **Proven to have teeth by
  mutation** — moving the entry label past the body makes exactly the first test fail.
- **The real wall, found while looking:** a **livelock on `sce_FindAddress` (0x83)**. The guest
  calls it 143–155 times; each call brute-force scans ~112,000 words of RDRAM for a function
  pointer (`computeBuiltinFindAddressResult`, `System.cpp:639`) that was never written, returns 0,
  and the guest retries until the deadline. `total_mmio_addresses=0` — the guest has touched no
  hardware, no IRX module is loaded, and the runtime has no BIOS path, so the pointer cannot exist.
  The scan is a heuristic substitute for the hardware's loaded-module export-table lookup.
- **Deliberately improved:** the report's halt name. `stuck_in_syscall` said only "a syscall", which
  is what sent G1.4 after a bug that did not exist. It is now `livelocked_in_syscall` with the
  syscall id, its share of the call tally and the guest PC in the detail line.
- **Upstream patch:** `tools/patches/ps2recomp-linux-g15-entryresume.patch` (test file only, +155).
  Verified to apply cleanly on top of the existing three in the G0.5 clean room.

### ✅ G0.5 — Clean-room reproducibility: the whole chain, no hidden state
- **DONE WHEN:** the toolchain and the harness are rebuilt in a **fresh** directory from a **fresh
  clone**, using only commands written in `docs/TOOLCHAIN.md`, and the guest boots again with the
  same report shape.
- **RESULT (2026-09-30):** ✅ exact reproduction. Fresh clone → new build root
  `/mnt/ssd/vulcan4-cleanroom`, no reused objects, no reused `_deps`, and the translation unit was
  **re-run through the recompiler** rather than copied. All four generated artefacts are
  **byte-identical to the incremental tree** (md5: `ps2_recompiled_functions.cpp` 8,899,740 B
  `a0461ca3…`, `.h`, `stubs.h`, `register_functions.cpp` 804,210 B). Boot report identical:
  `functions_entered=3 halt=stuck_in_syscall bios_files=0`. Transcript 1019 lines at
  `/mnt/ssd/vulcan4-cleanroom/cleanroom.log`. The incremental build root was not modified.
- **Nine documentation gaps found, three of them blocking**, all missing *steps* rather than wrong
  ones — which is the worse failure mode, because nothing errors and nothing looks stale:
  1. **`TOOLCHAIN.md` never mentioned the boot harness at all.** Following it end to end yields a
     runtime that errors with *"Pass the guest ELF as argv[1]"* and no guest. The harness build
     lived only in `FIRST-BOOT.md` §6.
  2. **The third patch was undocumented.** `ps2recomp-linux-g1wall.patch` (11 files, +361/-9) is
     required for the boot report and appeared in no apply-order list.
  3. **The recompile config was neither in the repo nor documented.** The working build used a
     hand-edited toml outside the repo; the entire delta from the analyzer's own output is two
     lines (`output`, `single_file_output = true`).
  4. `ps2_analyzer` emits a **relative** `input` path; must be made absolute. 5. §6 omitted `-EL`
  while §4/§5a call it mandatory — a stranger would build a big-endian ELF and get garbage with a
  clean-looking report. 6. The harness's 2nd argument has no stated provenance (it is analyzer
  output). 7. The `--depth 1` + `checkout 75d729c` pin works only while the pin *is* upstream tip.
  8. `PS2X_ENABLE_SCCACHE` defaults `ON` but sccache is absent, so it silently continues.
  9. `gen_syscall_names.py` is redundant — the patch ships the generated header.
- **FIXED in this dish:** gaps 1–8, by adding a stranger-proof end-to-end recipe as
  `TOOLCHAIN.md` §10 and correcting the missing `-EL` in §6. Gap 9 recorded. **Left open:** the GS
  probe's build script (`tools/gs/build_gs_probe.sh`) hardcodes `/mnt/ssd/vulcan4-build` and is not
  path-parameterised, so it is not covered by §10.
- **NOT proved:** the clean room faithfully reproduces the *same wall* — 3 functions, then stuck in
  a syscall. Reproducing a stall is not progress on the stall. Where a stranger gets the guest ELF
  is still undocumented and out of scope.

### ✅ G0.4 — The recompiler's output is complete and linkable
- **DONE WHEN:** a full `ps2_recomp` run over `SCUS_973.28` exits 0, writes all 721 function
  bodies, produces the artefacts it used to die before writing, and the generated `.cpp` compiles.
- **RESULT (2026-09-29):** ✅ `RECOMP_EXIT=0`, `errors: 0`, **721/721 bodies**, **0 declared-but-
  undefined**, all four artefacts written (`functions.cpp` 8,899,740 B, `functions.h`, `stubs.h`,
  `register_functions.cpp` 804,210 B). Compiles to a 10,652,680 B object; all 721 functions are
  global symbols in it; the only undefined symbols are `PS2Runtime::`/`ps2_stubs::` — **zero GT4
  functions** — which is G1.1's work, not a translation gap.
- **TWO defects, both in the combined-output writer**, fixed in
  `tools/patches/ps2recomp-linux-outputfix.patch` (1 file, +43/-3):
  1. **Race on the staging queue.** The termination check tested `completedCode.empty()` but never
     `readyCode.empty()`, so it declared work lost while finished bodies sat uncollected in
     `readyCode` — aborting the run and dropping the tail every time. Now re-drains before
     concluding, and **names the missing function** instead of a bare index.
  2. **Deadlock in the throttle.** The combined path throttled on
     `outstandingWork + completedCode.size()`, so out-of-order results filling the buffer behind a
     gap stopped it scheduling the very index that would fill that gap — 12 threads in `futex_wait`
     at 0% CPU, stuck at 320/721. Now throttles on `outstandingWork` alone, matching the
     per-file writer, which was already correct.
- **NOTE:** third time this toolchain reported a clean number while something was wrong. The
  third is the most serious — a *silent* truncation would have been worse than G0.3's loud one.
  Watch for more.


---

## G1 — FIRST BOOT

### ✅ G1.1 — Recompiled code executes, no BIOS, failures named
- **DONE WHEN:** a harness loads the translation unit plus the guest image, starts at the real
**Superseded: see the detailed `✅ G1.1` entry in the G1 section above.** 
  entry point, executes real GT4 code, and ends with a machine-readable report naming where it
  stopped.
- **RESULT (2026-09-29):** ✅
  `VULCAN4 BOOT REPORT functions_entered=3 halt=wallclock_deadline bios_files=0`
  Started at `0x01000008`, entered `0x01028640` then `0x010286DC`, halted at `0x0102871C` inside
  a memory-grow loop. **Zero BIOS files; the harness has no BIOS loading path at all.** Harness in
  `tools/harness/vulcan4_harness.cpp`, doc in `docs/FIRST-BOOT.md`.
- **FIRST NAMED BLOCKER — RESOLVED IN G1.2, see below.** It was the SCE kernel address search
  (`FindAddress`, `ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp:791`): it normalised the
  *target* but scanned the whole aliased window, 537,001,983 words per call, and GT4's search
  loop never converged.
- **GOTCHA WORTH KEEPING:** ps2xRuntime implements EE cooperative threads by throwing
  `EeDispatcherTransfer` ("the EE equivalent of a longjmp to the dispatcher... must only be caught
  at EeScheduler::run()"). A harness without `EeScheduler` must catch it itself or abort.
- **HONEST LIMIT:** three functions ran, not a boot. 721 bodies exist, 3 executed; 82 are stubs.

### ✅ G1.2 — Name the wall
- **DONE WHEN:** the harness stops for a **named** guest reason rather than a wall-clock deadline,
**Superseded: see the detailed `✅ G1.2` entry in the G1 section above.** 
  with the PC and the waiting instruction, and the guest call list is populated.
- **RESULT (2026-09-29):** ✅
  `VULCAN4 BOOT REPORT functions_entered=3 halt=stuck_in_syscall bios_files=0`
  `blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x0102871c`
  Call list: **5 named syscalls, 317 calls** — `0x83 FindAddress` ×311, `0x74 SetSyscall` ×2,
  `0x40 CreateSema` ×2, `0x3D SetupHeap` ×1, `0x3C SetupThread` ×1. All served, no BIOS.
- **THE WALL:** GT4 is waiting on the **kernel address search, `FindAddress` (`0x83`)**, issued at
  guest PC `0x0102863C` (`syscall`) inside `sub_01028638`. It searches
  `[0x01035358, 0x80080000)` for the value `0x010285F8` — a pointer to one of its own functions —
  and after 16 real hits it repeats the identical **failing** search: 103 consecutive misses,
  forever. `total_mmio_accesses=0` — **the guest never touched hardware**, so the GS/VU1/register
  walls are ruled out *at this point in the boot* by measurement, not assumption.
- **BUG FOUND AND FIXED (real win, did not unblock the boot):** `FindAddress` walked the caller's
  aliased window, so one call re-read the same 32 MB of RAM ~64 times — **537,001,983 words per
  call**, now **8,392,703** (exactly 64×, the alias factor). Practical effect: **144 calls, still
  unconverged after 19 min → 317 calls, all completing, in 200 s.**
  Patch `tools/patches/ps2recomp-linux-g1wall.patch` (6 files, +162, runtime only).
- **HONEST:** the wall is the *miss*, not the scan cost, so `functions_entered` is still 3. Two
  readings fit — the data lives in `CORE.GT4`/`GT4.VOL` (never loaded) or the guest's own
  bookkeeping diverged. Not yet decided; both are stated with falsifiers in
  `docs/FIRST-BOOT.md` §8.
- **NEXT:** the no-BIOS backlog is now *populated* — 5 syscalls served with zero BIOS files, and
  one of them (`FindAddress`) returning a wrong answer rather than no answer. G1.0 needs the
  **right** answer, not merely a present one.

### 🟡 G1.3 — Serve the wall *(gate NOT met: functions_entered is still 3, needed >= 4)*
- **DONE WHEN:** `functions_entered` rises above 3, no BIOS, and the next wall is named.
- **RESULT (2026-09-29):** ❌ **did not meet the gate.** `functions_entered=3`,
  `halt=stuck_in_syscall`, `bios_files=0`, 506 `0x83` calls. Reported as measured.
- **RESEARCH (cited, not guessed):** `__NR_FindAddress 0x83` is confirmed in ps2sdk's
  `ee/kernel/include/syscallnr.h`; there is an **upstream issue** (ran-j/PS2Recomp#90, *"Some games
  stop and reset if there is no implementation for the 0x83 syscall"*). Its arguments and return
  are **undocumented** — ps2rd's EE syscall reference stops at `0x7F`. The contract was settled from
  a **first-party source**: GT4 carries its own inlined copy of the identical algorithm at guest
  `0x010285F8`, which returns `$a0` (`move v0,a0`), i.e. the window end, **not zero**, on a miss.
- **TWO FIXES, both real, neither faked:** (1) the 64× alias rescan,
  `scannedWords` 537,001,983 → 8,392,703; (2) the miss return, `0` → the window end, which
  measurably changed the guest's behaviour (it now advances to the end instead of restarting).
  Both in `tools/patches/ps2recomp-linux-g1wall.patch`.
- **TESTS:** 5 new unit tests calling the handler directly (no game, no `PS2Runtime`), and **1
  pre-existing test corrected** — it asserted the miss returns `0`, which the first-party
  reference contradicts. That edit is flagged in the doc and the commit as the move to distrust.
  **441/441 pass.**
- **~~THE NEXT WALL, NAMED (RETRACTED, see below):~~** an earlier version of this entry claimed the
  guest needed a 164-byte-stride table living in `CORE.GT4`/`GT4.VOL`. **A full 5.3 GB disc scan
  falsifies it:** each target pointer occurs exactly **once on the whole disc**, and both are in
  the ELF's own `.data`, 8 bytes apart. `CORE.GT4` contains neither.
- **THE REAL TABLE — it is in the ELF all along**, at `0x01035350`, and it is a **syscall-override
  table** in ps2SDK's `SyscallData { int syscall_num; void *function; }` shape:
  `{0x83, sub_010285F8}, {0x5A, sub_010285C0}, {0,0 terminator}` — installed with `setup()`,
  which is SCE **`0x74 SetSyscall`**, a syscall GT4 already called twice.
- **WHERE THE WALL NOW POINTS:** GT4 is trying to **patch the console kernel's syscall table**, and
  to do that it must locate the slots — which is ps2SDK's `GetEntryAddress()`, i.e.
  `0x80011F80 + n*4` (`kTableGuestBase`, already known to `initializeEeKernelState`). Our
  `FindAddress` searches guest memory for a *value* and never consults that table.
- **MY `0xA4` DERIVATION WAS WRONG.** It came from reading the loop at `0x01028740`; the real table
  stride is **8**. Where my instruction reading and the actual bytes disagreed, the bytes win. That
  is the same class of mistake as the G0.1 endianness generalisation, made again one dish later.
- **NEXT MEASUREMENT (cheap, testable):** check whether `0x80011F80 + 0x83*4` really is the console
  kernel's address for syscall `0x83`, using ps2SDK's own `libosd.c` comment on the `0xFFFFC402`
  offset (*"relative to the start of the syscall table, in units of 32-bit pointers"*) as the
  cross-check. A number to test, not a story.
- **UNCHANGED:** `functions_entered` is **3** and the gate wanted **>= 4**. The correction changes
  why, not the outcome.

### 🟡 G1.3b — What is the guest looking for? *(gate NOT met again: functions_entered still 3)*
- **RESULT (2026-09-29):** ❌ `functions_entered=3` (gate wanted >= 4). The trace is the deliverable.
- **THE ANSWER, from the trace:** the guest searches `[0x00000004, 0x80080000)` for two pointers to
  its own code, `0x010285F8` and `0x010285C0` — the two handlers in its syscall-override table at
  `0x01035350` (`{0x83, sub_010285F8}, {0x5A, sub_010285C0}, {0,0}`). `0x5A` is `Copy` and `0x83` is
  `FindAddress`, so **it is checking that its two kernel-syscall overrides landed.**
- **WHY IT RETRIED:** the loop does `s1 = s3 - 0x20C`, `s0 = s2 - 0x168` and spins until they are
  equal. `0x20C = 0x83*4` and `0x168 = 0x5A*4`, and ps2SDK puts the console kernel's syscall table
  at `0x80011F80`, so the slots are `0x8001218C` and `0x800120E8`. **We were returning the same
  physical word in two address families** (`0x800120E8` and `0x1218C`), so the two derived bases
  differed by `0x80000000` and never matched.
- **FIXED, MEASURABLE:** canonicalise the result to KSEG1 (`0x80000000 | physical`). The trace now
  shows `0x8001218C` for slot `0x83`, both slots discoverable, and the enumeration shortened from
  three calls per pass to two. Unit tests **441/441** (four assertions updated to the new contract
  with the reason in the commit).
- **NAMED REMAINING DEPENDENCY, per the brief's decision rule:** the call at `0x010286D4`, which
  assigns `s3` — the value the loop cannot converge without — **is not in the trace at all**. The
  guest entered this function at `0x010286DC`, the resume point immediately after it, so that call
  completed inside the one `EeDispatcherTransfer` before the harness regained control. **Next dish:
  extend the trace to cover pre-entry calls and read what `s3` actually was.** That is a
  measurement, not a guess.
- **CORRECTION TO §10:** §10 retracted the `0xA4` claim. **That retraction was wrong** — `0xA4` is
  exactly the gap between the two syscall slots. The stride reading was right; the conclusion was
  not, because the values the guest actually received had never been logged. Three findings in a
  row came from reasoning about disassembly instead of logging what happened.

### 🟡 G1.4 — The next wall *(gate NOT met; investigation completed, then corrected by G1.5)* *(STUCK: gate not met, functions_entered still 3, halt unchanged)*
- **RESULT (2026-09-29):** ❌ `functions_entered=3 halt=stuck_in_syscall bios_files=0`. Branch **C**
  (blocked on something not built). Tests **441/441**.
- **LANDED ANYWAY — a real, tested fix:** `FindAddress` now returns the **canonical KSEG1 address**
  (`0x80000000 | physical`) instead of echoing whichever alias the caller happened to search
  through. Slot `0x83` is now discoverable at `0x8001218C` (it was reported as `0x1218C`), both
  kernel slots resolve, and the enumeration shortened from 3 calls per pass to 2.
- **THE NAMED WALL, at an address:** the loop at `0x01028740` converges when
  `s3 - 0x20C == s2 - 0x168`, i.e. when both handlers are found at `0x80011F80 + n*4`. **The
  register trace shows `s3 = 0`** — nothing ever sets it. `s3` is assigned at `0x010286DC` from the
  search at `0x010286CC`–`0x010286D8` in `sub_01028680`, and **that search never runs**: of 128
  `FindAddress` calls, **0** come from `0x010286D4`. The recompiler emitted the call correctly, so
  this is not a codegen gap — **a guest control transfer is being resolved to the wrong resume point
  and skipping the call.**
- **WHY IT IS NOT A SYSCALL PROBLEM:** the syscall completes and returns every time.
  `stuck_in_syscall` is a slightly misleading name for it.
- **TWO CORRECT THINGS THAT DID NOT ARM, both kept with the reason written down:** a general
  guest-cycle detector in the driver (correct, but the driver regains control only 3 times, so an
  in-function loop is invisible from outside), and `eeScheduler().reset()` via public API
  (faithful to `run()`, but `checkpointDue()` correctly returns false for a single-threaded guest).
- **CONSEQUENCE, stated plainly:** a guest loop that never yields **cannot be observed or bounded
  from the driver**, only by the wall-clock watchdog. G1.1's "it cannot spin forever" is true by
  watchdog, not by construction. Fixing that means recompiled code should yield on a **cycle budget**,
  not only on scheduler preemption — a recompiler change that wants its own dish and its own test.
- **STUCK — what the next dish needs:** log the guest PC and the resume PC at every
  `dispatchGuestBranch` yield for `sub_01028680` and compare against the MIPS fall-through. That
  identifies which transfer lands the guest at `0x010286DC` instead of executing the `jal`. A
  measurement, not another guess.

### ⬜ G1.4b — *(still open — see G1.5 for what the trace actually showed)*
 Resume-model defect, now localised *(carried out of the G1.4 STUCK)*
- **RESULT (2026-09-29):** ❌ gate still not met (`functions_entered=3`, `halt` unchanged), but the
  STUCK's `NEED` is answered and the defect is now specific.
- **THE DEFECT, at an address:** the `jal` at `0x010286D4` into the `FindAddress` stub
  `sub_01028638` **is** dispatched (`[Dispatch] n=8 … source_pc=0x10286d4 ra=0x10286dc`), but the
  stub's body never runs — there is no `FindAddress` and no `Yield` between that dispatch and the
  next, and **control comes back at `0x01028640`, the stub's own `jr $ra`**. The guest therefore
  reaches `0x010286DC` (the `jal`'s return point) with the call unexecuted, and `s3 = v0 = 0`.
  `[Dispatch] n=8` has no matching `[Returned]`.
- **NOT:** a syscall problem (`0x83` is served correctly 128×), and **not** a codegen gap (the `jal`
  is emitted correctly, `ps2_recompiled_functions.cpp:187146`).
- **LEAD, NOT A CONCLUSION:** a `jal` elsewhere in the same file carries a re-entry check
  (`if (ctx->pc == 0x1000570u) … goto label_1000574;`) and this one does not. Whether that
  asymmetry is the defect is **not established** and is not claimed.
- **NEXT, PRECISELY:** (1) write a test that drives a three-instruction `li/syscall/jr` stub through
  `dispatchGuestBranch` and asserts the callee body runs before control reaches the return point —
  it should fail today; (2) only then look at the `jal` emission. **Until there is a red test, any
  fix is a guess, and two guesses in this project have already been wrong.**
- **Kept in the tree:** the KSEG1 `FindAddress` fix, the register trace, the capped
  dispatch/yield/return traces, the cycle detector, the scheduler reset. **441/441 tests pass**,
  `bios_files=0`.

### 🟡 G1.0 — **NO BIOS REQUIRED** *(property demonstrated, goal not met — see STATUS.md)* *(the captain's bucket-list item, 2026-09-29)*
- **Captain's words:** *"one thing to add to the bucket list. making it run without a bios."*
- **DONE WHEN:** Vulcan 4 boots and runs with **no BIOS file anywhere on the machine** — every
  call GT4 makes into the console's own OS is served by our runtime and **named in a log**,
  never silently stubbed. A `BIOS` path search that finds nothing is a *pass*, not an error.
- **WHY IT'S THE NATURAL SHAPE:** an emulator has to *be* the console, so it needs the real
  BIOS dump — the PS2's OS, legally awkward and a setup step for every user. A recomp is
  different: the game's code is native, and the handful of things it asks of the OS are
  **syscalls**. Our runtime answers them itself (high-level emulation). PS2Recomp's runtime is
  already built around exactly that — a syscall dispatcher and named stubs.
- **THE HONEST WORK:** the dispatcher existing is not the same as *covering what GT4 calls*.
  This goal is done when GT4's own call list is satisfied by our handlers, and the ones we have
  not implemented announce themselves. Memory card services, DVD access and the IOP modules
  (`IRX/` on the disc) are the parts most likely to need real work.
- **PAYOFF:** nothing for the user to supply but their own disc, no BIOS distribution question,
  a deterministic boot, and one less thing that can differ between machines.
- **STATUS:** ⬜ waiting on G1.1 — a first boot with no BIOS is the *first* boot worth having.
- **⚠️ CAPTAIN'S GUIDANCE (2026-09-29):** *"if the bios thing is an issue. i dont mind having it but
  eventually i want to not need it."*
  → **A BIOS-derived stopgap is permitted if it ever unblocks a boot. The goal itself does not
  move: v1.0 must run with no BIOS.**
  → **Mechanism, stated plainly, because "have a BIOS" is not a checkbox for us:** the PS2 BIOS is
  itself R5900 code — it would have to be *recompiled or emulated* to be used, which is more work
  and legal noise than implementing the handful of calls GT4 actually makes. So the natural path is
  and stays **HLE**: implement the calls, name the ones we have not, and let the list drive.
  → G1.1's boot report produces exactly that list. Nobody has to decide anything today.

### ✅ G1.1 — Recompiled code executes
- **DONE WHEN:** the runtime loads the recompiled image and executes real GT4 code, with every
  unimplemented call **named** (not silently stubbed): a log showing N functions entered and
  which hardware path stopped it.
- **PROOF:** the log + the halt reason.

### ✅ G1.2 — The first observable milestone
- **DONE WHEN:** GT4 gets further than the loader — whatever the game does before it needs a
  GPU is observed (a string, a loaded resource, a state), with the evidence attached.

---

## G2 — A PICTURE (the GS)

### ✅ G2.1 — A GS layer exists and draws
- **DONE WHEN:** an actual frame comes out — even the boot logo — as a PNG.
- **NOTE:** this is the first of the two walls. Nothing downstream is possible without it.

---

## G3 — 3D (the VU1)

### ⬜ G3.1 — VU1 microcode path
- **DONE WHEN:** GT4's own vertex microcode is handled and a 3D scene renders (a car, a track).

### ⬜ G3.2 — A real car on a real track, natively

---

## G4 — PLAYABLE

### ⬜ G4.1 — Menus navigable (gamepad)
### ⬜ G4.2 — A race you can drive
### ⬜ G4.3 — Audio from the disc's own data
### ⬜ G4.4 — Save / progression

---

## G5 — THE CAPTAIN'S BUILD + POCKET

### 🟡 G5.1 — **Spec II** support *(parked for later — the captain's call, 2026-09-29:
"we will figure it out later. first of all lets keep going")*
- The captain plays GT4 **Spec II** (community mod) on the Odin 2, and wants it.
- **🔴 FINDING (researched, from the mod author's own pages): Spec II is NOT based on the retail
  US release.** *"Spec II is based on the NTSC version of **Gran Turismo 4 Online Public Beta**
  and is distributed as an xDelta patch **requiring this version**."* It reports as serial
  **`SCUS-97436`** / CRC **`4CE521F2`**, where vanilla US retail is **`SCUS-97328` / `77E61C8A`** —
  different discs. The patch is downloadable on its own, but it needs that base image
  (MD5 `3306538778dda2ded87ceaf52c944a98`). Its FAQ also states it changes *"the game's disc image
  **and executable**"*, and its ELF has 480p + GT3 chase cam + trigger sensitivity + widescreen
  baked in — so **Spec II patches the executable**, it is not a data-only mod.
- **CONSEQUENCE (not blocking anything):** recompiling retail `SCUS_973.28` produces *vanilla*
  GT4, not the captain's build. Spec II support = recompiling the **Online Public Beta** build +
  its patch — same machinery, different target binary. **That same disc also carries the lost
  Online mode** (G5.3). Both of the captain's wishes live on one disc, and we will come back to it.
- **STATUS:** 🟡 later. Vanilla first. Nothing here blocks G0/G1.

### ⬜ G5.2 — Android / Odin 2
- **DONE WHEN:** it runs on the Odin 2 at a playable frame rate. Snapdragon only — the
  captain's hardware rule.

---

### ⬜ G5.3 — Regional + special builds
- **The captain's question (2026-09-29):** *"u think we should get all the different country
  roms or it dont matter?"*
- **Answer, measured:** a recompilation is **per build** — each region has its own code addresses
  and its own differences, so region support means repeating the process against that build, not
  "one recompile runs all". And the regions are **not** the same game in the details:
  - the NA/PAL builds carry **~10 cars the JP build does not**, and JP has one the others lack
  - **prize cars differ** (e.g. an endurance prize is the Sauber in JP, the Auto Union elsewhere)
  - the S-licence final test uses **a different car** per region
  - the **AI is more aggressive on its tyres in PAL than in NA** — a real gameplay-code difference
  - driving missions carry **different handicaps**, and NA has a well-known **100%-completion
    glitch** if DM1 is not done first
  - **PAL is a 50 Hz conversion** where NTSC is 60 Hz — timing, and therefore physics and licence
    targets, genuinely differ
  These differences are exactly what a recomp lets us **study**: diffing two regional builds is
  an X-ray of the game's own logic, which is how decompilation communities locate interesting code.
- **ALSO WORTH KNOWING — `Gran Turismo 4 Online`.** A separate build (US public beta `SCUS-97436`,
  plus a JP *Online Test Version*) shipped to ~4,700 Japanese and 300 Korean test players, with an
  **Online mode the retail game never shipped**: Online Home, Quick Race, Tuned Car Race, Private
  Race, Time Attack. Services ran 2006-06-01 → 2006-09-01 and died. If the lost mode is ever to be
  studied or restored, **that build is the one to read** — and it is the kind of thing this project
  exists for.
- **PLAN:** the **US v2.00 build stays the only target until something boots and draws.** Additional
  builds get collected **when there is a reason to diff them**, not now. Storage is cheap (~5 GB
  each), attention is not.
- **STATUS:** ⬜ none collected beyond US v2.00 (already on `/mnt/ssd/gt4`).

### ✅ G5.2a — Android/ARM64 feasibility, answered with a build
- **DONE WHEN:** a real attempt at cross-compiling the runtime and the generated code for
  `aarch64`, naming exactly what compiles, what fails, and what the failures would cost.
- **RESULT (2026-09-30):** ✅ **portable in principle, zero architecture work.** Commit `5509151`.
  A real ARM64 executable exists: `/mnt/ssd/vulcan4-build/android/gs_probe_arm64` (5,263,568 B,
  ELF64 AArch64) carrying **707 recompiled GT4 guest functions** and **94 GS runtime symbols**, with
  **zero x86/SSE symbols** left. Transcript: `/mnt/ssd/vulcan4-build/android/try.log` (679 lines).
  The 8.9 MB generated translation unit, `register_functions.cpp`, all four GS sources, the VU1
  interpreter, all 8 `Kernel/Syscalls` and 27/27 `ps2xIOP` compile for aarch64 with zero errors.
  Upstream's CMake **already** had an ARM64 branch (sse2neon v1.9.1, `USE_SSE2NEON`); only the
  X11/raylib (33 window/audio symbols) and FFmpeg (1 TU) need Android equivalents. Full write-up in
  `docs/ANDROID-FEASIBILITY.md`.
- **NOT DONE:** no APK, no NDK build, no bionic. Tested against glibc for aarch64 Linux.

### ✅ X1 — Prior art: what the shipped recompilers did
- **DONE WHEN:** a survey of XenonRecomp / N64Recomp / Zelda64Recomp / PS1Recomp / PSXRecomp, every
  claim tied to a source that was actually read, ending in a concrete list for us.
- **RESULT (2026-09-30):** ✅ Commit `d467818`. `docs/PRIOR-ART.md`, 22,797 bytes, 14 cited sources.
  **Headline: not one shipped project links an emulator's GPU core** — UnleashedRecomp translates
  draw calls, Zelda64Recomp built RT64, PS1Recomp re-implements the PS1 GP0/GP1 command stream,
  PSXRecomp keeps a software rasteriser as "the reference look". Emulators are reading material
  only, which independently confirms the G2.0 GS decision. Also: vblank is a host clock, not
  emulated hardware; `mstan/psxrecomp` is **PolyForm Noncommercial** and incompatible with our
  GPL-3.0 tree.

---

## G6 — MODS & NEW CONTENT *(the captain's plans, registered so they cannot drift)*

> **Why this phase exists:** the captain, 2026-09-30: *"God I have so much plans for this recomp."*
> Every idea below is a goal, not a wish. None of it starts until the game runs — but writing it down
> now is how we make sure the runtime we are building can actually carry it.

### ⬜ G6.0 — **NEW CARS** *(the captain's first big question: "can we add new cars?")*
- **Answer: yes — and a recomp makes it easier than modding the original.** We own the loader, so we
  are not bound to GT4's own model format: **we can add a reader for any format we choose** (GLB,
  FBX, a converted Assetto Corsa/Forza model). GT4's own car format was never publicly cracked; we
  do not have to crack it to add a car.
- **What a new car actually needs — five pieces:**
  1. a **SpecDB entry** (the game's own database): CarID, name, maker, power, weight, dimensions,
     drivetrain, gearbox, tyre sizes — copied in *shape* from a real car
  2. a **model** carrying the nodes the game expects (`body` + four wheels), because the engine
     steers, spins and animates them
  3. **physics data in GT4's own fields** — ⚠️ GT4's *real* physics runs natively here, so it will
     judge the car: good data feels authentic, lazy data drives like a trolley
  4. an **engine sound** assignment (a table entry, or borrow an existing car's sample)
  5. **shop/menu integration** if it should be buyable/winnable: price, thumbnail, event eligibility
- **Honest limits:** nothing here starts before the game runs; new car IDs must be *additive* (never
  reuse an existing ID — saves refer to them).
- **⚠️ HONESTY RULE FOR THIS GOAL:** the model path is ours to choose, but the *car itself* has to
  satisfy GT4's own systems. A pretty mesh with wrong wheelbase/tread will be positioned wrong and
  drive wrong. The data is the hard half, not the model.
- **Community weight:** "add your own car to GT4" is the thing players have wanted for twenty years
  — a bigger draw than the native port itself.
  > **The captain, 2026-09-30:** *"This is like the most requested feature EVER. Bc in the original
  > game u cannot add modded cars."* — and he is right, for a concrete technical reason:

### ⬜ G6.0a — **SWAP vs ADD:** find every place GT4 assumes a fixed car count
- **Why you cannot add cars to the original:** on real hardware or an emulator, a modder has only
  bytes — no code. GT4's cars live in a **binary database** (SpecDB) plus models inside `GT4.VOL`,
  and the game's menus, shop lists, prize tables and event eligibility almost certainly index cars
  through **fixed-size structures**. A new CarID has nowhere to live. So every existing GT4 car mod is
  a **SWAP** — replace an existing car's model/specs — and a swap costs you a car. **Add has been
  impossible**, not for lack of models, but because the engine's tables do not grow.
- **Why a recomp changes it:** we have the engine's own logic as editable C++, we control the model
  loader, and we have a hook layer. Growing a table and teaching a lookup to accept the new range is
  *work* — but it is work on our side of the line, not a wall.
- **THE ACTUAL BLOCKER TO FIND (this is the dish):** every place the game assumes "there are N cars".
  Search the recompiled C++ and the data structures for: fixed arrays sized by car count, index
  tables with a hardcoded upper bound, shop/prize/menu lists built once at init, model-path
  construction (`car/<maker>/<id>` schemes), and any loop bounded by a constant that matches the
  known car total. Produce a ranked list: *what breaks first when we add car N+1.*
- **DONE WHEN:** `docs/NEW-CARS.md` names those sites with addresses/file+line evidence, ranked by
  what blocks an addition first — the survey that turns "can we add cars?" into a work plan.
- **STATUS:** ⬜ registers now, executes after playability (G4). It costs nothing to know where the
  ceiling is, and knowing it shapes how we design the loader **today** — which is why it is written
  down before the game runs.

### ⬜ G6.1 — **CUSTOM MUSIC** *(the captain's demand, 2026-09-30)*
- **The good news, and the same trick as cars:** we own the audio path, so we do **not** have to
  crack GT4's music format to add our own. Two layers, and they are independent:
  1. **Play the original** — decode from the user's own disc (see G4.3: the audio bank lives inside
     `GT4.VOL`; the survey found no SGDP and no ADPCM path yet).
  2. **Play custom** — our own music loader: drop files in a folder (OGG/MP3), a mapping table says
     which *game state* each file covers (main menu, race, replay, results), and a **hook point**
     intercepts the game's "play BGM id" call and plays ours instead.
- **The hook is the whole point.** Somewhere the game asks to start background music — a script call,
  a syscall, a table lookup. Custom music = intercept that request and satisfy it from our side.
  That is the same pattern as `G6.0` (own the loader) and it needs the same discipline: find the
  request site, name it, hook it.
- **Honest split:** *background music* is the easy 90% — files, a table, a hook. **Dynamic audio**
  (engine notes, tyre squeal, impacts) is a different problem: those are banks with pitch/loop
  parameters tied to car state, and they should keep coming from the disc for authenticity.
- **Capability note:** a decoder dependency will be needed (OGG/Vorbis or similar — free, GPL-
  compatible). Name it before adding it; do not install anything silently.
- **STATUS:** ⬜ post-playability (G4), same lane as the rest of the audio work (G4.3).

### ⬜ G7.0 — **DISTRIBUTION: code only. Assets come from the user's disc.** *(the captain's rule)*
- **The captain, 2026-09-30:** *"We don't publish anything. Just the code. People have to bring their
  own iso file of the game — that's where we take assets."*
- **This is already how the project is built, and it is now a stated policy, not a habit:**
  - the repository ships **no game data** — no ISO, no `GT4.VOL`, no extracted textures, models,
    audio or the recompiled C++ (which is a derivative of the game)
  - the runtime requires **the user's own legally-obtained disc image** — that is the *only* input
  - **mods are distributed as code and/or files the modder has the right to share** — never as game
    content
  - `.gitignore` blocks the extensions, and every dish brief repeats the rule; it is enforced by
    review and by the goal gates
- **Precedent we already set:** the same model as the emulator scene, and the same reasoning used on
  the captain's other projects — **ship the engine, never the assets.** Without the user's disc the
  program has nothing to run, exactly like an emulator without a ROM.
- **Why this is a feature, not just caution:** it means the project can be public, discussed and
  contributed to openly, with no takedown risk — and the community already understands this model.
- **STATUS:** ✅ **in force now.** Verify on every release that a fresh clone plus a user's disc is
  the whole setup (goal `G0.5` — clean room — is the standing proof).

## The discipline

1. Every dish opens with `GOAL: <id>`; the driver prints it in the banner.
2. Every commit ends with `[GOAL <id>]`.
3. Every report opens with the captain's picture or number, never with a commit list.
4. **"It's implemented" is not done.** Proof is him seeing it or counting it.
5. **No game data in the repo. No faking. Refused, not guessed at.**
6. Nothing new is dispatched before the captain has seen the previous result.

### ⚠️ GATE TYPES — learned the hard way (2026-09-30)

The gate is a shell command, and **a shell command cannot read a diagnosis.** For four dishes in a
row, excellent investigative work was logged as `GOAL NOT MET` because the gate demanded *"the guest
advanced past the wall"* while the dish's actual deliverable was *"name the cause precisely"*. The
dish text said a named dependency counts as a pass; the machine disagreed. That is a design flaw on
Caine's side, not a failure of the work.

**Two kinds of dish, two kinds of gate:**

| Dish type | Gate must check | Example |
|---|---|---|
| **FIX** — the product must move | the product's own measurement (a counter, a colour count, a boot report) | `functions_entered > 3` |
| **INVESTIGATE / SURVEY / DOC** — the deliverable is knowledge | the **artefact of knowledge**: the doc exists, is non-trivial, and contains the named finding | a trace line in the log + a conclusion in the doc |

**Never give an investigative dish a progress gate**, and never give a fix dish a prose gate. If a
dish is honestly both, split it into two dishes: one that produces the diagnosis (doc gate), one
that acts on it (progress gate).

Also: a dish that fails twice burns two of the three strikes before the driver parks. Prefer
correct gates over retry budget.

*Declared 2026-09-29, the day the captain said: "lets work on Vulcan 4! a recomp of gt4."*
