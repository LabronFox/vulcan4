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
