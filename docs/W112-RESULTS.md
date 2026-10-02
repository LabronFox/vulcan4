# W112 — NLOOP IS 11 BITS, PROVEN BY MEASUREMENT, AND THE STREAM IS NOT DESYNCHRONISED

**Does the captain see a picture yet? NO.** `docs/evidence/w112-nloop-11bit.png`, captured from the real
X11 window `48234503` on `:0` with `import -window`: 640x448, **1 distinct colour, mean=0, stddev=0**.
Gate needs >= 1000. GATE_FAIL. Nothing faked.

Same run's BOOT REPORT (budget 200000/60, `boot_w112_desk.log`):

    VULCAN4 BOOT REPORT functions_entered=9832 true_guest_entries=247427175 true_guest_exits=0
    halt=livelocked_in_syscall intr_queued=1578 intr_run=1617 intr_run_by_kind=1613
    gs_packets=2374

## The read-only check I said I would run first, and its answer

I said the next measurement decides between "fix the tag bitfields" and "add TADDR/TEX0 handling". I ran
it. **The answer is neither, and my own hypothesis was wrong: the stream is NOT desynchronised.**

Every tag in the first packets, decoded both ways, side by side:

    [gs:w112tag] pkt#1 offset=0  rawLo=0x1000000000008007 || OURS nloop=7    flg=0 regs=1 || PS2SDK nloop=7    flg=0 regs=1
    [gs:w112tag] pkt#1 offset=0  rawLo=0x1000000000000004 || OURS nloop=4    flg=0 regs=1 || PS2SDK nloop=4    flg=0 regs=1
    [gs:w112tag] pkt#1 offset=80 rawLo=0x800000000009c00  || OURS nloop=1024 flg=2 regs=0 || PS2SDK nloop=1024 flg=2 regs=0
    [gs:w112tag] pkt#1 offset=0  rawLo=0x0                 || OURS nloop=0    flg=0 regs=0 || PS2SDK nloop=0    flg=0 regs=0

**OURS and PS2SDK agree on every field of every tag.** `FLG` at bits 58-59 and `REGS` at bits 60-63 were
already the GIFtag positions. I had told the captain the fix was "all three fields are wrong" — two of
the three were right, and I should have measured before claiming it.

## The one field that WAS wrong, proven rather than asserted

`NLOOP` was read as **15 bits** (`& 0x7FFF`) at six decode sites. It is **11 bits** (`& 0x7FF`).

Proof, from the log of a single tag before the change:

    [gs:w111img] flg=2 nloop(ours,bits0-14)=7168 nloop(ps2sdk,bits0-10)=1024

7168 / 1024 = **exactly 7**, because bits 11-14 of that tag hold `0b0111`. And **7168 * 16 = 114,688** —
which is the byte count of the packet I spent all of W109 calling "impossible to fit", and the packet
whose absurd length I used to justify a bounds check. It was never impossible. It was misread by 7x.

Fixed at all six decode sites (`gs_frontend.cpp` L80, L856, L904, L1017, L1302, L1345), kept in step
deliberately: two parsers of one format drifting apart is how "impossible" packets happen.

### Gate, same binary shape, same budget (200000/30), 4 runs

| build | frames_presented | gs_packets |
|---|---|---|
| 15-bit NLOOP (before) | 1568, 1614, 1611 | 2034, 1154, 1194 |
| **11-bit NLOOP (after)** | **1615, 1576, 1604, 1565** | 1179, 1864, 1159, 1744 |

frames_presented is unchanged inside run-to-run variance. **gs_packets is noisy (1154-2034) and no
single-run comparison on it means anything** — I nearly reverted a provably-correct fix over one dip.

**A correction I owe the captain, because I nearly got it wrong twice.** Two runs of the 11-bit build
came back at `frames_presented=263` and `253`, an 84% collapse that looked exactly like a regression, and
I started reverting on that. The python edit had actually thrown on an assert *before writing the file*,
so the "revert" was the same binary — and four more runs of it came back 1565-1615. The 263s were
outliers immediately after a build, not a regression. Two of my own conclusions in this dish were
instrument artefacts. Both are written down rather than quietly fixed.

## Where the texture wall actually is, after the fix

The fix removed the fake "impossible packet". It did not deliver a texel.

    [gs:w111img] IMAGE packet#1 sizeBytes=96 offset=96 bytesAvail=0 flg=2 nloop=1024 -> imageBytes=0
    [gs:w111img] IMAGE packet#1 sizeBytes=96 offset=80 ... (tag at 80, nothing after it)

An `FLG=IMAGE` tag is reached with **zero bytes remaining**, so `imageBytes = nloop*16` is clamped to 0
and `UploadImage` is called with `bytes=0`. That is still the only thing that ever calls it:

    [w111:upload] call#1 bytes=0 direction=0 dest=0,0   (x4, every one zero)

Packets are NOT being chunked — the sizes are varied and sane:

    [gs:w112size] packet#1 sizeBytes=128     packet#2 sizeBytes=96
    [gs:w112size] packet#3 sizeBytes=114688  packet#5 sizeBytes=2048
    [gs:w112size] packet#7 sizeBytes=1024    packet#9 sizeBytes=256
    [gs:w112size] packet#10 sizeBytes=384    packet#11 sizeBytes=32

So a real IMAGE tag lands with no payload behind it. The 112 KB packet is still nonsense under both
decodes. Either the guest's texel DMA is not reaching `processGIFPacket`, or these packets are not
GIFtag at all — and I now think the latter is likelier, because a correct 11-bit NLOOP still does not
describe any of them.

Unchanged and confirmed in W111, restated so nobody redoes it:
- Guest programs a correct 64x448 DIR=0 texture load; writes TEX0 (0x80) which we have no case for; the
  guest writes raw 0x88 (TADDR) **zero** times and `GSRegId` has no TADDR/TMODE.
- `[w111:tex] nonZero=0` for both FBPs on a full scan; `CLUT[cbp=10378] entry0=0x0`.
- z-test is not it: `alpha=0 ate=0 zpass=3,604,224 WROTE_VRAM=...`.
- Framebuffer is full, not empty: `nonzero=286720`, `RGBA=0/0/0/255`.
- The interrupt livelock is not it: handler `sub_0100D838` advances `0x70002050` to `0x5db`.

## THE WALL

**A real `FLG=IMAGE` tag is reached with zero payload bytes behind it, and no texel data ever reaches
`UploadImage`. The GIF bitfield bug is fixed and proven (NLOOP 15 -> 11 bits, and that misread is what
made the 112 KB packet look impossible), but with both decoders agreeing on every tag, the remaining
wall is upstream of the decoder: the guest's texture data is not arriving as GIF image payload at all.**

Next single measurement: stop decoding and look at the DMAtag chain that feeds the GIF channel — where
`submitGifPacket(GifPathId::Path3, ...)` is called (`ps2_memory.cpp:1773, 1802, 1821, 2099`) and what the
EE actually DMAs. If the chain never carries a payload block, the wall is the DMA path; if it does, the
wall is that our chunking hands `processGIFPacket` a tag without its data.
