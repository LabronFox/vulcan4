# W113 — THE PAYLOAD IS PRESENT. IT ARRIVES IN THE NEXT PACKET.

**Does the captain see a picture yet? NO.** `docs/evidence/w113-payload-is-present.png`, captured from
the real X11 window `48234503` on `:0` with `import -window`: 640x448, **1 distinct colour, mean=0,
stddev=0**. GATE_FAIL, honestly. Nothing faked.

Same run's BOOT REPORT (budget 300000/60, `boot_w113_desk.log`):

    halt=livelocked_in_syscall frames_presented=3218 gs_packets=2239

## THE MAP — one FLG=IMAGE packet, decoder cursor, byte-exact (budget 300000/60)

    [gs:w113map] sizeBytes=96 tagOffset=80 offsetBeforeTagRead=80 offsetAfterTagRead=96
                 bytesAvailAtThisInstant=0 nloop=1024 nloopTimes16_claimed=16384
                 imageBytesCOMPUTED=0 rawTagLo=0x800000000009c00
    [gs:w113map] wholePacketNonZeroBytes=14 of 96
    [gs:w113map] @0  0x1000000000000004      tag: nloop=4 flg=0(PACKED) regs=1, rawHi=0x0e -> reg 14
    [gs:w113map] @8  0x000000000000000e
    [gs:w113map] @16 0x0001280000000000      value for reg 14
    [gs:w113map] @24 0x0000000000000050      BITBLTBUF
    [gs:w113map] @32 0x0000000000000000      value 0
    [gs:w113map] @40 0x0000000000000051      TRXPOS
    [gs:w113map] @48 0x000001c000000040      value -> RRW=64 RRH=448
    [gs:w113map] @56 0x0000000000000052      TRXREG
    [gs:w113map] @64 0x0000000000000000      value
    [gs:w113map] @72 0x0000000000000053      TRXDIR
    [gs:w113map] @80 0x0800000000009c00      <== THE IMAGE TAG (flg=2, nloop=1024 -> 16384 bytes owed)
    [gs:w113map] @88 0x0000000000000000      padding

The packet is 96 bytes and every byte is accounted for. It is a pure GS **transfer setup** packet —
`BITBLTBUF, TRXPOS, TRXREG, TRXDIR` in exactly that order — ending in an IMAGE tag that **declares 16,384
bytes of texel the packet does not contain**.

## THE ANSWER, WITH A NUMBER: PRESENT, not skipped

    [gs:w113await] following packet#2 sizeBytes=114688 firstQword=0x0
                    promisedTexelBytes=16384 deliveredSoFar=114688  <== DECLARED VOLUME SATISFIED

**The payload is not absent and not skipped. It arrives in the very next `processGIFPacket` call,**
114,688 bytes against a declared 16,384. GT4 does not make an IMAGE packet self-contained. Our decoder
was stateless per packet and so failed twice from one cause:

1. the IMAGE tag saw `bytesAvail=0`, clamped `imageBytes` to 0, and called
   `processImageData(data + offset, 0)` — so `UploadImage` never got a texel. **That is the black
   window**, and it is `gs_frontend.cpp:1183-1197`.
2. the following 114,688-byte packet — which *is* the payload — was walked as if it were a tag stream,
   so payload bytes were handed to `writeRegisterUnlocked` as register writes. **That is the "impossible
   packet"** I chased across W109, W111 and W112. One cause, both symptoms.

## I TRIED THE FIX. IT WORKS, AND I REVERTED IT. BOTH FACTS MATTER.

Carrying the shortfall and paying it does deliver the texels:

    [gs:w114image] PAYLOAD PACKET sizeBytes=114688 owed=16384 delivering=16384 -> processImageData
    [gs:w114image] PAYLOAD PACKET sizeBytes=2048  owed=2048  delivering=2048  -> processImageData
    [gs:w114image] PAYLOAD PACKET sizeBytes=1024  owed=1024  delivering=1024  -> processImageData

An unconditional debt broke 5 tests, so I carried only the shortfall — which is zero for a
self-contained packet, so both behaviours hold — and that passed 493/493. **Then paying it broke 2 more:**

    Total Tests: 493  Passed: 491  Failed: 2
    - raw image continuation after packed setup should not be decoded as VIF/GIF registers
    - raw qwords after a DIRECT image tag should continue the PATH2 image upload

The reason is a real contract, not a stale expectation. In **PATH2 DIRECT the image payload arrives as
HWREG register writes in the following packet** (`gs_frontend.cpp:1900` → `processImageData(buf, 8)`),
not as a raw payload block. Paying the shortfall swallows that packet whole and the upload stops
advancing — see `ps2_memory_tests.cpp:2215`.

**So the fix is real but incomplete: it needs a PATH3-vs-PATH2-DIRECT discriminator that this decoder
does not carry. Reverted rather than ship a 2-test regression to get texels one step nearer VRAM.**
Suite back to 493/493.

## TWO CORRECTIONS I OWE, BOTH THE SAME LESSON

**I compared two different halts and called it a result.** I reported "frames_presented 230 -> 3148".
Those were `halt=pc_outside_generated_table` and `halt=livelocked_in_syscall`. Per halt, at budget
300000/60:

| halt | frames_presented | gs_packets |
|---|---|---|
| `pc_outside_generated_table` | 230 (before), 312 (after) | 439, 459 |
| `livelocked_in_syscall` | 3148, 3215, 3218 | 3419, 2239, 2239 |

`pc_outside_generated_table` gives ~300 frames, `livelocked_in_syscall` gives ~3200. **You taught me that
these are not comparable numbers and you were right; I then did it anyway.** Every number from here on
carries its halt.

**`pc_outside_generated_table` is not a regression.** It is in `boot_w112_rep1.log`,
`boot_w112_rep2.log` and 27 of 173 logs on disk, exactly as you said.

## YOUR TWO REFUTATIONS, CONFIRMED IN THE TREE

Both are facts about the tree, and both kill premises I had been building on:

- `grep -c kickGifDmaChainFromMMIO ps2_recompiled_functions.cpp` → **0**. `gifDmaKick` across
  `recomp/*.cpp` → **0**. `tryProcessNativeGifImageUploadChain` has one caller,
  `ps2_runtime.cpp:2566`, inside `kickGifDmaChainFromMMIO`. **The native upload fast path is dead code
  for GT4.** Option 1 was a dead end and I spent a dish proposing it.
- The register census in `boot_w112_desk.log` contains **`reg=0x80` zero times**
  (`reg=0x0` x174, `reg=0x6` x11, then 0x50/0x51/0x52/0x53/0x41/0x42). **My W111 and W112 premise that
  "the guest writes TEX0 (0x80) and we drop it" was wrong.** The one 0x80 I logged was a one-off value
  carrying PRIM's bit layout, not a texture descriptor. Two dishes were built on that premise. It was
  wrong, it is not defended, and it is gone.

## WHERE THE WALL IS NOW

One cause, named: **the decoder is stateless per packet, and GT4 splits an IMAGE tag from its payload.**
Fixing it needs a PATH2-DIRECT discriminator. The texels then land at `BITBLTBUF.DBP = 0` while the
sampler reads `TEX0.TBP0 = 10240` (`gs_cpu_backend.cpp:1608` uses `bitbltbuf.dbp`), so the destination
still has to be reconciled — that is the step after this one, not this one.

Unchanged and not worth redoing: z-test refuted by count (`WROTE_VRAM=5,280,890`); framebuffer full, not
empty (`nonzero=286720`, `RGBA=0/0/0/255`); texture region and `CLUT[cbp=10378] entry0=0x0`; interrupt
handler `sub_0100D838` advances `0x70002050` to `0x5db` so that livelock is not the wall; `NLOOP` is 11
bits (W112, proven); `kickGifDmaChainFromMMIO` is never emitted; raw `0x80` is never written.
