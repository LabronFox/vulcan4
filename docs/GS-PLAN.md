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

```
VULCAN4 GS FRAME path=... 512x512 pixels=1048576 non_background=0 fnv1a64=0x1b5438fac83d0383
```

**Read that honestly: `non_background=0` means the frame is currently blank.** The pipeline is
proved end to end — GS readback to PNG is genuinely ours — but the triangle is not yet landing in
VRAM, so rasterisation itself is **not** yet proved. See the next section for why.

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
