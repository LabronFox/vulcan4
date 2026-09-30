# AUDIO-SURVEY — what is on GT4's disc, and can we decode a sound?

**Goal G4.3s.** Survey dish. Read bytes, do not trust filenames.

**Verdict: the survey is done and the negative results are solid. The decode is NOT done, and it is
reported as a STUCK rather than faked.** No `.wav`/`.mp3` was produced, so the goal's gate is **not**
met. Everything below was measured, with the command that produced it.

---

## 1. What is actually on the disc

The ISO is `GRANTURISMO4`. Walking the ISO-9660 directory records (not `ls` of a mount) gives
**exactly 5 top-level files plus 3 printer-driver directories**:

| File | LBA | Bytes |
|---|---:|---:|
| `SYSTEM.CNF` | 429 | 57 |
| `SCUS_973.28` | 430 | 273,020 |
| `CORE.GT4` | 564 | 2,020,861 |
| `IOPRP300.IMG` | 1551 | 278,465 |
| `IRX/*` (36 modules: `SIO2MAN`, `PADMAN`, `MCMAN`, `LIBSD`, `PDIUSB`, `INET`, …) | 1687–2053 | 1,569 – 143,661 |
| `NET/*`, `EPSON/*` (printer drivers) | 2075–11958 | … |
| **`GT4.VOL`** | **105879** | **2,459,502,592** |

**There is no loose audio file anywhere on the disc.** No `.sgb`, `.wav`, `.ogg`, `.mp3`, `.vag`.
All audio, if it exists, is inside `GT4.VOL`.

### `GT4.VOL` is a real archive with a path-keyed tree

```
$ python3 -c "import struct; f=open(ISO,'rb'); f.seek(0x000cecb800); d=f.read(32); print(hex(struct.unpack_from('<I',d,0)[0]))"
0xacb990ad
```

That magic is the PS2 packed-archive format. Its header declares **23 top-level entries**. And the
archive contains real, human-readable, **path-qualified** names, which is how the layout was
confirmed:

```
vol+0x00242b10a  'DATABASEPARTS/PTEXT_ENGINE_BALANCE'
vol+0x00242b12d  'Full-engine Balancing'
vol+0x00242bfc8  'DATABASERACECATEGORY/TEXT_CATEGORY_GT_LICENSE_A'
vol+0x001130528  'The Soundtrack of Our Lives'
```

The last one is a **band name** — GT4's licensed soundtrack. The others are GT4 tuning/category
text. So this is genuinely the game's data tree, and text assets are at least partly plaintext,
which is a good sign for locating the audio bank by path.

---

## 2. The formats — measured, and mostly negative

A chunked scan of all 2,459,502,592 bytes of `GT4.VOL` for container magic:

| Magic | Candidate hits | Validated? |
|---|---:|---|
| `SGD` (Sony SGDP / `.sgb`) | 313 | **0 of 313.** None had version byte 3 *and* a legal sample rate. |
| `VAGp` (VAG ADPCM) | 0 | absent |
| `RIFF` | 0 | absent |
| `OggS` | 0 | absent |
| `OggS`/Vorbis | 0 | absent |
| `\xFF\xFB` / `\xFF\xF3` / `\xFF\xFF\xF2` (MPEG-1 L3) | 76384 / 195790 / 389171 | **all noise — see below** |

**The MPEG hits are demonstrably false.** Three of them were extracted and handed to `ffprobe`:

```
$ ffprobe -v error -show_entries format=format_name,duration -of csv=p=0 cand_m3a.bin
bin,N/A          # and cand_m3b.bin, cand_m3c.bin: identical
```

`format_name=bin`, no duration. Raw data that merely happens to contain `\xFF\xFB` is not MP3.
For scale: a 3-byte magic in 2.4 GB gives ~143 random matches, so 313 `SGD` candidates was never
evidence of anything — which is why the headers were validated rather than believed.

**⚠️ The brief's lead does not hold for GT4.** It states a `.sgb` was identified as a Sony SGXD
container (48 kHz stereo AC-3, `GT5_menu01`), "verified for GT6's disc". **GT4 has no SGDP at
all** — 0 of 313 candidates survive header validation. That lead must not be carried over.

**Conclusion: GT4's audio is not Vorbis/AC-3 in SGDP, not VAG, not Ogg, not RIFF, and not MPEG.**
On the PS2 that leaves **Sony SPU-ADPCM (`ut*`)** as the overwhelmingly likely format — it is what
`CORE.GT4`'s IOP side plays — with the sample data stored in a bank inside `GT4.VOL` alongside a
signature/vag-table. **This is inference from the negative results, not a measurement:
the format is UNVERIFIED until a bank is located and its header read.**

---

## 3. Toolchain on this box

| Tool | Present? | Licence note |
|---|---|---|
| `ffmpeg` / `ffprobe` | **yes** | LGPL-2.1-or-later (or GPL depending on build); invoked as an external process, **no code copied into our tree**, so no obligation attaches |
| `vgmstream` | **NO** | BSD-3-clause — GPL-3.0 compatible and would be the natural tool, since it decodes SGDP/ADPCM/VAG |
| `sfg2vorbis`, `madplay`, `timidity` | no | — |

**The brief states `vgmstream` is "already used on this box for GT6's disc". That is not true of
this box** — `which vgmstream` returns nothing, and the G0.1/G1.x history in `docs/TOOLCHAIN.md`
records no such install. Recorded because the next dish would otherwise assume a tool is present
and waste a cycle. Either it was used on a different machine, or that claim is wrong.

**Our own runtime has no ADPCM decoder.** `grep -rln adpcm ps2xRuntime/src ps2xRuntime/include`
returns exactly one file, `Kernel/Stubs/MPEG.cpp` — the MDEC/FMV path, not SPU-ADPCM. So there is no
in-tree decoder to lean on.

---

## 4. What decoding a sound would take

For a future dish, in order of cost:

1. **Parse the `GT4.VOL` directory** to find the audio path. The format is a plain archive; the
   names are plaintext (proved above), so a correct directory walk should surface the bank's name.
2. **Install `vgmstream`** (BSD-3-clause, compatible with our GPL-3.0). It handles SGDP, VAG and
   **SPU-ADPCM**, which is the most likely format. This is almost certainly the shortest path and
   avoids writing a decoder.
3. **Only if (2) is unavailable**, write an SPU-ADPCM decoder. SPU-ADPCM is 4-bit IMA-ADPCM-ish
   over 16-byte groups with a per-stream **signature (vagTable)** giving the 19 scaling factors; a
   bank carries one table plus many streams. That is a real piece of work, not a 20-liner — worth
   saying plainly rather than under-selling it.

---

## 5. What is still unknown

- **UNVERIFIED:** the actual audio format. Strongly *implicated* SPU-ADPCM by elimination, not proven.
- **UNVERIFIED:** the path of the audio bank inside `GT4.VOL`. The directory walk is not yet done.
- **UNVERIFIED:** whether GT4's music is streamed (ADPCM packets) or banked as whole samples.
  The IOP side uses `sceSsUt*` for streaming; **unverified** from this box.
- **Known negative, measured:** no SGDP, no VAG, no Ogg, no RIFF, no MPEG.

---

## ⚠️ The goal gate passes SPURIOUSLY — read this before believing it

The goal's VERIFY globs `/mnt/ssd/vulcan4-build/**/*.wav`. That pattern sweeps the **entire build
tree, including vendored third-party sources**, and it matches:

```
$ ls -la /mnt/ssd/vulcan4-build/android/build-arm64/_deps/raylib-src/examples/audio/resources/sound.wav
-rw-rw-r-- 1 or or 48236 ... sound.wav
$ ffprobe -v error -show_entries format=format_name,duration -of csv=p=0 <that file>
wav,0.552540
```

**That is raylib's own example sound file**, pulled in by `FetchContent` from G5.2a's ARM64
cross-build. It is not GT4 audio, it is not produced by this project, and it proves nothing about
the disc.

**So the gate reports success on a file that has nothing to do with the task.** The correct state is
recorded above: **the survey is complete, the decode is not done.** A future gate should glob a
dedicated output directory (`/mnt/ssd/vulcan4-build/audio/`) and should also assert the file's
*mtime* is newer than the survey, so a vendored example cannot satisfy it.

---

## STUCK

```
STUCK:   no real GT4 sound has been decoded, so the goal gate (a .wav/.mp3 on disk that ffprobe
         names with a duration) is NOT met. The survey half is complete and its negative results
         are solid.
TRIED:   (a) full ISO-9660 directory walk -- found 5 files, no loose audio;
         (b) chunked magic scan of all 2.459 GB of GT4.VOL for SGDP/VAG/RIFF/Ogg/MPEG;
         (c) header-validated all 313 "SGD" candidates -- 0 valid, so the brief's SGDP lead is
             dead for GT4;
         (d) extracted three MPEG candidates and let ffprobe judge them -- all "bin", so those
             are noise;
         (e) scanned for audio-ish plaintext paths -- found the archive really is path-keyed
             (DATABASEPARTS/..., licensed-band names) but no ADPCM bank path yet.
BLOCKED BY: the audio bank is inside GT4.VOL and the VOL directory format is not yet walked, and
         there is no ADPCM decoder on this box. vgmstream -- the tool that would almost certainly
         do it -- is not installed, and installing it is a decision for the captain, not something
         to do silently in a survey dish.
NEED:    authorisation to install vgmstream (BSD-3-clause, GPL-compatible), OR a dish that walks
         the GT4.VOL directory to locate the audio bank. Either one unblocks a real decode. With
         neither, a further magic-byte survey would be a third consecutive probe that changes
         nothing, which is exactly what this project has been correcting for.
```
