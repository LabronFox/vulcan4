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

4. **The triangle never submits.** With the framebuffer registers, `FBW` and the scissor all
   verified correct in the rasteriser's own log output, the vertex queue does not reach the three
   vertices a triangle requires and no pixels are written. `non_background=0`; the PNG is blank.
   **Not yet diagnosed. This is the next dish.**

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

## Project-level

- **Nothing from Gran Turismo 4 has ever been rendered by this GS.** The guest has not reached
  it: the last boot recorded `total_mmio_accesses=0`. Every GS claim so far is about our own
  code driving our own code.
- **The GS cannot be recompiled.** It is fixed-function hardware, so a renderer must be supplied.
  This is the project's first real wall.
- **The recompiled guest is still stuck in a syscall** after 3 functions. See
  [`docs/FIRST-BOOT.md`](FIRST-BOOT.md) and the G1.4 notes: the `jal` at `0x010286D4` dispatches
  the stub at `0x01028638` but the callee body never runs before control returns at
  `0x01028640`. Unresolved.
- **Licence note for the future:** PCSX2's GS is **GPL-3.0-or-later**, not LGPL-3.0 as the G2.0
  brief assumed. Compatible with us, so copying is permitted, but it would oblige us to carry
  notices, state the change, and offer corresponding source. See [`docs/GS-PLAN.md`](GS-PLAN.md).
