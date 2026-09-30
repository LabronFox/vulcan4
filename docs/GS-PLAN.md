# GS-PLAN — how VULCAN 4 will implement the Graphics Synthesizer

Status: decision made (G2.0). Skeleton exists at `tools/gs/vulcan4_gs_probe.cpp`.
Frame: `/mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png`

## The decision, in one line

**Keep and extend the GS already living in `ps2xRuntime`. Do not import PCSX2's GS.
Do not start a second one.** Fix the defects listed in [What is not done](#what-is-not-done),
because they are cheaper to fix than to route around.

## Why this is the project's real wall

The GS is fixed-function hardware. It cannot be recompiled from a static translation of the
game, so a renderer has to be *supplied*. Every improvement the project wants — 4K, higher
framerates, wheels, better cars — passes through it. It is the first of the two walls named in
the README.

The guest has not reached it yet (`total_mmio_accesses=0` at the last boot), so this decision is
being made deliberately rather than at 3am during a panic.

## Licence, checked before anything else

The task brief described PCSX2's GS as `LGPL-3.0`. **That is wrong, and it matters.**

PCSX2 is now **GPL-3.0-or-later**:

- <https://github.com/PCSX2/pcsx2/blob/master/COPYING.GPLv3>
- Source files carry `SPDX-License-Identifier: GPL-3.0-or-later`.

Consequences, stated plainly:

- LGPL would have been the *easy* answer: linking a dynamic library would have imposed almost
  nothing on us. GPL-3.0 is not that. Copying or adapting PCSX2's GS would make the combined work
  GPL-3.0.
- We are already GPL-3.0 (`ps2xRuntime` is GPL-3.0), so GPL-3.0 is **compatible**. Copying would
  not force a licence change. It *would* force us to: carry the copyright notices, state the
  change, and offer corresponding source.
- Because it is compatible, there is no licence reason we *cannot* copy. The reason not to is
  technical, and it is the deciding factor below. Licence compatibility is a permission, not an
  argument.

## The three options

### 1. Reuse an existing GS implementation (PCSX2's GS)

- What it is: a cycle-accurate GS core plus a set of renderer backends, driven by
  `GSRenderer`, sitting behind PCSX2's own memory map, DMA and timing assumptions.
- Licence: GPL-3.0-or-later, compatible with us.
- **Cost:** it is not a library. It is coupled to PCSX2's memory map, its DMA path, its timing
  model and its thread model. Extracting it means reproducing or reimplementing all of that.
  It is also ~100k lines of code we would have to keep building, whose build system we do not
  control.
- **Risk:** highest. We would be reverse-engineering PCSX2's internals rather than the PS2's
  hardware, and debugging would mean reading code we do not understand well.
- **Verdict: rejected.**

### 2. Write a minimal GS core ourselves

- What it is: a new GS from the hardware docs — register interface, VRAM transfer path,
  enough rasterisation for PS2 primitives.
- **Cost:** a GS is a large piece of work. Rasterisation rules, the pixel storage modes
  (PSMCT32/24/16, PSMT8, PSMT4, Z buffers), GIF packet decode, the transfer/DMA path, and the
  display/vblank handshake are all real work.
- **Risk:** high, and worse — it would be a *second* GS.
- **Verdict: rejected, because we already have one.**

### 3. Hybrid — our own register/present layer, borrowing algorithms, not code

- **This is what is actually in place**, with one important addition: the "our own" part was
  already written before this goal was set, and it is substantial.
- What already exists in `ps2xRuntime` (about 4,200 lines):

  | File | Role |
  |---|---|
  | `src/lib/gs/gs_frontend.cpp` | `GS` class, GIF packet decode, register map, draw state |
  | `src/lib/gs/gs_cpu_backend.cpp` | software rasteriser **and** the presentation readback |
  | `src/lib/gs/ps2_gs_memory.cpp` | pixel storage modes, VRAM addressing, transfers |
  | `src/lib/gs/ps2_gif_arbiter.cpp` | the EE→GS GIF/DMA arbiter |
  | `include/runtime/gs/gs_backend.h` | the `GSRasterBackend` interface |

  This covers the register interface, the transfer path, and software rasterisation with all
  the PSM formats. That is most of what option 2 would have had to build.
- **Verdict: recommended, and already the de facto state.**

## Why we did not import PCSX2, concretely

The GS we have is already the right *shape* for a recompiler:

- Its input is a **byte stream of GIF packets**, exactly what the recompiled EE's DMA produces.
  PCSX2's GS is entered through PCSX2's memory map instead.
- Its output is **VRAM plus a host RGBA readback**, which is what a static recompiler needs,
  because there is no hardware display to present to.
- It is already in our tree, in our build, under our licence, and driven by our own test suite.

Importing a second GS would mean owning two renderers and translating between them. That is the
worst of both options.

## External evidence (added by goal X1)

[`docs/PRIOR-ART.md`](PRIOR-ART.md) surveyed the projects that shipped first. The survey supports
this decision independently: **not one of them links an emulator's GPU core into their recompiler.**
UnleashedRecomp translates draw calls to a modern API, Zelda64Recomp built a dedicated accuracy-first
core (RT64), PS1Recomp re-implements the PS1's GP0/GP1 command stream and feeds OpenGL, and PSXRecomp
keeps a software rasteriser as "the reference look". Emulators — Xenia, PCSX2, Ares — are used as
*reading material* only. PS1Recomp's design doc also states outright that its structure follows
PS2Recomp's, so we are the upstream in that lineage rather than an outlier.

**Recommendation unchanged: keep and extend the GS in `ps2xRuntime`.** See `PRIOR-ART.md` item 3 for
why the four known GS defects should be fixed before any new GS code is written.

## What would change my mind

- The existing GS proves unable to reach correctness on real GT4 frames after G3.x, **and** the
  defects below turn out to be architectural rather than local. Then a clean, self-contained core
  becomes cheaper than continued patching.
- A real, functioning GS we could drive without our own `ps2xRuntime` around it. None exists.
- Licence pressure in the other direction. It does not apply: GPL-3.0 is compatible with us.
  (Had PCSX2 been genuinely LGPL, this decision would have needed re-examining, because the
  obligations and the coupling analysis would have differed. It is not LGPL.)

## The interface the next dishes implement against

This is the contract. The recompiled guest talks to the GS through exactly this.

### Entry point

```
GS::init(uint8_t* vram, uint32_t vramSize, GSRegisters* privRegs)
GS::setRasterBackend(std::unique_ptr<GSRasterBackend>)
GS::processGIFPacket(const uint8_t* data, uint32_t sizeBytes)
```

`privRegs` is **not optional in practice**: `buildPresentationRequestUnlocked()` returns an empty
request when it is null, so the frame is silently blank. The probe passes one.

### Transfer path (EE → GS)

GIF packets. A GIFTAG is 128 bits, and the split is not obvious:

- **low 64 bits:** bits 0–14 = NLOOP, bit 46 = PRE, bits 47–57 = PRIM under PRE,
  bits 58–59 = FLG, bits 60–63 = NREG.
- **high 64 bits:** NREG nibbles, 4 bits per register, in slot order.

`FLG`: 0 = PACKED, 1 = REGLIST, 2 = IMAGE. A REGLIST writes its values into the registers named
in the high half.

**Known defect, and it is a hard blocker for guest-driven rendering:** the register-address field
is only 4 bits (`gs_frontend.cpp:696`, `regs[i] = (tagHi >> (i*4)) & 0xF`), and the PACKED
handler only implements cases `0x00`–`0x0F`. Registers above `0x0F` are therefore **unreachable
from any GIF packet**: `FRAME_1` (0x4C), `ZBUF_1` (0x4E), `SCISSOR` (0x40/0x41) and `FINISH`
(0x61) all sit in the top of the map. Real hardware uses a documented escape for this; this
runtime does not implement it. Until that is fixed, the probe programs those registers through
the public `GS::writeRegister`, which does reach the full map.

### Registers used by the skeleton

| Register | Addr | Value written |
|---|---|---|
| `PRIM` | 0x00 | 2 (triangle) |
| `RGBAQ` | 0x01 | R,G,B,A in bits 0–31, Q in 32–63 (REGLIST path) |
| `ST` / `UV` | 0x02 / 0x03 | 0 |
| `XYZF2` / `XYZ2` | 0x04 / 0x05 | X 16.16 in 0–15, Y in 16–31, Z in 32–63 |
| `SCISSOR_1` / `_2` | 0x40 / 0x41 | whole rect in one write: X0 0–10, X1 16–26, Y0 32–42, Y1 48–58 |
| `FBA_1` | 0x4A | 0 (no fixed-point blending) |
| `FRAME_1` | 0x4C | PSM 0–2, base page 4–12, **width in 64-pixel words 16–20** |
| `ZBUF_1` | 0x4E | same shape |
| `FINISH` | 0x61 | 0 |

`FRAME_1` must carry FBW. Leaving it zero gives the rasteriser a row stride of zero and it
draws nothing at all — silently.

### Transfer path encodings (ADDED IN G2.1 — the doc was under-specified here)

G2.0 documented the GIF path and the display registers but **not** the transfer registers, which
made the transfer route unusable without reading `gs_frontend.cpp`. Now recorded, as
`GS::writeRegisterUnlocked` actually decodes them:

| Register | Addr | Field layout |
|---|---|---|
| `BITBLTBUF` | 0x50 | SBP 0–13, SBW 16–21, SPSM 24–29, **DBP 32–45, DBW 48–53, DPSM 56–61** |
| `TRXPOS` | 0x51 | SSX 0–10, SSY 16–26, **DSX 32–42, DSY 48–58**, DIR 59–60 |
| `TRXREG` | 0x52 | **W 0–11, H 32–43** |
| `TRXDIR` | 0x53 | direction; `0` = host-to-local |

Widths (`SBW`/`DBW`) are in **64-pixel words**, so a 512-pixel-wide PSMCT32 buffer is `8`. Pixel
format `0` is PSMCT32. Note `TRXREG` puts H at bit 32, not 12 — that is not the obvious packing.

`GS::uploadImageNative(bitbltbuf, trxpos, trxreg, trxdir, data, sizeBytes)` is the single-call
form: it writes those four registers and then feeds the image data, which is what a guest's
`D_UTEXTURE`-style upload does. `GSCpuBackend::UploadImage` is what actually writes VRAM.

### Register surface actually touched by the skeleton (G2.1, measured)

The probe now prints this itself, read out of the GS's own debug history rather than maintained
by hand, so it cannot drift from the code:

```
VULCAN4 GS REGTRAKE gif_packets=0 draw_events=0 registers_written=15
```

Fifteen distinct registers, with the last value written to each:

```
0x1C TEXCLUT  0x3B TEXA     0x40 SCISSOR_1  0x41 SCISSOR_2   0x42 ALPHA_1
0x43 ALPHA_2  0x47 TEST_1   0x48 TEST_2    0x4C FRAME_1     0x4D FRAME_2
0x4E ZBUF_1   0x50 BITBLTBUF 0x51 TRXPOS   0x52 TRXREG      0x53 TRXDIR
```

Two honest qualifications, because "15" understates it in one direction and overstates it in
another:

- **Fifteen is a subset.** The GS's own register recorder whitelists which registers it logs
  (`GS::recordRegisterDebugEventUnlocked`, `gs_frontend.cpp:368`). The skeleton additionally writes
  the **whole sweep from `PRMODECONT` (0x1A) to `ZBUF_1` (0x4E)** so that nothing keeps a reset
  value — 53 registers in total, of which 15 are traced. The trace is a floor, not a census.
- **The GS's history is paused by default** (`m_debugHistoryPaused = true`, for memory reasons on a
  long run). The probe calls `setDebugHistoryPaused(false)` to get a real trace.

**`draw_events=0` and `gif_packets=0` are both true and both matter.** No primitive was drawn, which
is the known rasteriser gap. And the transfer did **not** travel as a GIF packet: it used the native
entry point `GS::uploadImageNative`, which writes the four transfer registers and feeds the image
data directly. A real guest would send a GIF packet containing an `IMAGE` transfer, so
`gif_packets=0` means the *GIF image-transfer* route is still unproven even though the underlying
transfer, and the VRAM write it performs, are real.

**Nothing in 0x00–0x0F is written**, so `PRIM`/`RGBAQ`/`XYZF2`/`XYZ2` — the whole primitive path —
remains untouched today.

### Sync / present points

- `GSCpuBackend::Flush()` then `Sync(GSSyncReason::Presentation)` run inside the latch.
- `GS::latchHostPresentationFrame()` calls `backend->Present(request)` and stores the result.
- `GS::copyLatchedHostPresentationFrame(out, w, h, &displayFbp, &sourceFbp, &usedPreferred)`
  hands back host RGBA.
- `PMODE` bit 0 must be set. If it is not, `Present` returns an empty frame **with no error**.
- There is no vblank, no CSR signalling and no FINISH-driven present event. The frame is latched
  on demand. This is correct for a recompiler, which wants pixels rather than a timed display,
  but it means `CSR`/`VIF` interrupt plumbing is unproven.

### Display registers

`GSRegisters` (`include/runtime/ps2_memory.h`) is the display block, and the presentation path
decodes `DISPFB`/`DISPLAY` in **ps2xRuntime's own bit layout, not the hardware layout**:

- `DISPFB`: base page 0–8, width in 64-pixel words 9–14, pixel format 15–19, read origin X 32–42,
  read origin Y 43–53.
- `DISPLAY`: horizontal magnification 23–26, width−1 at 32–43, height−1 at 44–54.

A real EE writing genuine hardware DISPLAY values will be misread by this path. This is a gating
defect for guest frames, listed below.

## The skeleton, and how to reproduce the frame

`tools/gs/vulcan4_gs_probe.cpp` — 4 MB of GS VRAM it allocates itself, the runtime's own
`GSCpuBackend`, real GIFTAG packets for the primitive, framebuffer registers, then
`latchHostPresentationFrame` → `copyLatchedHostPresentationFrame` → a PNG encoded by the probe's
**own** zlib writer. No window, no screenshot, no window manager anywhere in the file.

```
bash /home/or/vulcan4/tools/gs/build_gs_probe.sh
/mnt/ssd/vulcan4-build/gs/vulcan4_gs_probe /mnt/ssd/vulcan4-build/gs/vulcan4_gs_frame.png
```

The probe prints a frame line with a checksum, so the frame can be verified without opening it:

**G2.0's frame, and why it was black.** `non_background=0` — every one of 262,144 pixels was
`(0,0,0)`. G2.1 established *why*, and the answer was not what G2.0 guessed:

- The triangle **did** submit. `XYZF2` and `XYZ2` both queue a vertex and both call
  `vertexKick`, so a primitive written as `RGBAQ,ST,UV,XYZF2,XYZ2` produces **six** kicks for
  three vertices and `Submit` **is** called. G2.0's "the triangle never submits" was wrong.
- The rasteriser ran and produced **no pixels**. That part is still open and is tracked in
  `LIMITATIONS.md`.

**G2.1's frame, which is not black:**

```
VULCAN4 GS pattern computed in our code: 43804 distinct colours, 1048576 bytes
VULCAN4 GS transfer BITBLTBUF=0x8000000080000 TRXPOS=0x0 TRXREG=0x20000000200 TRXDIR=0x0
VULCAN4 GS sync vblank_ticks=1 csr=0x1 (bit0=SIGNAL raised after FINISH)
VULCAN4 GS FRAME path=... 512x512 non_background=251261 distinct_colours=43804
```

43,804 distinct colours computed by us, written into VRAM by the GS's own transfer path, read back
out of VRAM by the GS presentation path, encoded by our own PNG writer. In-count and out-count
match exactly, so the round trip is lossless. 119,302 B, against 845 B for the black G2.0 frame.

**The honest boundary:** the content arrives via the **transfer** path, not the **rasteriser**. A
guest that draws primitives still needs the rasteriser, and the rasteriser is not yet producing
pixels. This dish proves the memory, transfer, readback and present routes compute and move real
data. It does not prove triangle rasterisation.

## What is not done

Carried in full in [`docs/LIMITATIONS.md`](LIMITATIONS.md) and echoed as `VULCAN4 LIMITATION:`
lines by the probe. The four that block progress, all found in the existing runtime:

1. **GIFTAG register field is 4 bits** (`gs_frontend.cpp:696`); PACKED handles only 0x00–0x0F.
   Registers ≥ 0x10 unreachable from a GIF packet. Hard blocker for guest-driven rendering.
2. **Presentation decodes DISPFB/DISPLAY in a private layout**, not the hardware layout. A guest
   writing real values is misread.
3. **`Present` returns an empty frame with no error** when `PMODE` bit 0 is clear.
4. **The triangle never submits.** With registers, FBW and scissor all verified correct, the
   vertex queue does not reach the three vertices a triangle needs and the raster writes nothing.
   Not yet diagnosed; this is the next dish.

Beyond that: no texture sampling exercised, no VU1 (that is G3.1), no EE→GS GIF DMA exercised
(`GifArbiter` unproven), no vblank or CSR signalling, Z allocated but untested, and **nothing from
Gran Turismo 4 has ever been rendered by this GS.**

## A note on honesty

A blank frame written by our own encoder is still "our code path", and the gate would pass on it.
That is exactly why it is written down here as blank. A frame we drew honestly beats a screenshot
faked politely — but a black PNG described as a working GS would be the fake, just quieter.


---

## The guest draw sequence (added G2.2 — documented, and **not yet working**)

This is the sequence a guest performs to get geometry on screen, written down as the contract the
next dish compares against real GT4 GS traffic. Every step was issued by the skeleton through the
real register path; **the raster currently rejects it**, so read the status line before trusting
the rest.

```
1. FRAME_1  (0x4C)  PSM=0 (PSMCT32), base page, FBW = width/64      -> via writeRegister (see GIF gap)
2. ZBUF_1   (0x4E)  same shape
3. SCISSOR_1/_2      whole rect in ONE 64-bit value: X0 0-10, X1 16-26, Y0 32-42, Y1 48-58
4. PRMODECONT (0x1A) 1, to make PRMODE the attribute source and IIP take effect
5. PRIM     (0x00)  type = 2 (TRIANGLE), IIP = bit 3   -> via a GIF REGLIST
6. per vertex, in order: RGBAQ (0x01), UV (0x03), XYZ2 (0x05)      -> via GIF REGLIST
7. the draw kicks by itself when the third vertex arrives
```

Two things that are **not** obvious and cost this project real time:

- **Screen coordinates are 12.4 fixed point**, so pixel `P` is the register value `P << 4`. G2.0
  passed raw pixel values and got a 2.5-pixel triangle that covered nothing.
- **Write `XYZ2` or `XYZF2`, never both.** Each queues a vertex *and* kicks. Writing both per vertex
  doubles the vertex count, so three intended vertices arrive as six and every triangle is
  degenerate. A guest never does this; the G2.0 probe did.

**Status as of G2.2: the sequence is issued and the GS acknowledges it (8 draw events) but the
raster writes nothing.** The batches arriving at the rasteriser are degenerate — `v0 == v2 ==
(0,0)`, so the edge denominator collapses to zero and the triangle is correctly skipped. **The
remaining bug is in the primitive stream decoding or the vertex queue, not in the rasteriser**,
which is now provably doing correct barycentric coverage, scissor clipping and gouraud
interpolation on the data it is handed.

**The gate's proxy is weaker than it looks and is reported as such:** G2.2's check requires 1,000+
distinct colours and 1,000+ unique pixels, and the frame **passes that** — but only on the G2.1
transfer background. The frame checksum is byte-identical to G2.1's. No geometry contributed. A
background gradient satisfies the proxy; only a human or a structural check distinguishes them.


### Where the vertex queue goes wrong (G2.2, measured)

The GS's own draw log is the evidence. For the clipped triangle, whose vertices are
`(380,40)`, `(700,60)`, `(420,240)`, the batches that actually reached the rasteriser were:

```
[gs:prim] idx=6  v0=(0,0)      v1=(700,60)  v2=(0,0)
[gs:prim] idx=7  v0=(700,60)   v1=(420,240) v2=(0,0)
```

Two facts fall straight out:

1. **The batch shifts by one vertex per draw**, and
2. **the last slot is always `(0,0)`.**

Meanwhile the two functions involved are both straightforward:

```cpp
// gs_frontend.cpp:1208 -- where a vertex is STORED
GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
...
vertexKick(regAddr == GS_REG_XYZ2);

// gs_frontend.cpp:1681 -- how a batch is READ
for (int i = 0; i < batch.vertexCount; ++i)
    batch.vertices[static_cast<size_t>(i)] = m_vtxQueue[i];
```

Store-then-kick, and the read is always slots `[0..n)`. For those to disagree by one slot, the
first `XYZ2` of a run must be stored while `m_vtxCount != 0`. `GS_REG_PRIM` is what resets it:

```cpp
case GS_REG_PRIM: { ... m_vtxCount = 0; m_vtxIndex = 0; break; }
```

**So the prime suspect is that our `PRIM` write is not landing where we think it is.** Two
consequences follow, and only one experiment separates them:

- **(A)** the `PRIM` REGLIST write never reaches `case GS_REG_PRIM`, leaving `m_vtxCount` and
  `m_prim.type` at whatever the previous packet left them — which would also explain the **8 draw
  events for 4 triangles** (two draws per triangle, i.e. `needed` resolving to something other
  than 3, most likely 1, which is the `GS_PRIM_POINT` reset value), or
- **(B)** the write lands but `m_prim.type` is overwritten by `PRMODE`, because the frontend only
  takes the type when `!m_prmodecont`:
  ```cpp
  if (m_prmodecont) { m_prim = m_primRegister; } else { m_prim.type = m_primRegister.type; }
  ```
  and the framebuffer sweep writes `PRMODECONT = 0`, i.e. AC = 0, which takes the `else` branch and
  keeps IIP out of `m_prim` entirely. That would leave the gouraud quad flat-shaded even once the
  queue is fixed.

**The one experiment that settles it:** log `m_prim.type`, `m_prmodecont` and `m_vtxCount` at the
top of `buildDrawBatch`, alongside the vertex values. Six values, one run, and (A) versus (B) is
decided. This is the first thing the next dish should do — everything else is downstream of it.


---

## The texture register sequence (G2.3)

The sequence a guest performs to bind and sample a texture. This is the contract to compare real
GT4 GS traffic against. **Formats done / not done is stated honestly below.**

```
1. Transfer the texel data into GS memory, swizzled as the target format demands.
   BITBLTBUF 0x50 : DBP (dest base page), DBW (width in 64-px words), DPSM (dest format)
   TRXPOS    0x51 : DSX/DSY destination origin
   TRXREG    0x52 : W (0-11), H (32-43)
   TRXDIR    0x53 : direction; 0 = host-to-local
   -> GS::uploadImageNative(bitbltbuf, trxpos, trxreg, trxdir, data, sizeBytes)
      writes the four registers then feeds the image data; GSCpuBackend::UploadImage does the
      VRAM write, addressed through the per-format swizzle map.

2. If the texture is indexed, upload the CLUT the same way (DPSM = PSMCT16/PSMCT32),
   then tell the GS where it is:
   TEXCLUT 0x1C : CBP (CLUT base), CBW (width in 64 entries), COU/COV (origin)

3. Bind the texture:
   TEX0_1  0x06 : TBP0 (texture base), TBW (width in 64-px units), TPSM (texture format),
                   TFX (filter), TCC, TCF, CLUT storage
   TEXFLUSH 0x3F : required on real hardware to make the binding visible; we write it and
                   record that our sampling path does not currently depend on it

4. Submit a primitive with PRIM.TME set (bit 4) so the rasteriser samples:
   PRIM 0x00 : type (2 = triangle) | IIP bit 3 | TME bit 4
   then per vertex RGBAQ (0x01), UV (0x03) -- UV is what is interpolated to find the texel --
   and XYZ2 (0x05). The draw kicks on the last vertex.

5. The sampler resolves S/T via Q, converts to texel coords, then reads through the SAME
   swizzle map the upload used: GSPSMT8::addrPSMT8 / GSPSMT4::addrPSMT4 / PSMCT16 / PSMCT32.
```

**Formats: done / not done.**

| Format | Swizzle map | Indexed | CLUT | Status |
|---|---|---|---|---|
| PSMCT32 | `ps2_gs_psmct32.h` | no | n/a | ✅ used by the skeleton |
| PSMT8 | `ps2_gs_psmt8.h` (block + column tables) | yes | yes (`m_clut`, `LoadClut`, `CSA[4]` for 16-bit CLUTs) | ✅ **swizzle proven by test** |
| PSMT4 | `ps2_gs_psmt4.h` | yes | yes | ✅ swizzle present, **not** round-trip tested |
| PSMCT16 / 16S | headers present | no | n/a | ⚠️ present, untested here |
| PSMT16 (16-bit indexed) | headers present | yes | yes | ⚠️ present, untested here |

**Swizzle is now proven, not assumed.** The round-trip test in the suite pins three things for
PSMT8: the address map is a **permutation** of a 16×16 tile (256 distinct offsets, no aliasing), a
swizzled write **reads back at the same (x,y)**, and the map is **not** the identity — `(1,0)` does
not sit next to `(0,0)`. That third assertion is the one that stops the test passing by accident.

**Not modelled:** texture filtering beyond the mode field, mipmaps and LOD selection, CLUT
animation, anisotropic/bilinear edge behaviour, and swizzle for formats other than the four above.

---

## G2.3 status — and the G2.2 lead, which matters more

**The texture *sampling* proof is NOT complete, and the blocker is G2.2, not the texture path.**

The runtime's texture machinery is largely present: per-format swizzle maps for four formats, a CLUT
cache (`m_clut`, `m_clutCbp`, `LoadClut`, with the ninth address bit for 16-bit CLUTs), and
`SampleTexture` / `combineTexture` in the rasteriser. **What is missing is a demonstration that a
sampled texel reaches the framebuffer** — and that needs a primitive to sample with.

**The G2.2 lead, and it is strong.** While adding the swizzle test I read the neighbouring case
`ps2_gs_tests.cpp:829-849`, and it **draws and asserts correct pixels**:

```cpp
gs.writeRegister(GS_REG_PRIM, GS_PRIM_TRISTRIP);
gs.writeRegister(GS_REG_RGBAQ, kColor);
gs.writeRegister(GS_REG_XYZ2, xyz(0u, 0u));
...
t.Equals(readReferencePSMCT32Pixel(vram, 0u, 4u, 4u), kColor, "should draw BCD ...");
```

That test **passes**, and its `[gs:prim]` line shows `v0=(6,0) v1=(0,6) v2=(6,6)` — **all three
vertices populated correctly**, which is exactly what my probe never achieved.

**So: the rasteriser works, the vertex queue works, and the swizzle works. The fault is in the
G2.2 probe's GIF REGLIST submission path**, not in the runtime. That reframes G2.2 from
"undiagnosed runtime bug" into "our submission path is wrong", and it is now a one-line
experiment: submit the same primitives via `GS::writeRegister` and see whether they rasterise. If
they do, the REGLIST encoder in the probe is the bug. I did not have the runway to run that here,
and I am not going to claim the answer before measuring it.


---

## 12. G2.4 — THE FAULT WAS OURS, AND IT WAS A SINGLE WRONG CONSTANT

The G2.3 lead was right: the rasteriser, the vertex queue and the swizzle were all fine, and the
fault was in our submission path. **Three separate mistakes in our own probe, all of them ours,
none of them the runtime's.** Two dishes were spent on this because each earlier one guessed a
runtime bug instead of reading the enum.

### 12.1 The fault: `PRIM` was written as the literal 2, which is a LINESTRIP

```cpp
// tools/gs/vulcan4_gs_probe.cpp, before
constexpr uint64_t kPrimTriangle = 2u;
```

```cpp
// include/runtime/gs/gs_types.h -- these match real PS2 hardware exactly
GS_PRIM_POINT = 0, GS_PRIM_LINE = 1, GS_PRIM_LINESTRIP = 2,
GS_PRIM_TRIANGLE = 3, GS_PRIM_TRISTRIP = 4, GS_PRIM_TRIFAN = 5, GS_PRIM_SPRITE = 6,
```

`2` is `GS_PRIM_LINESTRIP`. Every symptom recorded in G2.2 and G2.3 follows from that one constant,
mechanically:

| Recorded symptom | What the wrong constant actually did |
|---|---|
| "8 draw events for 4 triangles" | `vertexKick` computed `needed = 2` for a LINESTRIP, so a draw fired after **two** vertices and one more fired per leftover vertex |
| "the batch shifts by one vertex per draw" | the LINESTRIP post-draw is literally `m_vtxQueue[0] = m_vtxQueue[1]; m_vtxCount = 1;` |
| "the last slot is always `(0,0)`" | `batch.vertices[2]` was never written, so it kept whatever was in the slot before |
| "`v0 == v2`, edge denominator collapses to zero, triangle correctly skipped" | the rasteriser was doing **the right thing** on garbage input |
| "nothing was drawn" | nothing ever rasterised a triangle |

The G2.2 notes even guessed the shape — *"most likely 1, which is the `GS_PRIM_POINT` reset
value"* — when the real answer was 2, our own hard-coded constant, one line above the call site.

**The fix is to use the symbolic enum**, so the value cannot be re-derived by hand:

```cpp
constexpr uint64_t kPrimTriangle = GS_PRIM_TRIANGLE;
```

Measured after the fix, from the GS's own `[gs:kick]` trace:

```
[kick] vtxCount=1  [kick] vtxCount=2  [kick] vtxCount=3   -> draw, all three vertices present
```

`[gs:prim] type=3 ... v0=(48,48) v1=(464,96) v2=(200,240)` — three real vertices, no `(0,0)`.

**A naming trap that hid this for two dishes:** the probe's "try TRISTRIP as a control" line passed
the literal `3u` while claiming to be TRISTRIP. `3` *is* TRIANGLE. The label was wrong and the
experiment was accidentally running the right primitive, so the one control that would have caught
the typo never meant what it said.

### 12.2 Second fault, same dish: `TEST_1 = 0` rejects every pixel

Geometry was correct but still invisible. `GSCpuBackend::WritePixel`:

```cpp
const uint32_t ztestMethod = static_cast<uint32_t>((ctx.test >> 17) & 3u);
switch (ztestMethod) { case 0: zpass = false; break; case 1: zpass = true; break; ... }
if (!zpass) return;
```

`TEST_1 = 0` puts ZTEST in method 0 = **NEVER**, so every covered pixel was discarded before it could
be written. Our probe's register sweep left `TEST_1` at its reset value 0. The runtime's own passing
draw test uses `TEST_1 = 0x30000`; the probe now does too. **This is also a real runtime fidelity
gap** — hardware gates the Z test on the ZTE bit, so `TEST=0` would disable it and draw normally,
whereas this runtime reads ZTEST without checking ZTE. Recorded in `LIMITATIONS.md`.

### 12.3 Third fault, same dish: `PRMODECONT = 0` throws away TME and IIP

With the texture path wired up, the textured quads rendered **flat white**. The packet really did
carry `PRIM = 0x1b` (type 3, IIP, TME) and the GS really did receive it — but the batch reported
`tme=0`, so `SampleTexture` was never called and the quad was painted in the vertex colour.

```cpp
// gs_frontend.cpp, case GS_REG_PRMODECONT
m_prim = m_prmodecont ? m_primRegister : m_primmodeRegister;   // PRMODE, a *different* register
m_prim.type = m_primRegister.type;                              // only the type comes from PRIM
```

With `AC = 0`, PRIM supplies the **type** and `PRMODE` supplies everything else. Nobody had written
`PRMODE`, so PRIM's TME and IIP were silently discarded. The probe now sets `PRMODECONT = 1`, which
is the documented G2.2 step 4 that had been left at 0.

This one is worth flagging beyond our own probe: **any guest that writes PRIM without also setting
`PRMODECONT` or `PRMODE` gets silently flat-shaded, untextured geometry from this runtime.** That is
a divergence from hardware and it is in `LIMITATIONS.md`.

### 12.4 The picture, with a sampled texture — G2.4's actual deliverable

Two textures, uploaded through the GS's own transfer path and bound by writing the GS's own
registers. No texel is ever poked into VRAM directly and no pixel is painted by us.

| Texture | Format | Base | Exercises |
|---|---|---|---|
| `PSMCT32-quad` | `GS_PSM_CT32` direct colour, 64×64 | page 8 | the direct-colour swizzle map, nearest sampling, UV interpolation |
| `PSMT8-CLUT-quad` | `GS_PSM_T8` indexed + **PSMCT16 CLUT** | page 12 / CLUT page 16 | the indexed swizzle map **and** the CLUT cache (`CLD=1`, `LookupCLUT`) |

```
VULCAN4 GS texture PSMCT32 uploaded psm=0x0  page=8  64x64  16384B
VULCAN4 GS texture PSMT8    uploaded psm=0x13 page=12 64x64  4096B
VULCAN4 GS texture PSMCT16-CLUT uploaded psm=0x2 page=16 256x1 1024B
VULCAN4 GS texture PSMCT32-quad   bound TPSM=0x0  TME=1 IIP=1 CLUT=none
VULCAN4 GS texture PSMT8-CLUT-quad bound TPSM=0x13 TME=1 IIP=1 CLUT=PSMCT16

VULCAN4 GS FRAME ... 512x512 pixels=1048576 non_background=212091 distinct_colours=37275
                       fnv1a64=0x45c7b19d6eb5fb5a
```

**37,275 distinct colours** (gate needs ≥1000), against 43,804 for the G2.1 transfer-only
background and 845 bytes for the black G2.0 frame. The two textured quads alone carry **589** and
**24** distinct colours respectively — 589 is the signature of UV interpolation across a 64×64
texture, and a flat fill cannot produce it.

Both quads are submitted as **real GIF REGLIST packets** naming `PRIM` (0x00), `TEX0_1` (0x06),
`RGBAQ` (0x01), `ST` (0x02) and `XYZ2` (0x05) — all inside the 4-bit GIFTAG address field, so the
whole bind-and-draw goes through the path a guest's DMA would use. `TEXCLUT` (0x1C) is the one
register still needing `GS::writeRegister`, for the 4-bit-field reason in §"Transfer path".

**The gate passes, and this time it is not the background passing it.** The colour count *fell* from
G2.1's 43,804 precisely because geometry and texture now cover the background, and the two quads'
own colour counts are the structural evidence that sampling happened.

### 12.5 One unit trap, measured both ways

`BITBLTBUF.DBP` and `TEX0.TBP0` must use the **same** unit, and in this runtime both are raw 256-byte
block indices with no conversion on either side (`UploadImage` reads `m_transfer.bitbltbuf.dbp` raw;
`SampleTexture` reads `tex.tbp0` raw). G2.4 first wrote `TBP0` in pages — the transfer put the texels
at byte 2 MiB, the sampler read byte 2 KiB, and the quad went flat white with **no error anywhere**.
Multiplying by 1024 made it worse. The fix was to make both sides agree, not to be cleverer.

**That measurement exposed a real runtime bug.** Real PS2 hardware defines a texel page as 256 KiB =
1024 blocks, so a real guest writes `TBP0 = page * 1024`. This runtime's only page helper is
`framePageBaseToBlock(fbp) = fbp << 5` — an 8 KiB page — and it is applied to the **framebuffer** but
not to the texture bases. A real guest's texture base would therefore be interpreted **32× too low**.
That will bite the moment a guest draws a textured primitive, and it is in `LIMITATIONS.md`. It is a
runtime change, so it is not fixed in this dish.

### 12.6 The guest draw sequence, corrected

This replaces the G2.2 sequence, which was right in structure and wrong in two values. Both errors
were ours; the runtime is unchanged.

```
0. PRMODECONT (0x1A) = 1        AC=1, so PRIM alone supplies type/IIP/TME/ABE.  <-- was 0, SILENTLY
                                                                       discarded TME+IIP
1. FRAME_1    (0x4C) PSM=0 (PSMCT32), base page, FBW = width/64      -> via writeRegister (see GIF gap)
2. ZBUF_1     (0x4E) same shape                                      -> via writeRegister
3. TEST_1     (0x47) = 0x30000    ZTE=1, ZTEST=1, ATE=1, AREF=0     <-- was 0, ZTEST=NEVER
                                                                       rejected EVERY pixel
4. SCISSOR_1/_2      whole rect in ONE 64-bit value: X0 0-10, X1 16-26, Y0 32-42, Y1 48-58
5. XYOFFSET_1 (0x3C) = 0
6. Texture upload: BITBLTBUF(0x50) TRXPOS(0x51) TRXREG(0x52) TRXDIR(0x53), then the image data
   -> GS::uploadImageNative(...). DBP is a 256-byte block index; see 12.5.
7. If indexed, upload the CLUT the same way, then TEXCLUT(0x1C): CBP, CBW (64 entries), COU/COV
8. TEX0_1     (0x06) TBP0(0-13) TBW(14-19) TPSM(20-25) TW(26-29) TH(30-33)
                   TCC(34) TFX(35-36) CBP(37-50) CPSM(51-54) CSM(55) CSA(56-60) CLD(61-63)
9. PRIM       (0x00) type(0-2) | IIP bit 3 | TME bit 4.  type 3 = TRIANGLE.  <-- the G2.2 fault
10. per vertex: RGBAQ(0x01), ST(0x02) [S/T/Q as float, 0x02 carries S in 0-31 and T in 32-63],
                          XYZ2(0x05)   [X 12.4 in 0-15, Y 12.4 in 16-31, Z in 32-63]
11. the draw kicks itself when the third vertex arrives
```

**Non-obvious things that cost this project two dishes, collected in one place:**

- **`GS_PRIM_TRIANGLE` is 3, not 2.** Use the enum. `2` is LINESTRIP and it fails *quietly*.
- **`PRMODECONT` must be 1** or PRIM's TME/IIP are dropped and you get flat, untextured geometry
  with no error.
- **`TEST_1` must enable the Z test.** `0` means ZTEST = NEVER and every pixel is discarded. This
  is the opposite of the intuitive reading of "reset value = no test".
- **Screen coordinates are 12.4 fixed point**, so pixel `P` is `P << 4`. `Y` is at **bit 16**, not 20.
- **Write `XYZ2` or `XYZF2`, never both.** Each queues a vertex *and* kicks, so writing both doubles
  the count and every triangle is degenerate.
- **DBP and TBP0 must be in the same unit** (see 12.5).
- **`ST` carries S and T as raw floats**, not 12.4 like the screen coordinates. FST=0 is the normal
  path: the DDA interpolates S and T and only then divides by Q.

### 12.7 The tests, and one thing that did not work

Added: `tools/patches/ps2recomp-linux-g24-primenum.patch`, one test with three groups of
assertions, all reading the GS's **own** debug history rather than a list the test maintains about
itself (`GS::getDebugHistory()`, which is public).

**1. The constant.** `GS_PRIM_TRIANGLE` is 3 and `GS_PRIM_LINESTRIP` is 2. Cheap, and it stops the
literal coming back. But it would have passed *before* the fix too, because the runtime was never
wrong — so on its own it pins nothing about the fault. It needs the next group.

**2. The mechanism — and this is the one that would have caught it.** The primitive type decides how
many vertices a draw waits for, so a LINESTRIP **cannot** carry a triangle even when three vertices
are written:

| Submitted | Draws recorded by the GS | `vertexCount` per draw |
|---|---|---|
| `GS_PRIM_TRIANGLE` + 3 vertices | **1** | **3** |
| literal `2` (LINESTRIP) + the same 3 vertices | **2** | **2**, **2** |

Those numbers are the GS's, not mine, and they are the whole of G2.2's "8 draw events for 4
triangles" and "the batch shifts by one vertex per draw" as an executable fact instead of a theory.
Deliberately **no framebuffer, no rasteriser and no pixels are involved**, so this group cannot be
broken by the separate open question in the next paragraph.

**An honest caveat on "fails now, passes after."** The brief asked for a test that fails before the
fix. Because the fault was in our **probe** and not in the runtime, no runtime test can fail before
and pass after it — there is nothing in the runtime that changed. What group 2 does instead is make
"what the wrong constant actually does" checkable, so anyone who writes `PRIM = 2` expecting a
triangle finds the two-vertex batch in an assertion failure rather than in a blank frame two dishes
later. That is the nearest honest equivalent, and it is weaker than a true before/after test, so it
is worth saying so.

**3. A behavioural test that does NOT pass, and is not shipped.** The intent was to submit a
triangle and assert its **interior pixel**, pinning the fix by behaviour rather than by shape. It
does not pass, and the reason is worth recording:

> The batch reaches `DrawPrimitive` with **all three vertices correct** (`v0=(64,64) v1=(400,96)
> v2=(200,360)`), `type=3`, `scissor=(0,0)-(511,511)`, `test=0x30000`, `fbw=8 psm=0x0` — every input
> correct — and the interior pixel is still `0x00000000`. Tried: `GSCpuBackend` installed
> explicitly; `FBW` 1 and 8; readback through the GS's own `GSPSMCT32::addrPSMCT32` swizzle map
> (a linear offset reads zeroed VRAM and would fail with a *correct* rasteriser, which is exactly
> the kind of false negative this assertion must not have).

**So there is a further reason a triangle does not land that is neither the primitive type nor the
vertex queue.** Both of those are now demonstrably healthy — group 2 proves the second from the GS's
own record. This is the next thing to chase, and it is not on the critical path for this dish: the
probe draws correct, texture-sampled geometry through the real register path with 37,275 colours, so
the pipeline is proven end to end. But it is a loose end, and it should be closed before
`writeRegister` is relied on as a fallback. A failing test that ships is worse than no test, so this
one is documented instead.

**Suite state: 446 tests, 445 passed, 1 failed.** The failure is
`VU0 macro mappings cover all S1/S2 enums`, and it is **pre-existing** — proven by building and
running the suite with this dish's test block removed: 445 tests, 444 passed, the same single VU0
failure. It asserts on `instructions.h` being readable and on non-empty S1/S2 enum lists, and is
unrelated to the GS. The docs previously claimed a 444/444 baseline; the measured baseline is
**444 passed / 1 failed**, and this commit does not change it.

---

## 13. G2.5 — THE TRIANGLE ORACLE: geometry is proven, and the register sequence was wrong in one more place

§12.4 claimed geometry and texture now draw, on the evidence of 37,275 distinct colours in the frame.
That evidence was worthless: the G2.1 **transfer background** alone supplies 43,804, so a frame full
of colour bars passes any "many colours" test. Caine looked at the G2.4 frame and reported a test
pattern, not a triangle. So G2.5 built the check the earlier dishes did not have: **one primitive
whose pixels can be counted against the geometry.** Full numbers in
[`GS-TRIANGLE.md`](GS-TRIANGLE.md).

Measured, on an otherwise black 512×512 PSMCT32 framebuffer:

```
vertices (128,64) (448,96) (192,320)   analytic_area = 39936
non_background = 39936                 err = 0.000 %   bbox = [128,446]x[64,319]  == predicted
distinct_colours = 1 (flat)   unexpected_colours = 0
```

One GIF REGLIST packet, one draw event, three correct vertices at the rasteriser, and **the covered
pixel count equals the shoelace area exactly** — not within a tolerance, exactly. With a 2-colour
texture bound through `TEX0_1`, the hard edge lands between columns 287 and 288 and the two counts
are **27456 / 12480** against predicted **27456 / 12480**.

**What that settles:** the rasteriser, the vertex queue, scissor clipping, flat shading, UV
interpolation, nearest sampling and the PSMCT32 swizzle are all working on real geometry. The open
loose end in §12.7 ("a `writeRegister`-submitted triangle still does not land") **did not reproduce**:
the same triangle through the GIF path, with the Z buffer moved out of the picture, lands exactly
where the geometry says. The reason it failed there was the Z buffer, below.

### 13.1 The register sequence, corrected again — `ZBUF` is not `FRAME`

`gs_frontend.cpp`, `case GS_REG_ZBUF_1`, decodes:

```
zbp   = value & 0x1FF          bits 0-8      (NOT bits 4-12, which is FRAME's layout)
psm   = ((value >> 24) & 0xF) | 0x30
zmask = (value >> 32) & 1
```

and the backend addresses it with `framePageBaseToBlock(zbp) = zbp << 5` — an **8 KiB** page. A
512×512 PSMCT32 framebuffer is 1 MiB, i.e. **128** of those pages. So `ZBUF` base "page 1", the
obvious choice, is *inside the picture*.

The symptom is worth writing down because it is silent and convincing: the first oracle run drew
**59,700** pixels in **two** colours with a bounding box reaching `y = 383` when no vertex is below
`y = 320`. 19,764 of them were `RGB(240,255,63)` — **the depth buffer, visible in the framebuffer**,
offset from the triangle. An area check alone would have called that a failure with no explanation;
what named it was counting distinct colours and looking at the bounding box.

Corrected step 3 of §12.6's sequence:

```
3. ZBUF_1     (0x4E) zbp = 128   (block 4096 = byte 1 MiB), psm field at bit 24, zmask at bit 32
               -> any zbp below 128 puts the depth buffer inside a 512x512 PSMCT32 framebuffer
```

**This is the same 8 KiB-vs-256 KiB page defect G2.4 recorded for texture bases**, now measured on
the framebuffer side as well: real hardware's page is 256 KiB, so a guest's ZBUF base would be read
**32× too low** here. It is a runtime change and is not made in this dish; it is in
[`LIMITATIONS.md`](LIMITATIONS.md).

**The G2.4 probe has the same latent defect**: it writes `ZBUF_1 = makeFbp(1, 8)`, i.e. the FRAME
layout and a base of 1, so a depth-buffer shape is drawn inside its own picture. It is deliberately
**left untouched** so its recorded checksum (`fnv1a64=0x45c7b19d6eb5fb5a`, §12.4) stays true of the
frame that was actually produced. The oracle is the frame that proves geometry.

### 13.2 `TFX = 0` (MODULATE) cannot show a 2-colour texture

`combineTexture` computes `(texel * vertex) >> 7` for MODULATE and `texel` for DECAL. With the
white vertex colour the G2.4 probe used, MODULATE is `texel × 2` clamped: every texel above 127
saturates, so a 2-colour texture comes back as a tinted gradient and cannot be checked. **Use
`TFX = 1` (DECAL) for anything whose colour has to be predictable.** That is very likely why §12.4's
own interior pixel values did not match the texture it had uploaded.

### 13.3 The oracle itself

`tools/gs/vulcan4_gs_triangle.cpp`, built by `tools/gs/build_gs_triangle.sh` and run by
`tools/gs/run_gs_triangle.sh`. It prints its prediction before it writes a register, judges the run
against it, exits non-zero on a fail and **names the stage that swallowed the draw** (setup /
rasteriser / geometry / convention) instead of printing a smaller number. Frames:
`/mnt/ssd/vulcan4-build/gs/triangle.png` and `triangle-texture.png`. Neither image is reproduced in
any document; a human looks at the path.
