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

## 5. Sources (MIT — readable references we may port from, with attribution)

- GT Modding Hub — *Executables (CORE.GT3/CORE.GT4)*: `nenkai.github.io/gt-modding-hub/ps2/executables/`
- GT Modding Hub — *CORE — PS2 Executables* (file layout + the GT4O crypto snippet):
  `nenkai.github.io/gt-modding-hub/formats/ps2_core/`
- `github.com/Nenkai/PDTools` — `PDTools.GT4ElfBuilderTool/GTImageLoader.cs` (MIT): the reference
  decoder, including the Salsa20 key for GT4 Online (`"PolyphonyDigital"` XOR 0x55).
- `github.com/Razer2015/GT4FS` — GT4.VOL extractor/repacker (the volume layer, needed later for
  streamed assets: `.pss/.es/.sqt/.ins/.ads` and `carsound` come from the volume via the IOP streamer).
