# W109-RESULTS — the framebuffer is NOT empty, and the colour is what's missing

Dish 51 (G2.10). Commit by `Or Golan <or024662@gmail.com>`. Not pushed.

---

## 1. THE HEADLINE: done-looks-like (b), with the exact evidence

**No picture was faked.** No test pattern, no gradient, no hardcoded framebuffer address, nothing
painted. Nothing in this dish changes behaviour — every line added is a `std::cout`.

The census runs on the exact bytes that become the window texture (`s_scratch`, immediately before
`UpdateTexture`):

```
[frame:census]   idx=1 fbp=0   size=640x448 bytes=1146880 nonzero=286720 first@0x3
[frame:census]   idx=2 fbp=160 size=640x448 bytes=1146880 nonzero=286720 first@0x3
[frame:channels] pixels=286720 R=0 G=0 B=0 A=286720 sample=(320,224) RGBA=0/0/0/255
```

**The framebuffer is full, and every pixel is `(0, 0, 0, 255)`.**

| | |
|---|---|
| pixels in the 640×448 buffer | **286,720** |
| pixels with **alpha = 255** | **286,720 — all of them** |
| pixels with R, G or B non-zero | **0** |
| `first@0x3` | byte 3 of pixel 0; PS2 PSMCT32 is RGBA8 with **R in the low byte**, so byte 3 is **alpha** |

**This is a different wall from the one the brief predicted, and it is a much better one.**

The brief's model was *"the game has not written a pixel into the framebuffer yet"*. **False.** The
game wrote the entire screen — full coverage, full alpha, every single pixel. **Coverage works. The
colour path does not.**

That also settles two things at once:

- `frame0.fbp=0` is **not** the problem. The census ran at `fbp=0` *and* `fbp=160` and found
  identical, fully-populated content. Whatever the base register says, the window is reading a real,
  fully-written framebuffer.
- The window is **not** reading the wrong buffer, and the guest is **not** failing to submit work.

So the window is black because the runtime is writing black-with-alpha, not because there is nothing
there.

## 2. THE 114,688-BYTE PACKET — the brief's lead (3), refuted

The brief called this "the single most promising lead". It is not. Measured:

```
[gs:giftag] idx=2 size=114688 words: 0 0 0 0 0 0
           | nloop_lo15=0 bit16=0 bit17_altmode=0 bit46_PRE=0 flg_58_59=0 nreg_60_63=0 tagHi=0
           | nonZeroWords=9005/28672 firstNonZero@0x6c
```

The packet's **first 108 bytes are zero**, which is why it decodes as `nloop=0, flg=0, nreg=16` and
writes zero registers. The tempting inference is a broken tag decode, an alternate-DWORD-mode packet,
or a DMAtag chain. **All three are wrong.**

I instrumented every entry point that can hand data to the GS:

| entry point | instrumented | fired? |
|---|---|---|
| `PS2Memory::processGIFPacket(srcPhysAddr, qwCount)` | `[gif:dma]` | **no** |
| `PS2Memory::processGIFPacket(data, sizeBytes)` | `[gif:ptr]` | **no** |
| `ps2_vu1_core.cpp:922` `submitGifPacket(Path1, m_xgkick.packet.data(), …)` | (by elimination) | **yes** |

`submitGifPacket` is called **directly** from the VU1 XG-kick path and **bypasses both
`processGIFPacket` overloads** — which is precisely why the first two dumps printed nothing. That
absence is the proof.

**So the 114,688-byte packet is a VU1 XGKICK: vertex/pixel data from the VU1, not a display
register walk.** It is not going to carry FBP, and chasing it further was a dead end. Worth recording
because the packet size — 7,168 quadwords — looks like a display-setup packet and is not. (For the
record, the PS2 GIF DMA ring is 256 QW / 4 KB, so 7,168 QW could never have come from the ring at
all; another reason it is not the EE path.)

## 3. WHERE THE WALL ACTUALLY IS

Two measurements that were not asked for and that matter more:

**1. `UploadFrame` reads a real, non-zero framebuffer, and `fbp` oscillates rather than being zero.**

```
[frame:upload] idx=0 tick=4  displayFbp=0   sourceFbp=0   size=640x448 preferred=0
[frame:upload] idx=1 tick=6  displayFbp=160 sourceFbp=160 size=640x448 preferred=0
...
73 samples at displayFbp=0 / 72 at displayFbp=160
```

`size=640x448` — the **640 width is exactly right** for `fbw=0x10`. So the display path is reading
the guest's own 640-wide buffer at both bases. `fbp` toggling 0 ↔ 160 is GT4 double-buffering through
the layout we read; it is not the wall.

**2. The colour arrives as nothing.** `A=286720` with `R=G=B=0` means the rasteriser ran, covered
every pixel, and resolved the colour to zero. Candidate causes, **none of which I fixed, in order of
where I would look**:

- **`PRMODE`/`PRIM` semantics.** `gs_frontend.cpp`'s `case GS_REG_PRMODECONT` takes `tme`/`iip`/`abe`
  and the rest from the **`PRMODE`** register when `AC = 0`, taking only `type` from `PRIM`. GT4 writes
  660 FRAME/ZBUF registers and never writes `PRMODECONT` (0x1a) or `PRMODE` (0x41) in the traced set.
  If the guest's colour lives in `PRMODE`, we are reading a register nobody wrote — the exact shape of
  W7's `FindAddress` hunt.
- **Texture/shading source empty.** The 9,005 non-zero words in the XGKICK packet are vertex data; if
  the guest is shading from a texture the GS has not received, colour legitimately resolves to zero.
- **`TEXA`/`TEXCLUT` never written** by the guest in the traced set — same family.

I did **not** act on any of these. Each is a runtime change, and each would need its own RED test;
guessing between them at the end of a shift is how this project produced four days of "we fixed the
rasteriser" that meant nothing. Naming the wall precisely is worth more than a coin flip on a fix.

## 4. NUMBERS, BEFORE AND AFTER

Only logging was added, so nothing should have moved.

| | W108 (dish 50) | W109 (this dish) |
|---|---|---|
| `frames_presented` | 2387 | 1878 |
| `gs_packets` | 3309 | 1319 |
| `halt` | `wallclock_deadline` | `livelocked_in_syscall` |
| suite | 492/493 | **492/493** (unchanged) |

**These are not comparable runs and I will not present them as a regression or a win.** W108's gate
used a 45 s deadline; this dish's measurement runs used 35 s, and the run ended on a **different
wall** (`livelocked_in_syscall` instead of running out its budget), so it did less guest work before
stopping. The one number that *is* comparable, `frames_presented`, is the same order as W106's
zero-and-W108's 2387, and no behaviour changed. **A like-for-like run is the next thing to do before
anyone claims a number moved.**

Suite: `493 tests, 492 passed, 1 failed` — `VU0 macro mappings`, the known working-directory
artefact. This dish touched no test file.

## 5. INSTRUMENTATION SHIPPED

`tools/patches/ps2recomp-linux-w109-fbcensus.patch`, purely additive, three call sites:

- `gs_frontend.cpp` — raw GIFTAG words, the per-field breakdown, a whole-packet non-zero census, and
  the decoded tags around the first non-zero word (`[gs:giftag]`).
- `ps2_memory.cpp` — both GIF entry points instrumented, which is how the XGKICK was identified
  (`[gif:dma]`, `[gif:ptr]`).
- `ps2_runtime.cpp` — the framebuffer census and the per-channel census (`[frame:census]`,
  `[frame:channels]`).

Two notes for whoever reads this next. `PS2_IF_AGRESSIVE_LOGS` takes **one** argument, so any block
containing top-level commas must be wrapped in an extra pair of parentheses — that cost two failed
builds and is why the census block looks the way it does. And the census must run on `s_scratch`
*before* `UpdateTexture`, because that is the only buffer that is provably the window's contents.

## 6. THE HONEST SENTENCE

**No — the captain still does not see a picture, and I will not pretend otherwise.** What this dish
established is that the game is already drawing the whole screen into the framebuffer the window
reads (286,720 pixels, full alpha) and that only the **colour** is missing — every pixel is
`(0,0,0,255)` — so the next wall is the colour path, not the framebuffer, and the 114,688-byte packet
everyone was going to chase is a VU1 XGKICK that was never going to set FBP.