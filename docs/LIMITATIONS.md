# VULCAN 4 — LIMITATIONS

What is not done, named rather than glossed over. A frame we drew honestly beats a screenshot
faked politely. The GS probe echoes these as `VULCAN4 LIMITATION:` lines at runtime.

**Last updated:** G2.0 (GS foundation).

## The GS — blocking defects found in `ps2xRuntime`

All four are pre-existing defects in the runtime's own GS, not in the probe. The probe works
around 1, 2 and 3; defect 4 is unsolved.

1. **GIFTAG register-address field is 4 bits.** `gs_frontend.cpp:696` computes
   `regs[i] = (tagHi >> (i*4)) & 0xF`, and the PACKED handler only implements cases
   `0x00`–`0x0F`. Registers above `0x0F` are unreachable from any GIF packet: `FRAME_1` (0x4C),
   `ZBUF_1` (0x4E), `SCISSOR_1`/`_2` (0x40/0x41), `FINISH` (0x61). Real hardware has a
   documented escape for this range; this runtime does not implement it.
   **Consequence: a guest cannot program the drawing framebuffer through the GIF path at all.**
   The probe uses the public `GS::writeRegister` for these instead.
   *Fix: implement the 0xF escape, or widen the address field.*

2. **The presentation path decodes DISPFB/DISPLAY in a private layout, not the hardware one.**
   `DISPFB`: base page 0–8, width 9–14, format 15–19, read origin X 32–42, Y 43–53.
   `DISPLAY`: magnification 23–26, width−1 32–43, height−1 44–54. A guest writing genuine
   hardware values is silently misread. *Fix: decode the hardware layout.*

3. **`Present` returns an empty frame with no error** when `PMODE` bit 0 is clear, and when
   `GSRegisters*` is null `buildPresentationRequestUnlocked()` bails out early. Both look
   identical to the caller: a blank frame, no message.
   *Fix: report a distinct status, or at minimum log why.*

4. **The triangle rasteriser produces no pixels.** CORRECTED IN G2.1: the earlier claim that "the
   triangle never submits" was **wrong**. `XYZF2` and `XYZ2` both queue a vertex and both call
   `vertexKick`, so writing `RGBAQ,ST,UV,XYZF2,XYZ2` produces six kicks for three vertices and
   `GSCpuBackend::Submit` **is** called. All the framebuffer state the rasteriser logs
   (`fbp=0 fbw=8 psm=0x0`, `scissor=(0,0)-(511,511)`) is correct. The rasteriser runs and writes
   nothing. **Still undiagnosed.** This is now the single biggest gap in the GS: a guest that draws
   primitives rather than uploading pixels cannot be rendered until it is fixed. G2.1 worked around
   it by using the transfer path, and says so.

## The GS — not implemented

- **No texture sampling is exercised.** The probe draws one untextured triangle into a PSMCT32
  framebuffer. Filtering, CLUTs, and the PSMT8/PSMT4 paths have code but no coverage from the
  guest's point of view.
- **No VU1.** The GS path takes vertex data directly; nothing computes geometry. That is G3.1.
- **The EE→GS GIF DMA is not exercised.** The probe hands packets to `GS::processGIFPacket`
  directly. `GifArbiter` and the DMAC are unproven.
- **No vblank, no CSR signalling, no FINISH-driven present event.** Frames are latched on
  demand. Defensible for a recompiler, which wants pixels rather than a timed display, but it
  means interrupt plumbing is unproven.
- **Z-buffering is allocated but untested.** `ZBUF_1` is set; nothing writes or tests depth.
- **`GS::init` with `privRegs == nullptr` yields a permanently blank presentation path.**

## The guest — where it actually stops (goal G1.5)

- **The G1.4 premise was wrong, and the evidence is in `boot.log`.** The `jal` at `0x010286D4`
  dispatches the stub at `0x01028638` correctly and **its body runs**. Evidence: all 13 dispatches
  in the trace carry `entry_pc=0x1028638`, and `PS2Runtime::dispatchGuestBranch` sets
  `ctx->pc = targetPc` as its *first statement* (`ps2_runtime.cpp:1352`), so a callee reached that
  way can never observe a resume address. `sce_FindAddress` was invoked **155 times** — the body
  demonstrably executed every time. The `pc=0x01028640` on re-entry is the resume case
  (`case 0x1028640u: goto label_1028640`) working exactly as designed, not a skipped body.
- **The entry-vs-resume mechanism is correct, and it is correct by construction.** A static scan of
  all 707 generated functions found **61** whose own entry address is also a resume case. All 61
  are safe: the label for the entry address is emitted at the *top of the body*, immediately after
  `default: break`, so the fresh-call and resume paths converge without skipping an instruction.
  If codegen ever moves that label, all 61 silently lose their first instruction. Locked in by a
  regression test.
- **The real wall is a livelock on `sce_FindAddress` (0x83), not a dispatch bug.** The guest calls
  it 143–155 times; each call brute-force scans ~112,000 words of RDRAM
  (`computeBuiltinFindAddressResult`, `Kernel/Syscalls/System.cpp:639`) looking for a function
  pointer such as `0x10285c0`, finds nothing, and returns `0`. The guest retries. The scan is a
  heuristic substitute for the real hardware algorithm, which resolves a function from the kernel's
  loaded-module export table.
- **Nothing has ever populated that table.** `total_mmio_accesses=0` and `distinct_mmio_addresses=0`:
  the guest has not touched a single hardware register. No IRX module has been loaded, and the
  runtime has no BIOS path at all (`bios_files=0`, and the harness contains no BIOS loading code).
  So the pointer the guest is hunting for does not exist anywhere in memory, and no amount of
  dispatch correctness will make the lookup succeed.
- **A precise halt name now replaces the generic one.** The report says
  `halt=livelocked_in_syscall`, with the detail naming syscall `0x83 (FindAddress)`, its share of
  the call tally, and the guest PC. Previously `stuck_in_syscall` said only "a syscall", which is
  what sent G1.4 hunting for a bug in the wrong place.

## What every one of these means for a player

The list above is written for engineers. This is the same set translated for the person who would
run the thing. **None of these are fixed.** None are hidden either — that is the point of the file.

| Limitation | What a player would experience |
|---|---|
| GS rasteriser produces no pixels | **No 3D, ever, until this is fixed.** The car's shape, the track, the sky — all of it goes through the triangle rasteriser. This is the blocker. |
| VU1 never driven by a guest | **No vertex maths**, so no 3D even if the rasteriser worked. Independent second wall. |
| Guest reaches 3 functions, then livelocks | **Nothing past the opening.** No logo animation, no menu, no race. The game does not start. |
| `total_mmio_accesses=0` | The guest has never spoken to the console's hardware. Nothing it asks for has been answered by anything real. |
| No IRX modules loaded, no BIOS | The kernel cannot resolve functions, because the table it searches was never populated. The direct cause of the livelock. |
| GS image transfer not proven through GIF packets | The mechanism works; the route a real guest uses does not. Would only matter once a guest is drawing. |
| Vblank is a host clock, no interrupts modelled | A guest waiting on vblank would be released by a host thread, not by the console. **Timing would be wrong** — races would not behave like the original. |
| No texture sampling, CLUT, blending, alpha, Z-test | No textures, no transparency, no depth sorting. Even with a working rasteriser, the picture would be flat and wrong. |
| GS debug history paused by default | Not a player-facing limit. It cost one wrong register count in a commit; recorded because the mistake is instructive. |
| No CD/DVD or disc I/O from a guest | **No loading screens, no disc audio, no CD streaming.** |
| No ADPCM decode | No music, no engine noise, no effects. |
| No real controller input | Not playable even if it rendered. |
| No menus, saves, or race logic | No game. |
| No NDK/bionic build, no APK | Runs on desktop x86-64 only. **The AYN Odin 2 is not a target yet**, despite the ARM64 build working. |
| No regional/special build support (G5.1, G5.3) | Retail US v2.00 only. **Spec II, which the captain plays, is not supported** — it is not based on the retail disc. |

## Project-level

- **Nothing from Gran Turismo 4 has ever been rendered by this GS.** The guest has not reached
  it: the last boot recorded `total_mmio_accesses=0`. Every GS claim so far is about our own
  code driving our own code.
- **The GS cannot be recompiled.** It is fixed-function hardware, so a renderer must be supplied.
  This is the project's first real wall.
- **The GS computes and moves real pixels, but does not yet rasterise.** As of G2.1 the frame
  `/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png` (512x512, 119,302 B) carries **43,804 distinct
  colours**, computed by our own code, written into VRAM through the GS **transfer** path
  (`BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR` + image data), read back through the GS **presentation**
  path and encoded by our own PNG writer. In-count and out-count match exactly. What is missing is
  the **rasteriser**: no primitive of any kind is drawn. Nothing from Gran Turismo 4 has been
  rendered, and the guest still has `total_mmio_accesses=0`.
- **No texture has been sampled into the framebuffer yet (G2.3).** Swizzle is **proven** by a
  round-trip test (permutation, write-then-read, and not-the-identity) for PSMT8, and PSMT4 /
  PSMCT16 / PSMT16 have swizzle maps present but **untested**. CLUT upload and the CLUT cache
  exist (`m_clut`, `LoadClut`, ninth address bit via `CSA[4]` for 16-bit CLUTs) but **no indexed
  texture has been drawn**. Not modelled: filtering beyond the mode field, **mipmaps and LOD**,
  **CLUT animation**, anisotropic/bilinear edge behaviour, and swizzle for formats outside the
  four with headers.
- **G2.2 reframed by G2.3: the fault is in our probe, not the runtime.** The suite case at
  `ps2_gs_tests.cpp:829-849` submits primitives with `GS::writeRegister` and **asserts correct
  rasterised pixels**, and it passes with all three vertices populated. The runtime's rasteriser
  and vertex queue are therefore working. G2.2's failure is in the skeleton's GIF REGLIST
  submission path. The next experiment is one line: submit the same primitives via `writeRegister`
  and see if they rasterise.
- **The GS rasteriser has still never written a pixel from a primitive (G2.2).** The draw sequence
  is issued correctly and the GS acknowledges it, but the batches reaching the rasteriser are
  degenerate (`v0 == v2 == (0,0)`, zero edge denominator) and the triangle is correctly skipped.
  The rasteriser's own coverage, scissor-clip and gouraud code looks correct on the data it is
  given, so the fault is in the primitive stream decoding or the vertex queue. Until this is fixed
  **no guest geometry can be rendered**, and no amount of background work changes that.
- **G2.2's own gate is satisfied by the background, not by geometry.** The check needs 1,000+
  distinct colours and the G2.1 transfer gradient supplies 43,804, so the frame passes a
  geometric-structure test while containing no drawn shape. A future gate for "the GS draws" should
  compare against a *known-empty* background, or assert on a structure a gradient cannot produce
  (e.g. a flat-shaded region with hard edges, or a count of distinct colours in a specific band).
- **The GS image transfer has not been proven through a GIF packet.** G2.1 fills the framebuffer
  via the native entry point `GS::uploadImageNative`, which writes `BITBLTBUF`/`TRXPOS`/`TRXREG`/
  `TRXDIR` and feeds the image data straight to the backend. The probe reports `gif_packets=0`. A
  real guest sends a GIF packet containing an `IMAGE` transfer, and **G2.0's hard blocker makes
  that impossible today** — the GIFTAG register field is 4 bits, so the destination registers a
  GIF transfer needs cannot be named. So the transfer *mechanism* is proven and the GIF *route* to
  it is not.
- **The vblank/CSR sync primitive is host-driven and says so.** The skeleton advances a monotonic
  vblank tick counter from the host and raises CSR bit 0 (SIGNAL) after FINISH. It **does not**
  model vblank timing, deliver a vblank interrupt to the EE, provide DMA/AD interrupts, or emulate
  the interrupt controller or the IOP. A guest waiting on vblank is released rather than spinning —
  deliberately, because G1.5's `FindAddress` livelock is exactly what a guest does when a promised
  return value never arrives. But it is released by a host thread's wall clock, not by the GS.
- **The recompiled guest is still stuck in a syscall** after 3 functions. See
  [`docs/FIRST-BOOT.md`](FIRST-BOOT.md) and the G1.4 notes: the `jal` at `0x010286D4` dispatches
  the stub at `0x01028638` but the callee body never runs before control returns at
  `0x01028640`. Unresolved.
- **Licence note for the future:** PCSX2's GS is **GPL-3.0-or-later**, not LGPL-3.0 as the G2.0
  brief assumed. Compatible with us, so copying is permitted, but it would oblige us to carry
  notices, state the change, and offer corresponding source. See [`docs/GS-PLAN.md`](GS-PLAN.md).

## GS limitations, as of G2.4 (measured)

G2.4 fixed our own probe and drew a **sampled** texture through the register path
(37,275 distinct colours, `docs/GS-PLAN.md` §12). That proves the happy path. These are what is
still absent or wrong. Items marked **RUNTIME** are divergences from hardware in `ps2xRuntime`
itself, not gaps in our probe — they will bite the real guest, not us.

### Absent

- **Filtering.** Only nearest sampling is exercised (`TFX = 0`). The bilinear path in
  `SampleTexture` exists but **has never been driven by a test or a frame**, and its edge behaviour
  on `tw`/`th` boundaries is unverified.
- **Mipmaps and LOD.** No mip chain, no `TEX2` LOD selection, no trilinear/anisotropic anything.
  `TEX2_1`/`TEX2_2` write `TPSM`/`CBP` but nothing selects a level.
- **Blending.** `PRIM.ABE` and the `ALPHA_1` register are implemented in `WritePixel` but **never
  set by our probe and never asserted by the suite**. Fixed-point blending (`FBA_1`) likewise.
- **Z-buffer / depth test.** The Z test *rejects* correctly (G2.4 found and fixed the `TEST_1 = 0`
  → `ZTEST = NEVER` case), but **no geometry has ever actually been depth-sorted**: our triangles
  are submitted in back-to-front order by hand, so the Z buffer is never exercised as a *test*.
- **CLUT animation / CSM.** `CSM` and `CSA` are decoded and the CLUT cache has a PSMCT16 path, but
  no test covers the 512-entry suffix layout, so 16-bit CLUT index masking beyond 256 entries is
  unverified.
- **Rasteriser coverage.** One pixel centre per pixel, barycentric, no MSAA, no polygon offset, no
  coverage-based fill-rule handling beyond the top-left-style epsilon. Triangles only: `DrawLine`
  and `DrawSprite` are implemented but unverified by this dish's frame.

### RUNTIME divergences from hardware (will bite the guest)

- **ZTE is not honoured.** `GSCpuBackend::WritePixel` computes
  `ztestMethod = (TEST >> 17) & 3` and applies it **unconditionally**. Hardware gates the Z test on
  `ZTE` (bit 16), so a guest writing `TEST = 0` (ZTE clear) gets normal drawing on hardware and
  **every pixel discarded** here. Measured in G2.4: the probe's own `TEST_1 = 0` drew nothing.
- **`PRMODECONT`/`PRMODE` semantics will flatten a guest's geometry.** With `AC = 0`,
  `gs_frontend.cpp`'s `case GS_REG_PRMODECONT` takes `tme`/`iip`/`abe` from the **`PRMODE`**
  register and only `type` from `PRIM`. A guest that writes `PRIM` alone — without also setting
  `PRMODECONT` or `PRMODE` — gets **flat, untextured geometry with no error**. Real hardware applies
  `PRIM` directly when `AC = 0`. Measured in G2.4: our textured quads rendered flat white.
- **Texture base units are 32× off for a real guest.** Real hardware defines a texel page as
  256 KiB = 1024 blocks of 256 B, so a guest writes `TBP0 = page * 1024`. The runtime's only page
  helper is `framePageBaseToBlock(fbp) = fbp << 5` — an 8 KiB page — and it is applied to the
  **framebuffer** but not to `TEX0.TBP0` or `TEX0.CBP`, which are used as raw block indices. A guest
  texture base would be read **32× too low**. Our probe sidesteps it by keeping upload and sample in
  the same (wrong) unit, which is internally consistent and externally wrong.
- **Presentation decodes `DISPFB`/`DISPLAY` in a private layout** (G2.1 finding, still open): a
  guest writing genuine hardware `DISPLAY` values is misread.
- **`Present` returns an empty frame with no error** when `PMODE` bit 0 is clear.
- **The GIFTAG register address field is 4 bits** and PACKED implements only `0x00`–`0x0F`, so
  `FRAME_1`, `ZBUF_1`, `SCISSOR`, `TEXCLUT` and `FINISH` are unreachable from a GIF packet. Our
  primitive path is fully reachable (`PRIM` 0x00 … `TEX0_1` 0x06 all fit); the framebuffer and CLUT
  registers are not. Unchanged since G2.0 and still the hard blocker for a guest.

### Ours, and fixed, recorded so it is not re-introduced

- **`PRIM = 2` used to mean "triangle" in our probe.** It is `GS_PRIM_LINESTRIP`; `GS_PRIM_TRIANGLE`
  is 3. This cost G2.2 and G2.3 two dishes and produced a set of symptoms that all pointed at a
  broken rasteriser. Pinned by
  `tools/patches/ps2recomp-linux-g24-primenum.patch`.

### Open, and deliberately not closed

- **A `writeRegister`-submitted triangle still does not land in a unit test.** Correct vertices,
  open scissor, `TEST_1 = 0x30000`, and the interior pixel stays `0`. Not the primitive type and
  not the vertex queue: `tools/patches/ps2recomp-linux-g24-primenum.patch` proves from the GS's own
  debug history that a `GS_PRIM_TRIANGLE` batch carries all **three** vertices, so both of those are
  demonstrably healthy. The probe draws correctly through the GIF path, so the pipeline is proven,
  but this loose end should be closed before `writeRegister` is relied on as a fallback. Detail and
  the failed attempts in `docs/GS-PLAN.md` §12.7.
- **EE→GS GIF DMA is unproven.** `GifArbiter` has never been exercised; packets are handed to
  `GS::processGIFPacket` directly.
- **No vblank or CSR signalling.** `CSR`/`VIF` interrupt plumbing is untouched; the frame is latched
  on demand, which is correct for a recompiler but means the timing model is absent.
- **Nothing from Gran Turismo 4 has ever been rendered by this GS.** The guest has not reached it.

---

## GS limitations, as of G2.5 (additions — measured by `tools/gs/vulcan4_gs_triangle.cpp`)

These are appended, not merged: nothing above this line was rewritten.

### New RUNTIME divergences from hardware

- **`ZBUF` is decoded with a different bit layout than `FRAME`, and the page is 8 KiB, so a depth
  buffer placed at "page 1" lands inside the framebuffer.** `gs_frontend.cpp`, `case GS_REG_ZBUF_1`:
  `zbp = value & 0x1FF` (bits 0–8), `psm = ((value >> 24) & 0xF) | 0x30`, `zmask = (value >> 32) & 1`
  — whereas `FRAME_1` puts its base page at bits 4–12 and its PSM at bits 0–2. The backend then
  addresses both with `framePageBaseToBlock(x) = x << 5`, an **8 KiB** page, while a 512×512 PSMCT32
  framebuffer is 1 MiB = **128** such pages. **Measured symptom:** a draw with `ZBUF` at base 1
  produced 19,764 extra pixels in `RGB(240,255,63)` — the depth buffer, visible in the picture,
  offset from the triangle — and a bounding box reaching `y = 383` when no vertex is below `y = 320`.
  Any area-only check reads that as an unexplained failure; what identifies it is counting distinct
  colours and looking at the bounding box. Real hardware's page is 256 KiB, so a guest's ZBUF base
  would be read **32× too low** here — the same defect G2.4 recorded for texture bases, now on the
  framebuffer side. *Not fixed: it is a runtime change.*
  **Work-around used by the oracle:** `ZBUF` base `zbp = 128` (block 4096 = byte 1 MiB), clear of the
  framebuffer. Note the G2.4 probe still encodes `ZBUF_1` the `FRAME` way at base 1 and therefore
  carries this shape in its frame; it was left alone so its recorded checksum stays honest.

- **`TFX = 0` (MODULATE) multiplies and saturates, so a sampled texture's colours are not the
  texture's colours.** `combineTexture` computes `(texel * vertex) >> 7`; with a white vertex colour
  that is `texel × 2` clamped, so every texel above 127 flattens to 255 and an indexed 2-colour
  texture returns as a tinted gradient. **Measured consequence:** the G2.4 probe's interior pixel
  values do not match the texture it uploaded, and no colour-level check of sampling is possible with
  `TFX = 0`. `TFX = 1` (DECAL) returns the texel unchanged and is what the oracle uses.

### Now proven (was "not done" above)

- **Triangle rasterisation from a guest-shaped primitive stream.** `docs/GS-TRIANGLE.md`: one flat
  triangle, vertices `(128,64) (448,96) (192,320)`, analytic area **39936**, measured **39936**
  (0.000 % error), bounding box `[128,446]×[64,319]` exactly as predicted, 1 distinct non-black
  colour, 0 unexpected colours. Submitted as **one GIF REGLIST packet** (`PRIM` 0x00 … `XYZ2` 0x05),
  read back through the GS's own presentation path, encoded by the oracle's own PNG writer.
  **This closes the §12.7 loose end:** a correctly-programmed triangle does land. The G2.4 probe's
  failure to land one was the Z buffer above, not the primitive type, the vertex queue or the
  rasteriser.
- **UV interpolation + nearest sampling + PSMCT32 swizzle, checked by count.** A 64×64 PSMCT32
  texture split at texel column 32, bound with `TEX0_1`, sampled with S affine in screen x: orange
  **27456** pixels in columns ≤ 287 and blue **12480** in columns ≥ 288, against predicted
  **27456 / 12480**, with no mixed column anywhere and no unexpected colour.

### Still absent

Unchanged from the sections above: no VU1, no EE→GS DMA through `GifArbiter`, no indexed texture with
a CLUT drawn by any test, no blending, no dithering, no mips/LOD, no bilinear edge behaviour, no
vblank or CSR interrupt plumbing, and **nothing from Gran Turismo 4 has ever been rendered** — the
guest still has `total_mmio_accesses=0`.
