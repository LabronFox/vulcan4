# GS-TRIANGLE — THE TRIANGLE ORACLE (goal G2.5)

**One flat-shaded triangle. One black 512×512 framebuffer. The pixels are counted and they match the
geometry exactly.**

This file is the proof for goal G2.5. Every number in it came out of a run; the two PNGs are on the
SSD and **no image is reproduced here** — the paths are given, and a human looks.

## Why this dish exists

Dish 24 (G2.4) closed with *"the fault was ours: PRIM=2 is a LINESTRIP. Geometry and a sampled
texture now draw."* The frame that produced was 512×512 with **37,275 distinct colours** — colour
bars, gradients and dithered bands. Whether any of that was a triangle was not answerable by eye,
and the old gate (≥1000 distinct colours) was satisfied by the **transfer background** alone. So
"geometry draws" had never been demonstrated. It is demonstrated here, or it is not.

## The prediction, stated before anything was run

The vertices were chosen and written down first, and the oracle prints the prediction before it
writes a single register (`tools/gs/vulcan4_gs_triangle.cpp`, the `VULCAN4 GS PREDICT` lines). The
numbers below are that output, not an explanation of whatever came out.

**The number the goal gate parses:**

```
analytic_area = 39936
```

| | value |
|---|---|
| vertices (screen pixels) | `v0 = (128, 64)`, `v1 = (448, 96)`, `v2 = (192, 320)` |
| background | black, `(0,0,0)` |
| triangle colour (flat) | `RGB(32, 192, 240)`, alpha 255 |
| **analytic_area** | **39936** |

The analytic area is the shoelace formula `|x1(y2−y3) + x2(y3−y1) + x3(y1−y2)| / 2`:

```
128*(96-320) + 448*(320-64) + 192*(64-96)
=  128*(-224) +   448*256     + 192*(-32)
=  -28672        + 114688      - 6144
=  79872  / 2   = 39936
```

The vertices were picked so that the rasteriser's own rule — **one sample per pixel, at the pixel
centre**, barycentric coverage — lands on exactly that number, so there is no rounding to argue
about and nothing to fudge. An independent implementation of that rule (in the oracle, written from
the geometry, not copied out of the GS) also predicts **39936** covered pixels and the bounding box
`[128,446] × [64,319]`.

## What was drawn, and how

Exactly one primitive, submitted the way the recompiled guest's GIF DMA will submit it:

```
1 PRMODECONT (0x1A) = 1        AC=1, so PRIM alone supplies type/IIP/TME
2 FRAME_1    (0x4C) PSM=0 (PSMCT32), base page 0, FBW = 512/64 = 8
3 ZBUF_1     (0x4E) zbp = 128   <-- see "the Z buffer was inside the picture" below
4 TEST_1     (0x47) = 0x30000    ZTE=1, ZTEST=1 (always pass), alpha test off
5 SCISSOR_1/_2      the whole 512x512 rectangle, one 64-bit write
6 XYOFFSET_1 (0x18) = 0, ALPHA_1 = 0, FBA_1 = 0
7 GS::clearActiveFramebuffer(0x00000000)   -> the black background, through the GS's own clear
8 [textured mode only] upload a 64x64 PSMCT32 texture at block 8192 (byte 2 MiB) through
   BITBLTBUF/TRXPOS/TRXREG/TRXDIR, then bind it with TEX0_1 (0x06)
9 ONE GIF REGLIST packet: PRIM(0x00) = GS_PRIM_TRIANGLE [| TME], then per vertex
   RGBAQ(0x01), UV(0x03) or ST(0x02), XYZ2(0x05).  The draw kicks on the third vertex.
10 GS::latchHostPresentationFrame -> copyLatchedHostPresentationFrame -> our own PNG writer
```

The GS's own trace for the run (`[gs:kick]`, `[gs:prim]`, from the runtime, not from this program):

```
[gs:gif]  idx=0 size=96 nloop=1 flg=1 nreg=10
[gs:kick] idx=0 drawing=1 prim=3 vtxCount=1
[gs:kick] idx=1 drawing=1 prim=3 vtxCount=2
[gs:kick] idx=2 drawing=1 prim=3 vtxCount=3
[gs:prim] idx=0 type=3 tme=0 abe=0 fst=0 ctxt=0 fbp=0 fbw=8 psm=0x0 scissor=(0,0)-(511,511)
                 test=0x30000 v0=(128,64) v1=(448,96) v2=(192,320) rgba2=(32,192,240,255)
```

`gif_tags=1 draw_events=1`: one packet, one draw, three vertices, the right ones.

## MEASUREMENT 1 — flat, unshaded triangle

```
VULCAN4 GS MEASURE png=/mnt/ssd/vulcan4-build/gs/triangle.png 512x512
             non_background=39936 distinct_colours=1 bbox=[128,446]x[64,319] fnv1a64=0x10227ba18464e483
VULCAN4 GS MEASURE predicted_area=39936 measured_area=39936 err=0% (limit 2%) -> PASS
VULCAN4 GS MEASURE bbox predicted=[128,446]x[64,319] vertex_box=[128,448]x[64,320]
             measured=[128,446]x[64,319] inside_vertices=yes matches_prediction=yes
VULCAN4 GS MEASURE flat colour(32,192,240)=39936 unexpected_colours=0
VULCAN4 GS ORACLE PASS mode=FLAT
```

| check | predicted | measured | error |
|---|---|---|---|
| covered pixels / analytic area | 39936 | **39936** | **0.000 %** |
| bounding box x | 128 … 446 | **128 … 446** | exact |
| bounding box y | 64 … 319 | **64 … 319** | exact |
| distinct non-black colours | 1 (flat, IIP clear) | **1** | — |
| pixels in an unexpected colour | 0 | **0** | — |

The bounding box lands inside `[128,448] × [64,320]`, which is what catches a Y flip or an origin-
corner error that an area check alone would let through. `distinct_colours=1` and
`unexpected_colours=0` are what prove it is a **flat** triangle and not a gradient.

**PNG:** `/mnt/ssd/vulcan4-build/gs/triangle.png` — 512×512, 2,417 bytes, FNV-1a 0x10227ba18464e483.

## MEASUREMENT 2 — the same triangle with a 2-colour texture

Same three vertices, so the area is the same number and both frames satisfy the same oracle. A
64×64 PSMCT32 texture split down the middle: `RGB(255,96,0)` orange for texel columns 0–31,
`RGB(0,96,255)` blue for 32–63, uploaded through the GS's transfer path and bound with `TEX0_1`.
`PRIM.TME` set, `TFX = 1` (DECAL), so the texel colour *replaces* the vertex colour and the two
colours arrive intact.

S and T were chosen affine in the screen coordinates of each vertex, so the interpolated S at any
covered pixel is exactly `(x − 128)/320` and the hard texture edge lands at a **predictable screen
column: 288**.

```
VULCAN4 GS MEASURE png=/mnt/ssd/vulcan4-build/gs/triangle-texture.png 512x512
             non_background=39936 distinct_colours=2 bbox=[128,446]x[64,319] fnv1a64=0x570f99303ecfe23
VULCAN4 GS MEASURE texture colourA(orange)=27456 predicted=27456
             colourB(blue)=12480 predicted=12480  unexpected_colours=0
VULCAN4 GS ORACLE PASS mode=TEXTURED
```

| | predicted | measured |
|---|---|---|
| orange pixels (columns ≤ 287) | 27456 | **27456** |
| blue pixels (columns ≥ 288) | 12480 | **12480** |
| unexpected colours | 0 | **0** |

The split is **68.75 % / 31.25 %**, not 50/50, because the triangle is not symmetric about that
column — and that is the point: the counts were predicted per column from the geometry and came out
exactly, and a scan of the frame confirms the boundary is exactly between column 287 and column 288
with no mixed column anywhere. That is UV interpolation, swizzle and nearest sampling all landing
where the arithmetic says.

**PNG:** `/mnt/ssd/vulcan4-build/gs/triangle-texture.png` — 512×512, 2,436 bytes, FNV-1a 0x570f99303ecfe23.

## Exact commands

```
bash /home/or/vulcan4/tools/gs/build_gs_triangle.sh      # g++ -std=c++20 -O1 -DAGRESSIVE_LOGS=1
bash /home/or/vulcan4/tools/gs/run_gs_triangle.sh        # both modes, log -> triangle-oracle.log
```

Run directly:

```
/mnt/ssd/vulcan4-build/gs/vulcan4_gs_triangle /mnt/ssd/vulcan4-build/gs/triangle.png
/mnt/ssd/vulcan4-build/gs/vulcan4_gs_triangle /mnt/ssd/vulcan4-build/gs/triangle-texture.png --texture
```

Both exit **0** on a pass and **1** on a fail, and the fail path names the stage that swallowed the
draw (setup / rasteriser / geometry / convention) rather than just printing a smaller number.

## Two real findings, measured, not guessed

### 1. The Z buffer was inside the picture

The first run of this oracle drew **59,700** pixels instead of 39,936, in **two** colours, with a
bounding box reaching `y = 383` when no vertex is below `y = 320`. The extra shape was 19,764
pixels in `RGB(240,255,63)` — the **depth buffer showing up in the framebuffer**.

Two causes, both in `ps2xRuntime`:

- **`ZBUF` is not decoded with the `FRAME` layout.** `gs_frontend.cpp`, `case GS_REG_ZBUF_1`:
  `zbp = value & 0x1FF` (bits 0–8), `psm = ((value >> 24) & 0xF) | 0x30`, `zmask = (value >> 32) & 1`.
  Encoding `ZBUF` the way `FRAME` is encoded (base page at bits 4–12) silently lands elsewhere.
- **The page size is 8 KiB, not 256 KiB.** The backend addresses both with
  `framePageBaseToBlock(fbp) = fbp << 5` — 32 blocks of 256 B. A 512×512 PSMCT32 framebuffer is
  1 MiB, i.e. **128** such pages, so the obvious `ZBUF` base of "page 1" is *inside* the picture.
  Real hardware's page is 256 KiB, so a guest's ZBUF base would be read 32× too low.

Fixed in the oracle by putting the depth buffer at `zbp = 128` (block 4096, byte 1 MiB, clear of the
framebuffer) with the runtime's real layout. **The runtime divergence itself is not fixed** — it is a
change in `ps2xRuntime`, which is not this dish's file, and it is recorded in
[`LIMITATIONS.md`](LIMITATIONS.md).

### 2. `TFX = 0` (MODULATE) cannot show a 2-colour texture

`combineTexture` computes `(texel * vertex) >> 7` for MODULATE. With a white vertex colour that is
`texel × 2`, clamped — every texel above 127 saturates and the texture comes back as a different,
tinted, gradient-filled image. For an oracle whose whole job is to check sampled colours, `TFX = 1`
(DECAL) is the only setting under which a 2-colour texture is checkable. The G2.4 probe used
`TFX = 0` with white vertices, which is very likely why its own interior pixel values did not match
its texture.

## What this does NOT prove, said plainly

- **Nothing from Gran Turismo 4 is drawn here.** The three vertices were typed into a GIF packet by
  `tools/gs/vulcan4_gs_triangle.cpp`. No VU1 computed them and no guest has ever reached this GS; the
  last recorded boot had `total_mmio_accesses=0`.
- **`FRAME_1`, `ZBUF_1`, `SCISSOR`, `TEST_1` and `PRMODECONT` are still written through
  `GS::writeRegister`, not through a GIF packet.** The GIFTAG register-address field in this runtime
  is 4 bits wide, so a packet cannot name a register above `0x0F`. A real guest cannot program its
  framebuffer through the GIF path at all. The primitive stream (`PRIM` 0x00 … `TEX0_1` 0x06) *is*
  fully reachable, and that is the part this oracle proves.
- **`TEST_1 = 0x30000` is a workaround for a runtime bug**, not how hardware behaves. Hardware gates
  the Z test on `ZTE`, so `TEST = 0` would draw normally; this runtime reads `ZTEST` without checking
  `ZTE`, so `TEST = 0` means NEVER and every pixel is discarded.
- **`PRMODECONT = 1` is mandatory here.** With `AC = 0` this runtime takes `TME` and `IIP` from
  `PRMODE`, which nobody wrote, so `PRIM`'s bits vanish and the geometry comes out flat and
  untextured with no error anywhere.
- **Not exercised at all:** blending, the alpha test, depth sorting, dithering, mip selection,
  bilinear filtering, indexed textures with a CLUT, MSAA, `DrawSprite`, `DrawLine`, vblank or CSR
  interrupt plumbing, and the EE→GS DMA (`GifArbiter` is still never exercised — packets are handed
  to `GS::processGIFPacket` directly).
- **One primitive, one frame.** This is an oracle, not a scene. It answers "does the rasteriser put
  pixels where the geometry says", and nothing more.

## Test suite

Unchanged by this dish: it adds no runtime code and modifies no runtime file.
`ps2xTest`: **456 tests, 456 passed, 0 failed** (measured from
`/home/or/vulcan4/tools/PS2Recomp/ps2xTest`, which is where it must be run from).