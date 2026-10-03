# W114 — THE SHORTFALL IS PAID AND THE TEXELS LAND. THEY LAND IN THE WRONG PLACE.

**Does the captain see a picture yet? NO.** `docs/evidence/w114-shortfall-paid.png`, captured from the
real X11 window `48234503` on `:0` with `import -window`: 640x448, **1 distinct colour, mean=0,
stddev=0**. GATE_FAIL, honestly. Nothing faked.

Same run's BOOT REPORT (budget 300000/60, `boot_w114_desk.log`):

    halt=wallclock_deadline frames_presented=3193 gs_packets=3729

## WHAT MOVED

Texels now reach VRAM for the first time in this project's history on the GS texture path:

    [gs:w114debt]  IMAGE tag claims 16384; this packet supplies 0; PATH3 SHORTFALL carried=16384
    [gs:w114image] PAYLOAD PACKET sizeBytes=114688 owed=16384 delivering=16384 -> processImageData
    [gs:w114image] PAYLOAD PACKET sizeBytes=2048  owed=2048  delivering=2048  -> processImageData
    [gs:w114image] PAYLOAD PACKET sizeBytes=1024  owed=1024  delivering=1024  -> processImageData
    [gs:w114image] PAYLOAD PACKET sizeBytes=256   owed=256   delivering=256   -> processImageData

Four real payload deliveries per run, 19,712 bytes, none of which happened before W114.

Suite **493/493**. Gate at budget 300000/60, with halt named every time:

| halt | frames_presented | gs_packets |
|---|---|---|
| `livelocked_in_syscall` | 3158 | 3939 |
| `wallclock_deadline` | 3193 | 3729 |

## THE DISCRIMINATOR — AND WHY MY FIRST ONE WAS THE WRONG TOOL

In W113 I flagged the discriminator as "thread `path2DirectHl` through `GifArbiter::submit`". I did
that first, and **it was not enough, for two reasons worth recording.**

**The suite builds its own arbiter.** `GifArbiter`'s delivery callback must stay 2-arg because the tests
construct it with 2-arg lambdas (`ps2_memory_tests.cpp:1016, 1043, 2107, 2175, 2228`,
`ps2_vu1_tests.cpp:1385`); changing the signature broke six call sites. I moved the flag to a separate
delivery-mode hook, which is correct wiring — and the tests never install it, so the flag is simply
absent there. 491/493.

**The flag was the wrong question anyway.** Measuring the real shape of a PATH2 DIRECT continuation:

    pkt#1 IMAGE tag claims 16, supplies 0        -> shortfall carried
    pkt#2 32 bytes, first qword is an IMAGE tag  -> must be DECODED, not swallowed

A DIRECT continuation arrives as **its own packet that begins with its own IMAGE tag**. A raw payload
packet does not describe itself. So the discriminator is the packet, not the caller:

    [gs:w114image] SKIP debt payment: this packet declares its own image payload (PATH2 DIRECT)
                   -- it is a continuation, not payload. Decoding it normally.

Peek `FLG` of the first qword; if it is `GIF_FMT_IMAGE`, this packet is self-describing and the carried
debt is dropped rather than paid. Both behaviours then hold at once, and the suite is 493/493.

The delivery-mode hook is kept as a second condition, because it is real information the arbiter already
had and used to throw away (`ps2_gif_arbiter.cpp:40-46` sorts on it; delivery dropped it).

## WHY THE WINDOW IS STILL BLACK — the next wall, measured not guessed

The texels land, but the texture region is still empty:

    [w111:tex] drawFbp=0 regionWords=[20480,20756) scanned=276 nonZero=0 firstNonZeroWord=none
               || CLUT[cbp=10378] entry0=0x0 entry1=0x0

**Because they land in the wrong place.** `GSCpuBackend::UploadImage` writes to
`m_transfer.bitbltbuf.dbp` (`gs_cpu_backend.cpp:1608`), and the guest programs `BITBLTBUF` with value 0 —
visible in the W113 map at `@24 0x50 BITBLTBUF / @32 value 0`. So the texels go to VRAM block 0 while the
sampler reads `TEX0.TBP0 = 10240` on the five `tme=1` glyph prims. Destination and sampler disagree, so
every index still reads 0, `CLUT` entry 0 at `cbp=10378` is still `0x00000000`, and a correct fetch still
returns `src=(0,0,0,0)`.

Two honest notes on that:
- The guest never writes raw `0x80` — confirmed, zero occurrences — and `TEX0.TBP0=10240` comes from raw
  register `0x6` (`GS_REG_TEX0_1`), which the census shows written 11 times. So this is a DBP-vs-TBP0
  agreement question, not the dead "TEX0 is dropped" theory I was told in W111 and was wrong about.
- I have not yet proven where `TBP0=10240` is *meant* to come from, and I will not assert it. What is
  proven is the mismatch: written at DBP=0, read at TBP0=10240.

## ALSO CONFIRMED DEAD IN THIS TREE

`path2DirectHl` was, until this dish, plumbing that nothing ever set true from `PS2Memory`
(`ps2_memory.cpp` submits Path3 with `false` on every call). The real setter is the VIF1 interpreter
(`ps2_vif1_interpreter.cpp:323, 501`). And `kickGifDmaChainFromMMIO` has 0 emissions, so the native
upload fast path is dead code for GT4. Three "it must be this path" beliefs, all measured, all wrong.

## THE WALL, restated

**GT4 splits an IMAGE tag from its payload; the decoder was stateless per packet, so it delivered zero
texels and walked the payload as a tag stream.** W114 pays the shortfall and the texels arrive. What is
left is one step downstream and equally concrete: **they are written at `BITBLTBUF.DBP = 0` and sampled
at `TEX0.TBP0 = 10240`, and until those two agree the glyph atlas is still zero and the window is still
black.**

Next single measurement: log `BITBLTBUF.DBP` and `TEX0.TBP0` together, per transfer, and find who is
supposed to reconcile them — because on hardware a host-to-local transfer writes where TEX0 says it will,
so either `DBP` is being decoded from the wrong bits of raw `0x50`, or the guest sets `TBP0` from a place
the decoder is not reading. One of those two, measured, not guessed.
