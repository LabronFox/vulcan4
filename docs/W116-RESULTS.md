# W116 — THREE CONFIRMED REGISTER-DECODE BUGS, MEASURED FROM RAW QWORDS

**Does the captain see a picture yet: still not the finished picture.** Window is 640x448 with **16
distinct colours, mean=4256.18, stddev=13147.7** (`docs/evidence/w116-reverted-dbw48.png`, real X11
window `48234503` on `:0`). Gate needs 1000, so **GATE_FAIL**. That is unchanged from W115 and this dish
did not improve it — what it did was find and prove three decode bugs, one of which crashes the runtime.

Same run (budget 300000/60, `boot_w116r_desk.log`): `halt=wallclock_deadline frames_presented=3131
gs_packets=3999`. Suite **493/493**.

## The atlas TEX0, decoded from the raw qword

    [w116:tex0atlas] raw=0x511466942a800 TBP0=10240 TBW=10 PSM=20 (0x14) TW=10 TH=9 TCC=1
                       TFX=0 TFY=5 CBP=648 CPSM=0 CSM=0 CSA=0

Every field matches the brief's `tex0=(tbp0=10240 tbw=10 psm=0x14 tw=10 th=9 cbp=10378 ...)` **except
CBP**, and the discrepancy is a decode bug in our code, not a difference of opinion:

    CBP  bits 41-54 : 648    <- authoritative GS TEX0 layout (TFX 35-37, TFY 38-40 sit in between)
    CBP  bits 37-50 : 10378  <- what gs_frontend.cpp actually reads  (exactly 16x)

**BUG 1, CONFIRMED.** `gs_frontend.cpp:1972` and `:2003` do `t.cbp = (value >> 37) & 0x3FFF`. CBP is bits
41-54. The decoder skips TFX (35-37) and TFY (38-40) entirely and starts CBP at 37, so every CLUT lookup
in the project has been 16x off. **Not applied in this dish** — see below for why.

## BITBLTBUF, decoded from the raw qword

    [w115:bitbltbuf] raw=0x50 value=0x1280000000000
                     TRUE LAYOUT: DBP=10240 DBW=4  DPSM=0
                     OUR DECODE:   DBP=10240 dbw=1 dpsm=0

    [w115:bitbltbuf] raw=0x50 value=0x129c000000000
                     TRUE LAYOUT: DBP=10688 DBW=4  DPSM=0
                     OUR DECODE:   DBP=10688 dbw=1 dpsm=0

**BUG 2, CONFIRMED.** DBW is bits 46-51; the code reads bits 48-53 (`(value >> 48) & 0x3F`). Every
transfer has been written with `dbw=1` when the hardware register says `dbw=4`. With `dbw=1` a 64-pixel
row wraps every 32 pixels instead of every 128, so **the glyph atlas is scattered across blocks the
sampler never reads.** That is a real defect and it is very likely why 16 colours is all we get.

**BUG 2 WAS APPLIED, AND IT CRASHES.** With `(value >> 46) & 0x3F`, two of three runs died with
`*** buffer overflow detected ***`. Under gdb the same binary exits normally, so it is timing-dependent.
**The wrong DBW was masking a bounds defect in the GS VRAM write path, and the correct value walks far
enough to hit it. Reverted** — shipping an intermittent abort to fix a decode is a bad trade. The decode
bug stays recorded at the call site; the write-path bounds bug is the more valuable find and is not
claimed to be fixed.

Two more shifts are off in the same block, with no measured effect on these particular values (both read
0 either way), recorded so they are not rediscovered: **SPSM** is bits 20-25, the code reads 24-29;
**DPSM** is bits 52-57, the code reads 56-61. **SBW** is bits 14-19, the code reads 16-21. DBP at bits
32-45 is the one field that is already correct.

## The CLUT is empty at BOTH addresses, so BUG 1 is not today's wall

    [w116:clutcensus] CBP=648   byte=82944    entries=4096 nonZero=0 nonZeroAlpha=0 entry0=0x0
    [w116:clutcensus] CBP=10378 byte=1328384  entries=4096 nonZero=0 nonZeroAlpha=0 entry0=0x0

Fixing the CBP shift would point the sampler at a palette that also does not exist. The palette has never
been uploaded: transfers are seen at `dbp` 10240, 10688, 10696, 10700 and **none is 648 or 10378**. So
BUG 1 is real and worth fixing, but it is not what is black right now.

## Where that leaves the wall

Three things are now separately true, and only the first two are ours:

1. **The atlas is written.** `dbp=10240`, 28,672 pixels copied, **9,005 non-zero** — verified by reading
   every covered pixel back immediately after the write (`[w115:fulldest]`).
2. **The atlas is scattered.** `dbw` decoded 1 instead of 4 (BUG 2), so the layout on the GPU side does
   not match the layout the transfer used. Fixing it exposes a buffer overflow in the write path.
3. **The palette does not exist.** No transfer writes a CLUT to either address.

The honest order of work is therefore: **find the GS VRAM write-path bounds defect first** (it is a real
memory-safety bug that exists today and is merely being masked), then apply the DBW fix, then find where
the palette is meant to come from. Doing them in the other order either crashes or produces a different
wrong picture.

Also still open from W115 and unchanged: `TEX0.TBP0=10240` is sampled as T4 (`PSM=20`) while the transfer
runs with a decoded `dpsm=0` (CT32), so we consume 4 source bytes per texel where T4 needs half a byte.
Whether the guest really transfers CT32 or our `dpsm` decode is also wrong is not settled, and it changes
where in the payload the palette would sit.

## The instrument that found all of this

`[w116:tex0atlas]` prints the raw qword next to the decoded fields. That is the whole technique: print
the bits and the decode side by side and disagreement is visible without trusting either. Every one of
the five broken probes in this project (stride-16, block-as-word-index, too-small window, too-early
sample, and now the CLUT address) was a probe that reported a conclusion instead of showing its work.
