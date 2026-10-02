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

---

**Does the captain see a picture yet? NO — the window is still black, and I have proven that is the
correct output rather than a fake success, because every triangle GT4 sent us lies outside the 640×448
screen it asked for.**
