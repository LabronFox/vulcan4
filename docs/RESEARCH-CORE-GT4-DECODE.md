# THE GAME'S OWN FILES, DECODED — and verified against the real machine (2026-10-09, Caine)

The captain's order for this project: *"figure out a way to make the game finally talk to the actual
game files."* This document is the first authoritative step: we now know exactly what "talking to the
game files" means, we can produce the target ourselves, and the real machine has confirmed our reading
byte for byte.

## 1. What GT4's boot actually is

GT4 ships **two** executables (this is documented by the GT modding community, MIT-licensed sources at
the bottom):

- `SCUS_973.28` — the **bootstrap** (273,020 B). This is what VULCAN 4 recompiled, and it is what the
  PS2 boots from `SYSTEM.CNF`.
- `CORE.GT4` — the **real game** (2,020,861 B), stored **compressed and unreadable**: not an ELF.

The bootstrap's job, in order: locate `CORE.GT4` (host → memory card 0 → memory card 1 → **disc root**),
verify its CRC, decompress it, read its header, verify a SHA-512 body hash, check the segments do not
overlap and that the entry point is valid, then **`ExecPS2` into the decoded image**.

`CORE.GT4`'s format (verified on our own disc, not taken on trust):

| Layer | Field | Value on our disc |
|---|---|---|
| Compression | `ushort` boot flags @0x00 | `0x0101` |
| | `uint` decompressed size @0x02 | **6,119,116** |
| | raw **DEFLATE** stream @0x06 | — |
| Image header | RSA modulus (128 B) + RSA value (128 B) | present |
| | section count | 3 |
| | **entry point** | **0x00100008** |
| Segments | `0x006179FC` | 24 B (`.reginfo`) |
| | `0x00100000` | **5,339,668 B (`.text` — the engine)** |
| | `0x00617A80` | 779,132 B (`.data`) |

292 bytes of header + 6,118,824 bytes of segments = 6,119,116 — the whole image, nothing left over.
(The extra Salsa20 layer the community documents applies to *GT4 Online*; our NTSC-U retail disc is
plain DEFLATE. Measured, not assumed.)

**This is the single most important number in the project: `0x00100008`.** Everything the game will
ever do — every menu, every race — happens inside the image we decode. Our recompiled engine
(`recomp_engine_r30`, 19,404 functions) covers exactly this range; what has never been proven is that
*our runtime's own decode* produces this image correctly at boot time.

## 2. Our decode, verified against the live machine

Tool: `tools/analysis/decode_core_gt4.py` (plain `zlib` raw inflate — no game code, no BIOS, no
guessing). It produces the image and a manifest with per-segment SHA-256.

Then the oracle check — `tools/oracle/compare_decoded_image.py` — decodes the file itself, breaks the
live PCSX2 at `0x00100008`, reads the three segments out of the emulator's memory, and compares:

```
entry=0x00100008  sections=3  image=6119116 B
  seg1 0x006179fc        24 B  IDENTICAL
  seg2 0x00100000   5339668 B  IDENTICAL
  seg3 0x00617a80    779132 B  IDENTICAL
VERDICT: oracle == our decode
```

6,118,824 bytes compared, zero differences. The real machine's decoded image and ours are
**byte-identical**, and it stopped at exactly the entry point our decode predicts.

## 3. What that changes

"Make the game talk to the actual game files" is now a *byte-exact, testable* target instead of a
slogan:

1. The disc read is **already working**: our runtime's `fioRead` serves all 2,020,861 bytes of the real
   `CORE.GT4` (`boot_desk173635.log` lines 1148/1158 open `cdrom0:\CORE.GT4;1`, fd 4 and 5).
2. The format is **known and confirmed** — no reverse-engineering is needed to know what the output
   must be.
3. Therefore the entire remaining question is **our own in-guest decode and hand-off**: the
   recompiled bootstrap's DEFLATE decompression (the W229 wall: *"two DEFLATE calls in FUN_0100F390
   write the same output word 0x1394420 — call 11 writes it correctly, call 40 overwrites it"*) and the
   moment it hands control to `0x00100008`.

## 4. The next wall, with its value (tomorrow's work)

**Gate:** run our runtime, stop at the hand-off (entry `0x00100008` or the `ExecPS2` call in the
bootstrap), dump the guest's image buffer, and diff it against
`/mnt/ssd/gt4/work/CORE.GT4.dec.bin`.

- **Pass:** byte-identical → our runtime decodes the game's own file correctly, and the wall moves to
  what the engine does *after* entry (which is the engine lane, not the file lane).
- **Fail:** the first differing byte offset IS the wall, named as a value, per law 13 — for example
  "our inflate leaves byte `+0xNNNNN` wrong, PCSX2's does not", which is a far sharper statement than
  any of the spin-address walls.

## 6. RESULT — our runtime's own decode, measured against the golden image (same day)

`VULCAN4_RDRAM_DUMP=100000:5D5ECC:our-image.bin` (the harness already had a whole-image dump for
exactly this question), one 120-second run (`boot_ourimg.log`: `functions_entered=147411`,
`frames_presented=6639`, `gs_packets=15`, `iop_instructions=1892`, `halt=wallclock_deadline`),
then a byte diff against `CORE.GT4.dec.bin`:

| Segment | Size | Result |
|---|---|---|
| `.reginfo` @ `0x006179FC` | 24 B | **identical** |
| `.text` @ `0x00100000` | 5,339,668 B | **BYTE-IDENTICAL — 0 differing bytes** |
| `.data` @ `0x00617A80` | 779,132 B | 15,916 differing bytes |

**The game's own engine image is decoded correctly by our runtime.** The 5.34 MB of GT4's code that the
bootstrap inflates out of `CORE.GT4` matches PCSX2's memory exactly — which retires the W229 "two DEFLATE
calls write the same output word" wall with a measurement instead of a story.

The `.data` deltas, classified: **15,899 are the game initialising its own data at runtime** (values like
`0x00617AB0` — an address inside the image pointing at itself — and `0xFFFFFFFF` sentinels, where the
file holds zeros); **12 bytes are content we do not have** (`ours = 0, golden != 0`) and 5 differ on both
sides. **Caveat, stated: this dump is taken at HALT, after the engine has run for two minutes**, so those
12 bytes may equally be the engine clearing slots it owns. They are not a wall until the same dump is
taken **at the hand-off** (the moment the bootstrap gives control to `0x00100008`).

So the file lane's state is: **initial load = correct and proven; remaining question = the streaming
path** (`SIF0` chain routing / the IOP's `PDI` modules writing the tag at `0x00874304`), which is where
`GT4.VOL`'s streamed assets (`.pss/.es/.sqt/.ins/.ads`, `carsound`) are served from.

## 7. THE HAND-OFF — CLOSED, with zero differences (same day)

The late-dump caveat in §6 is now resolved, because the dump moved to the right moment. New harness
probe (off by default): `VULCAN4_RDRAM_DUMP_AT_HANDOFF=addr:len[:path]` fires **the instant the
bootstrap gives control to the decoded image** — `VULCAN4 EXECPS2 -> unified resolve entry=0x00100008`,
`relaunch#1 entry=0x00100008 gp=0x00000000 argc=2 argv=0x80075334` — when RDRAM must hold exactly the
file's segments and nothing else.

```
dump handoff-image.bin: 6119116 B at 0x100000   golden: 6119116 B
  seg1 0x006179fc       24 B  diffbytes=0  first=none
  seg2 0x00100000  5339668 B  diffbytes=0  first=none
  seg3 0x00617a80   779132 B  diffbytes=0  first=none
VERDICT: decode is byte-identical to the file
```

**All 6,118,824 bytes — `.reginfo`, `.text` and `.data` — are byte-identical to the file's own content at
the hand-off.** The twelve bytes §6 could not classify were the engine's own runtime writes, as
suspected; that question is closed by measurement, not by argument.

**The file lane is therefore finished and proven:**

1. the disc read is real (all 2,020,861 bytes of `CORE.GT4` served by `fioRead`),
2. our in-guest DEFLATE decode is **byte-exact** (the W229 wall is retired),
3. the hand-off to `0x00100008` happens with the correct image in RDRAM, the correct entry, `argc=2`
   and `argv=0x80075334`.

What remains on the file path is **streaming**, not loading: the `SIF0` chain routing / the IOP's `PDI`
modules that serve `GT4.VOL`'s streamed assets at runtime and write the command tag at `0x00874304` —
the wall the crew had been circling, now the *last* one on this path instead of the first.

Tooling added today (all committed): `tools/oracle/vg_oracle.py` (live oracle client),
`tools/analysis/decode_core_gt4.py` (golden decode), `tools/oracle/compare_decoded_image.py`
(oracle vs decode), `tools/oracle/compare_rdram_dump.py` (dump vs file, with the missing-content
classification), and the harness hand-off probe.

## 5. Sources (MIT — readable references we may port from, with attribution)

- GT Modding Hub — *Executables (CORE.GT3/CORE.GT4)*: `nenkai.github.io/gt-modding-hub/ps2/executables/`
- GT Modding Hub — *CORE — PS2 Executables* (file layout + the GT4O crypto snippet):
  `nenkai.github.io/gt-modding-hub/formats/ps2_core/`
- `github.com/Nenkai/PDTools` — `PDTools.GT4ElfBuilderTool/GTImageLoader.cs` (MIT): the reference
  decoder, including the Salsa20 key for GT4 Online (`"PolyphonyDigital"` XOR 0x55).
- `github.com/Razer2015/GT4FS` — GT4.VOL extractor/repacker (the volume layer, needed later for
  streamed assets: `.pss/.es/.sqt/.ins/.ads` and `carsound` come from the volume via the IOP streamer).
