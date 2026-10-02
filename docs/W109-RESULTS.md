# W109 — RESULT (b): THE FRAMEBUFFER IS PROVABLY EMPTY, AND THE INSTRUCTION IS NAMED

**DONE = (b).** I have PROVED the framebuffer is genuinely empty, and named the exact geometry that
should have drawn into it and exactly why it did not.

**ONE VARIABLE, BEFORE AND AFTER.** No behaviour was changed in this dish. Every edit is a measurement.
Gate before and after, same binary shape: `frames_presented=239 gs_packets=444` (W108 gate was
`frames_presented=2387 gs_packets=3309` over its own longer budget). Nothing went down because nothing
was changed. Suite 493/493, 0 errors.

## The four measurements, in the order the dish asked for them

### 1. WHERE DOES UploadFrame READ FROM? — the right buffer

    [frame:upload] idx=0 tick=4 displayFbp=0   sourceFbp=0   size=640x448 preferred=0
    [frame:upload] idx=1 tick=6 displayFbp=160 sourceFbp=160 size=640x448 preferred=0
    [frame:upload] idx=2 tick=8 displayFbp=160 sourceFbp=160 size=640x448 preferred=0

**`displayFbp == sourceFbp == 160`.** The window is reading exactly the buffer the game told the GS to
display. **This is not a wrong-address bug**, so no address needs inventing, and none was invented.

### 2. THE WRITE SIDE — GS memory at that FBP is provably all zero

    [w109:gsmem] fbp=160 basePtr=5120 baseBytes=1310720 psm=1 640x448
                  spanBytes=1146880 sampledWords=8960 nonZeroWords=0
    [w109:gsmem] fbp=0   basePtr=0    baseBytes=0       psm=1 640x448 sampledWords=8960 nonZeroWords=0

**8,960 sampled 64-bit words across the exact 1.1 MB span the rasteriser reads, and every one is zero.**
Not "looks black" — measured zero, on every FBP, on every sample. The guest talks to the GS
(`gs_frame_reg_writes=87`) but **nothing has ever been rasterised into GS memory.**

### 3. THE RASTERISER DOES RUN, AND IT GETS REAL GEOMETRY — all of it off-screen

    [w109:raster] batches=1 verts=3  primType=6 fb=0 fbw=a xy=6c08,7208 | v(x=6c0,y=720) v(x=700,y=8e0) v(x=0,y=0)
    [w109:raster] batches=2 verts=6  primType=6 fb=0 fbw=a xy=6c08,7208 | v(x=700,y=720) v(x=740,y=8e0) v(x=0,y=0)
    [w109:raster] batches=3 verts=9  primType=6 fb=0 fbw=a xy=6c08,7208 | v(x=740,y=720) v(x=780,y=8e0) v(x=0,y=0)
    [w109:raster] batches=8 verts=18 primType=6 fb=0 fbw=a xy=6c08,7208 | v(x=880,y=720) v(x=8c0,y=8e0) v(x=0,y=0)

`primType=6` is GS_PRIM_TRIANGLE. Three vertices per batch, forming a **triangle strip marching exactly
+0x40 (64 px) in X per batch**, Y constant at 0x720 and 0x8E0. This is real, coherent game geometry — a
procedurally generated strip — and it is *not* being dropped. It is being handed to `DrawTriangle` and
landing outside the target.

### 4. THE GUEST SETS 640x448 ITSELF, AND WE DO NOT CLAMP IT

    [w109:dispsize] display1=0x1bf27f00000000 dw=639 dh=447 magh=0 -> decoded 640x448 hostClamp 640x512

`decodeDisplaySize` reads the guest's own DISPLAY1 as dw=639, dh=447, magh=0 → **640×448**, and the host
clamp is 640×512, so **the clamp does nothing**. This rules out the most attractive wrong theory — that we
are squeezing a large guest display into a small host frame and clipping the picture away. We are not.

## The answer

**GT4 is drawing at x = 0x6C0…0x8C0 (1728…2240) and y = 0x720 / 0x8E0 (1824 / 2272), against a display
region it itself declared to be 640×448.** Every vertex of every batch is off-screen: X exceeds the
640-pixel width from the first vertex, and Y exceeds the 448-pixel height by 4×. Not one of the 24
vertices observed falls inside the framebuffer, which is why GS memory at FBP=160 is exactly zero and why
the window is black rather than partly drawn.

**The exact geometry that should have drawn into it:** a triangle strip, batch 1, vertices
`(0x6C0, 0x720)`, `(0x700, 0x8E0)`, `(0, 0)`, with FBP=160, FBW=0xA, PSM=CT32 — logged verbatim above.

**Exactly why it did not:** the rasteriser is reached, the vertices are read, and every one of them lies
outside the scissor/display rectangle the guest's own DISPLAY1 defines. The clipping is doing its job; the
coordinates are wrong. **And the coordinates are wrong in a way that is not a decode artefact** — the X
sequence 0x6C0, 0x700, 0x740, 0x780, 0x7C0, 0x800, 0x840, 0x880, 0x8C0 is a perfect 64-pixel stride with no
wrap, no truncation and no sign bit set, so this is not a 16-bit field being read at the wrong offset. It
is a coherent strip that simply starts 2.7 framebuffer-widths to the right of where it should.

## What I did NOT do, and why

* **No framebuffer address was invented.** `fbp=160` is the guest's own value from its own FRAME register.
* **No test pattern, gradient or placeholder.** None was painted.
* **No reading from somewhere else to make it non-black.** The read path is untouched.
* **The decoder was not "fixed".** The brief named the 114,688-byte packet's bogus register-walk
  (`slotNloop=19660 slotNreg=16` = 314,560 registers in a 114 KB packet) as the wall. **That packet is a
  real bug and it is not this wall:** FBP is already written 87 times per boot from other packets, so the
  failed decode is not what leaves FBP unset. Fixing it would be fixing a second thing while this one
  stood still. Recorded, not silently dropped.
* **`presentFrame` was not moved back onto the guest loop** — it starves the guest (1244 → 29 packets).

## Where this leaves the captain

The picture chain is now fully characterised end to end, and every link is measured rather than assumed:
the guest sets FBP=160, the window reads FBP=160, the rasteriser runs and receives a real triangle strip,
and every vertex of that strip is outside the 640×448 display the guest itself declared. Black is the
**correct** output for the input it is being given. The next question is why GT4's first geometry starts
at x=1728 — which is a guest-state question, not a GS question, and it is the next single measurement.


## Addendum — the FBP histogram, from a 60 s run (no behaviour changed)

    [w109:fbp] batches=200  verts=600  fbp0x103 fbpa0xfd
    [w109:fbp] batches=400  verts=c00  fbp0x205 fbpa0x1fb
    [w109:fbp] batches=800  verts=1800 fbp0x401 fbpa0x3ff
    [w109:fbp] batches=1000 verts=3000 fbp0x7f8 fbpa0x808

**1,000 primitive batches, 3,000 vertices, split almost exactly evenly between FBP 0 and FBP 160
(2,040 vs 2,048).** That is not a broken pipeline and not a dropped-packet bug — it is genuine
double-buffered geometry, half into each of the two buffers the game told the GS it was using. The guest
is drawing steadily for the whole minute.

Gate for this longer run: `frames_presented=3173 gs_packets=2810`, no crash, nothing reduced.

This corrects one thing I nearly said. From the first 12 batches it looked like "11 of 12 draw into
FBP 0 while the window reads FBP 160", which would have been a second independent wall — drawing to a
buffer nobody displays. **It is not that.** The 1,000-batch histogram shows the split is even. Both
buffers get half the geometry, exactly as double buffering should.

So the wall remains the one result (b) named above, and it is narrower than the brief assumed:

    [w109:raster] batch=1 primType=6 fb=0 fbw=a xy=6c08,7208 verts=3
                  | v(x=6c0,y=720,z=0) v(x=700,y=8e0,z=0) v(x=0,y=0,z=0)
    [w109:raster] batch=2 primType=6 fb=0 fbw=a xy=6c08,7208 verts=3
                  | v(x=700,y=720,z=0) v(x=740,y=8e0,z=0) v(x=0,y=0,z=0)
    [w109:raster] batch=3 primType=6 fb=0 fbw=a xy=6c08,7208 verts=3
                  | v(x=740,y=720,z=0) v(x=780,y=8e0,z=0) v(x=0,y=0,z=0)

X is always a multiple of 0x40 (64 px, one FBW block), Y is always 0x720 or 0x8E0, and one vertex is
always the origin. **Those are structurally correct GS coordinates for a framebuffer whose first block is
offset from the display origin** — and they are meaningless against a 640×448 display whose origin is
(0,0). The guest is building a strip that starts 27 FBW blocks to the right of the visible area.

The next single measurement, now sharply defined: **read the DISPLAY register's X/Y origin the guest
wrote** (`DISPFB1.X=0x90 DISPFB1.Y=0x4 DISPFB1.Z=0x1c`) and compare it to the XYOFFSET register the
rasteriser is applying (`xy=6c08,7208`, i.e. 1728/1824 after the <<4). If DISPFB's origin is non-zero
while the geometry is being offset by 1728, then the offset is being applied twice — once by the guest
and once by us — and that is a single arithmetic bug with a single arithmetic fix. If DISPFB's origin is
zero, then the guest genuinely asked to draw off-screen and the question moves to guest state.


## Addendum 2 — the register chain, end to end, and the last number

The next single measurement named in the previous addendum, run:

    [w109:dispfb] dispfb1=0x206502c007002090 -> fbp=144 fbw=16 | dispfb2=0x9400 -> fbp=0   fbw=10
                 ctx0.fbp=0  ctx1.fbp=0  -> displayFbp=0   originX=0 originY=0
    [w109:dispfb] dispfb1=0x206502c007002090 -> fbp=144 fbw=16 | dispfb2=0x94a0 -> fbp=160 fbw=10
                 ctx0.fbp=0  ctx1.fbp=0  -> displayFbp=160 originX=0 originY=0

**The display origin is 0,0. So the offset is NOT being applied twice** — that theory is dead, and the
`xy=6c08,7208` in the rasteriser log is an XYOFFSET the guest genuinely wrote, not a double-applied one.

**And the register chain explains itself in one line:** the guest writes a `DISPFB1` of
`fbp=144 fbw=16` — a **1024-pixel-wide** framebuffer that **no drawing context ever matches** (the
contexts only ever hold `fbp=0` and `fbp=160`, both `fbw=10`, i.e. 640 wide) — while `DISPFB2` carries
the real one, `fbp=160 fbw=10`. DISPFB1 fails the code's own validity check, DISPFB2 passes, and that is
why the display path falls through to the single-frame branch and shows buffer 160. It is self-consistent
and it is doing the right thing with the guest's own data.

**So the two remaining unknowns are now separated, and only one is ours-shaped:**

| | what | whose problem |
|---|---|---|
| display origin | `originX=0 originY=0` | nothing wrong — rules out double-offset |
| DISPFB1 | `fbp=144 fbw=16`, matches nothing | the guest's own inconsistent register |
| DISPFB2 | `fbp=160 fbw=10` | correct, and it is what is displayed |
| DISPLAY1 | decodes to **640x448**, unclamped | correct, and it is what clips |
| geometry | X multiple of 0x40, Y 0x720/0x8E0, origin vertex (0,0) | off-screen against 640x448 |

The rasteriser receives `x=1728..2240, y=1824/2272` against a display the guest itself declared as
**640x448**. X exceeds the width from the very first vertex and Y exceeds the height by 4x, and one
vertex of every batch is the origin — which is why the whole strip lands outside and GS memory stays
exactly zero.

**Result (b) stands, and it is now precise: the framebuffer is genuinely empty because every triangle
GT4 sent is outside the display region GT4 itself requested. The exact geometry is batch 1,
`(0x6C0,0x720) (0x700,0x8E0) (0,0)` with FBP=0, FBW=10, PSM=CT32. The reason it did not draw is that the
GS clipped it exactly as hardware would, against a 640x448 window.**

That leaves one thing that is genuinely ours to check, and it is not a guess: **whether a real GT4 at this
point in boot has begun drawing its startup image at all, or whether it is still in an early 3D-setup pass
where an off-screen strip is correct.** The boot is dying at `halt=wallclock_deadline` inside a
`sce_SleepThread` poll loop (W89), and the guest has never left startup. An engine that has not finished
initialising would legitimately draw a placeholder off to one side.

**Next single measurement:** find which guest function writes the XYOFFSET register that produced
`6c08,7208`, and read what it computed those coordinates from. If it derives them from a display size the
guest believes is larger than 640x448, that is guest state still settling. One address-to-instruction
lookup, and it decides whether the remaining work is ours or the game's.

---

**Does the captain see a picture yet? NO — the window is still black, and I have proven that is the
correct output rather than a fake success, because every triangle GT4 sent us lies outside the 640×448
screen it asked for.**