# W115 — THE WINDOW IS NOT BLACK ANYMORE

**The window shows a picture for the first time in this project's history.**
`docs/evidence/w115-atlas-landed.png`, captured from the real X11 window `48234503` on `:0` with
`import -window`: **640x448, 16 distinct colours, mean=4256.18, stddev=13147.7.**

**The gate is still NOT met: 16 < 1000.** So this is GATE_FAIL, honestly stated. But it is no longer the
uniform `1 colour, mean=0, stddev=0` that W110 through W114 all reported, and that difference is the
whole result of this dish.

Same run's BOOT REPORT (budget 300000/60, `boot_w115_desk.log`):

    halt=wallclock_deadline frames_presented=3153 gs_packets=3185

Suite **493/493**, 0 build errors.

## WHAT ACTUALLY MOVED — the glyph atlas now has pixels in it

    [w115:fulldest] dbp=10240 64x448 copiedPixels=28672 | pixels=28672 NONZERO=9005 firstNonZeroAt=(27,0)
    [w115:fulldest] dbp=10688 32x16 copiedPixels=512  | pixels=512  NONZERO=137  firstNonZeroAt=(0,0)

9,005 non-zero pixels in the 64x448 atlas at GS block 10240, verified by reading VRAM back immediately
after the write, before anything else can touch it.

## TWO REAL FIXES, IN ORDER

**1. Deliver the WHOLE payload packet, not the tag's share of it.** (`gs_frontend.cpp`)

Measured: the IMAGE tag says `nloop=1024` -> 16,384 bytes, but the transfer is `TRXREG 64x448 CT32` =
28,672 pixels x 4 = **114,688 bytes**, and the payload packet is **exactly 114,688**. Seven times the
tag's figure. The tag count is not authoritative for the volume; the transfer is. W114 clamped delivery to
the tag's figure, copied 4,096 pixels, and left the atlas empty.

    before: [w115:writeback] dbp=10240 copiedPixels=4096
    after:  [w115:fulldest]  dbp=10240 copiedPixels=28672  NONZERO=9005

`GSCpuBackend::UploadImage` already stops itself at `m_transferState.totalPixels`, so handing it the
whole payload is safe — it consumes exactly the transfer and no more.

**2. My W114 claim about the destination was WRONG.** I wrote that texels land at `BITBLTBUF.DBP = 0`
while the sampler reads `TBP0 = 10240`. Measured:

    [w115:bitbltbuf] WRITE raw=0x50 value=0x1280000000000 | DBP=10240 dbw=1 dpsm=0
    [w115:tex0]      WRITE raw=0x6  value=0x0            | TBP0=0 ... (later) TBP0=2304, 3088
    [w115:uploaddest] bytes=114688 dbp=10240 rrw=64 rrh=448 totalPixels=28672

**DBP is 10240 — exactly where the sampler reads.** I inferred `DBP=0` from ONE packet in the W113 map
(the first transfer, which genuinely is 0) and stated it as the wall. Destination and sampler agree.

## FOUR INSTRUMENTS THAT ALL SAID "EMPTY" AND WERE ALL WRONG

This is the part I want recorded, because it is the same failure four times and it is what made four
dishes point the wrong way.

1. **Stride-16 sampling** (W110) — a 64x1 sprite is 32 words, the probe read 2 of them.
2. **Block number used as a word index** (W111-114) — `TBP0`/`DBP` are 128-**byte** blocks, so
   `tbp0=10240` is byte 1,310,720 = word 163,840. I scanned words `[20480, 20756)` = **block 1280** and
   reported the atlas empty while the guest wrote block 10240. For four dishes.
3. **A window too small for the transfer** — ~25 KB window on a 114,688-byte write.
4. **Sampling too early** — the census printed its first four frames, which are before the one-time
   transfer, so it read VRAM before the texels existed.

Every one of these produced a confident, well-formatted "nonZero=0", and I repeated it in the write-up of
W111, W112, W113 and W114 without checking whether the probe could physically see what it claimed to be
looking at. The proof that a probe is broken is not that it agrees with the last probe.

## WHAT IS STILL MISSING, NAMED

The atlas has pixels; the window shows 16 colours, not 1000. The CLUT is still empty:

    [w115:texscan] ... CLUT[cbp=10378] byte=1328384 entry0=0x0

A T4 texture is 4bpp **indices**. Every index the sampler reads is resolved through the CLUT at
`cbp=10378`, and entry 0 there is `0x00000000` — so a correct fetch returns black for index 0. The 9,005
atlas pixels are indices; without a palette they cannot become colour. **16 distinct colours is roughly
what you get from un-palettised or partially-palettised data, and that is consistent with the window not
being black any more but not being right yet.**

Also worth naming, measured not assumed: `BITBLTBUF.dpsm` decodes as **0 (CT32)** while `TEX0.psm` on the
glyph prims is **0x14 (T4)**. We are writing a T4 atlas as if it were CT32 pixels, which is why 4 of every
source bytes are consumed per texel. That mismatch is either a decode error on `dpsm` or the guest really
transfers CT32 and the sampler reads it as T4; it is not yet settled and I am not guessing.

Next single measurement: read the CLUT the same way as the atlas — find who writes block 10378. The
transfers we see are at `dbp` 10240, 10688, 10696, 10700 and **none of them is 10378**, so either the
palette arrives in a transfer not yet seen, or it is uploaded as a trailing run inside the 114,688-byte
payload that our `totalPixels` bound stops before reaching.

## CORRECTIONS CARRIED FORWARD FROM EARLIER DISHES

- `NLOOP` is 11 bits, proven by measurement (W112).
- `kickGifDmaChainFromMMIO` has 0 emissions; the native upload fast path is dead code for GT4.
- Raw `0x80` is never written; the guest is not asking via TEX0 (W113).
- `path2DirectHl` was never set from `PS2Memory`; the VIF1 interpreter sets it.
- The z-test is not the wall: `WROTE_VRAM` counted in the millions. Do not touch `WritePixel`.
- The framebuffer is full, not empty: `nonzero=286720`, `RGBA=0/0/0/255` opaque black.
