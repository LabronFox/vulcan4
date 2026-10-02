# W111 — THE WINDOW IS BLACK BECAUSE THE TEXTURE REGION IS EMPTY

**Does the captain see a picture yet? NO. `docs/evidence/w111-texture-wall.png`, captured from the
real X11 window `48234503` on `:0` with `import -window`, is 640x448 with exactly 1 distinct colour,
mean=0, stddev=0. The gate (>= 1000 colours) FAILS. The PNG is the product and the PNG is black.**

Same run's BOOT REPORT (budget 400000/60, log `boot_desk233548.log`):

    VULCAN4 BOOT REPORT functions_entered=27224 true_guest_entries=1669018 true_guest_exits=0
    halt=livelocked_in_syscall bios_files=0 intr_queued=5622 intr_run=12475
    gs_packets=5479 frames_presented=3681 gs_frame_reg_writes=1094 (ctx0=1094 ctx1=0)

## (a) THE TEXTURE REGION — scanned, whole, no stride. It is ZERO.

The 5 `tme=1` prims sample T4 at `tbp0=10240` with a CT32 CLUT at `cbp=10378`. Nobody had ever
scanned that region. `gs_cpu_backend.cpp` W111 probe, full region `words=[20480,20756)`:

    [w111:tex] drawFbp=0   regionWords=[5000,5114)   scanned=114 nonZero=0 firstNonZeroWord=none
    [w111:tex] drawFbp=160 regionWords=[20480,20756) scanned=276 nonZero=0 firstNonZeroWord=none
    [w111:tex] drawFbp=0   regionWords=[20480,20756) scanned=276 nonZero=0 firstNonZeroWord=none

Zero for both FBPs. Also note the probe now SCANS rather than samples every 16th word — the old
stride-16 probe would have missed a 64x1 sprite entirely (32 words, 2 sampled), which is exactly the
mistake that made W110's "provably empty" claim worthless.

## (c) THE CLUT — entry 0 is 0x00000000

`csa=0` means index 0 maps to the first RGBA word at `cbp=10378`:

    [w111:tex] ... || CLUT[cbp=10378] entry0=0x0 entry1=0x0

So a **correct** T4 fetch still yields black: every index in the atlas is 0 (region is zero), index 0
resolves through a zero CLUT entry, and `src=(0,0,0,0)` arrives at `WritePixel`. That is precisely what
the W110 `[w110:write]` lines showed even on the `test=0x30000` prims carrying `rgba0=(9,9,9,128)`.
**The draw code is behaving correctly. The texture simply is not there.**

## (b) WHY NO TRANSFER RAN — the wall, named

The 114,688-byte packet is **not a GS register walk at all**. Its first qwords, decoded against the
authoritative ps2sdk `GS_PACKED`/`GS_REGLIST` layout (`NLOOP` bits 0-10, `REGS` bits 11-15, `EOP` bit 19,
`REGISTER` bits 20-23, `MARK` bit 63):

    [gs:w111tag] qword#0 raw=0x00069c530000cccc nloop=19660(ours) REGS=25 EOP=0 MARK=0
    [gs:w111tag] qword#1 raw=0xcc0600b0c90c0020 nloop=32(ours)    REGS=0  EOP=1 MARK=1
    [gs:w111tag] qword#4 raw=0x00000000000c0600 nloop=24576(ours) REGS=12 EOP=0 MARK=0

Nothing coherent. **Our decoder's own bitfields are also wrong** — `gs_frontend.cpp:887-889` reads
`nloop=bits 0-14`, `flg=bits 58-59`, `nreg=bits 60-63`, which forces `nreg=0 -> 16` on every one of
these. The 9afdf8f clamp correctly SKIPPED this packet; it was never the wall and I should not have
called it one.

**The real transfer count, measured at the transfer path** (`gs_frontend.cpp` TRXPOS/TRXDIR writes,
`gs_cpu_backend.cpp:1588` `UploadImage`), whole 45 s budget:

    [w111:trx] WRITE TRXPOS value=0x0
    [w111:trx] WRITE TRXDIR dir=0 -> BeginTransfer(direction=0)
    [w111:upload] call#1 bytes=0 direction=0 dest=0,0
    ... x4 total ...

**The guest issues exactly 4 texture transfers in 45 seconds, every one with `TRXPOS=0x0` and ZERO BYTES
of pixel data.** `UploadImage` is called with `sizeBytes=0` four times. Nothing is ever copied into
VRAM, which is why the glyph atlas at `tbp0=10240` is zero.

Compounding it: **`TADDR` — the register that specifies the transfer's VRAM destination (TBP0) — does not
exist in this runtime at all.** `grep -rn TADDR tools/PS2Recomp/ps2xRuntime/` returns nothing in the GS
layer. `GSRegId` (`gs_types.h:44-101`) has `GS_REG_TRXPOS=0x51`, `GS_REG_TRXREG=0x52`,
`GS_REG_TRXDIR=0x53`, `GS_REG_HWREG=0x54` and then jumps to `GS_REG_SIGNAL=0x60`. No TADDR, no TMODE.
Even a correctly-issued transfer would have nowhere to land.

## What is NOT wrong (so the next cook does not redo it)

- **The z-test is not the wall.** Counted, not argued: `alpha=0 ate=0 zpass=3,604,224 WROTE_VRAM=...`.
  `WritePixel` must not be touched again.
- **The framebuffer is not empty.** `[frame:census] idx=203 nonzero=286720` and
  `[frame:channels] pixels=286720 R=0 G=0 B=0 A=286720 sample=(320,224) RGBA=0/0/0/255`. Every one of
  the 286,720 pixels is written, opaque black. W110's "provably empty" was wrong twice over: wrong
  instrument, and it measured black-on-black as emptiness.
- The draw path works. Registers decode, FBPs alternate 0/160, sprites land at screen (0,0),(40,0),(80,0).

## THE WALL, in one sentence

**GT4 is drawing a T4 glyph atlas that was never transferred: the guest issues 4 texture transfers with
zero TRXPOS and zero bytes, the runtime has no `TADDR` to aim them even if it did, so VRAM at
`tbp0=10240` is all zero, every index reads 0, CLUT entry 0 at `cbp=10378` is `0x00000000`, and a
perfectly correct texture fetch therefore returns `src=(0,0,0,0)` — black, honestly, with no faking.**

Next single measurement: find where the guest's texture pixel data lives before the transfer — the EE
must DMA it to the GIF channel. Log the DMAtag chain that feeds `GS_REG_HWREG` (`gs_frontend.cpp`
`case GS_REG_HWREG` -> `processImageData`) and see whether it ever fires with real bytes.
