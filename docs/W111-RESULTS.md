# W111 — GT4 ASKS FOR A TEXTURE AND NO TEXEL DATA EVER ARRIVES

**Does the captain see a picture yet? NO.** `docs/evidence/w111-texture-never-transfers.png`, captured
from the real X11 window `48234503` on `:0` with `import -window`, is 640x448 with **1 distinct colour,
mean=0, stddev=0**. The gate needs >= 1000. GATE_FAIL. Nothing was faked to change that.

Same run's BOOT REPORT (budget 200000/30, `boot_w111z_desk.log`):

    VULCAN4 BOOT REPORT functions_entered=19216 true_guest_entries=719402 true_guest_exits=0
    halt=wallclock_deadline bios_files=0 intr_queued=4374 intr_run=11630 intr_run_by_kind=11626
    gs_packets=3249 frames_presented=3219

## The captain's three steps, answered with numbers

### (a) THE TEXTURE REGION — scanned whole, no stride. It is ZERO.

The 5 `tme=1` prims sample T4 at `tbp0=10240` with a CT32 CLUT at `cbp=10378`. Nobody had scanned it.

    [w111:tex] drawFbp=0   regionWords=[5000,5114)   scanned=114 nonZero=0 firstNonZeroWord=none
    [w111:tex] drawFbp=160 regionWords=[20480,20756) scanned=276 nonZero=0 firstNonZeroWord=none

Zero for both FBPs. **The probe now scans every word.** The old stride-16 probe is why W110's "provably
empty" was worthless: a 64x1 sprite is 32 words, so the probe looked at 2 and missed the write. Never
sample a region and then call it empty.

### (c) THE CLUT — entry 0 is 0x00000000

`csa=0` maps index 0 to the first RGBA word at `cbp=10378`:

    [w111:tex] ... || CLUT[cbp=10378] entry0=0x0 entry1=0x0

So a **correct** T4 fetch yields black: every atlas index is 0, index 0 resolves through a zero CLUT
entry, `src=(0,0,0,0)` arrives at `WritePixel`. The draw code is correct. The texture is not there.

### (b) WHY NO TRANSFER RAN — the guest SETS ONE UP CORRECTLY, and no data comes

Raw register census (every `writeRegisterUnlocked` address the guest writes, `gs_frontend.cpp`):

    [w111:regs] 0x80(TEX0)=1  0x88(TADDR)=0  0x8A(TEXA)=0  0x51(TRXPOS)=4  0x53(TRXDIR)=4

And the values, decoded:

    [w111:tex0] WRITE rawReg=81 (0x51) value=0x0            TRXPOS: SSAX=0 SSAY=0 DSAX=0 DSAY=0
    [w111:tex0] WRITE rawReg=82 (0x52) value=0x1c000000040  TRXREG: RRW=64 RRH=448
    [w111:tex0] WRITE rawReg=83 (0x53) value=0x0            TRXDIR: DIR=0  (RAM -> VRAM, a texture LOAD)
    [w111:tex0] WRITE rawReg=128 (TEX0=0x80) value=0xccc9d200299ccc00

**The guest is not failing to ask.** It programs a textbook 64x448 host-to-local texture load:
`TRXPOS=0`, `TRXREG=64x448`, `TRXDIR=0`. Then it writes TEX0 (0x80) — which on real hardware is the
transfer trigger — and our runtime has **no case for 0x80 at all**.

And the data never arrives:

    [w111:upload] call#1 bytes=0 direction=0 dest=0,0
    [w111:upload] call#2 bytes=0 direction=0 dest=0,0
    [w111:upload] call#3 bytes=0 direction=0 dest=0,0

`UploadImage` is called with `sizeBytes=0`, every time. **Four transfers in 45 s, zero texel bytes.**
`BeginTransfer(direction=0)` arms the transfer and then waits for payload that never comes.

Two concrete gaps, both nameable:

1. **No texel data reaches `UploadImage`.** The GIF path-3 stream that should carry the texels is not
   delivering them. Evidence that the stream is desynchronised: the one write to raw 0x80 carries
   `0xccc9d200299ccc00`, which is pixel-shaped, not a register value — the same `...cccc` pattern as the
   qwords in the "impossible" 114,688-byte packet. Payload bytes are being handed to
   `writeRegisterUnlocked` as register writes.
2. **No TADDR.** The guest writes raw 0x88 **zero** times, and `GSRegId` (`gs_types.h:44-101`) has no
   TADDR or TMODE at all — it runs TRXPOS=0x51, TRXREG=0x52, TRXDIR=0x53, HWREG=0x54, then jumps to
   SIGNAL=0x60. So even with data in hand there is no TBP0 destination to write into.

Our decoder's own tag bitfields are wrong, which is consistent with (1):
`gs_frontend.cpp:887-889` reads `nloop=bits 0-14`, `flg=bits 58-59`, `nreg=bits 60-63`. The ps2sdk
`GS_PACKED`/`GS_REGLIST` tag word is `NLOOP` bits 0-10, `REGS` bits 11-15, `EOP` bit 19,
`REGISTER` bits 20-23, `MARK` bit 63 — so `nreg` is read from bits that are not a count at all, and
`nreg=0 -> 16` fires on almost every tag. **This is the single highest-value fix and it is a decoder
bug, not a draw bug.**

## What is NOT the wall (do not redo these)

- **The z-test.** Counted, not argued: `alpha=0 ate=0 zpass=3,604,224 WROTE_VRAM=...`. `WritePixel`
  must not be touched again. The captain was right to check and I was right to measure.
- **The framebuffer.** `[frame:census] idx=203 nonzero=286720`,
  `[frame:channels] pixels=286720 R=0 G=0 B=0 A=286720 sample=(320,224) RGBA=0/0/0/255`. All 286,720
  pixels are written, opaque black. "Provably empty" was wrong twice over: wrong instrument, and it
  read black-on-black as emptiness.
- **Coordinates.** `DrawSprite:1240-1242` and `DrawTriangle:1357-1365` subtract XYOFFSET; the sprites
  land at screen (0,0), (40,0), (80,0). The raster print now shows raw AND screen so this cannot recur.
- **The interrupt livelock.** `halt=livelocked_in_syscall`, 48,581 of 51,794 syscalls at pc 0x0100B184,
  which looked like the wall. It is not. `sub_0100B628` polls scratchpad `0x70002050`, and the counter
  it waits on is bumped by the registered interrupt handler `sub_0100D838`
  (`0x100d888 ld $v1,0x50($s1); daddu $v1,$v1,$a1; 0x100d890 sd $v1,0x50($s1)`). Measured, that handler
  is dispatched continuously and the counter does advance:

      [w111:irq838] totalDispatch=1    flag@0x70002050=0
      [w111:irq838] totalDispatch=0x1f4 flag@0x70002050=0x1f3
      [w111:irq838] totalDispatch=0x3e8 flag@0x70002050=0x356
      [w111:irq838] totalDispatch=0x5dc flag@0x70002050=0x5db

  So the flag advances and that spin exits. I chased it, measured it, and it is not the wall. Stating
  that plainly so the next cook does not spend a dish on it.
  (Two of my own instruments were wrong on the way: I read the scratchpad with `READ8` and with an RDRAM
  index, both of which silently return 0 for `0x70002050`, and I read 64-bit `$s1` through a 32-bit
  getter. All three produced confident nonsense. `ps2GetScratchpadHostPtr()` and `GPR_U64` are the
  correct accessors.)

## THE WALL, in one sentence

**GT4 programs a correct 64x448 host-to-local texture load and no texel data ever arrives —
`UploadImage` is called with `bytes=0` every time, our runtime has no case for the TEX0 (0x80) trigger
the guest actually uses, it has no TADDR (0x88) to aim the transfer, and the GIF tag decoder reads its
length field from the wrong bits (`gs_frontend.cpp:889`), so the path-3 stream desynchronises and pixel
data is delivered to `writeRegisterUnlocked` as register writes. VRAM stays zero, the glyph atlas at
`tbp0=10240` is zero, every index reads 0, CLUT entry 0 at `cbp=10378` is `0x00000000`, and a perfectly
correct texture fetch returns `src=(0,0,0,0)`.**

Next single measurement: re-decode one real transfer packet with the ps2sdk tag layout and check whether
the texel payload lands where `UploadImage` is called from. If it does, the wall is the missing 0x80/0x88
handling; if it does not, the wall is the path-3 DMAtag chain. One of those, not both.
