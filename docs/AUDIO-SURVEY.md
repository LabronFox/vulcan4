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

### 3b. CORRECTION TO §3 — the brief was RIGHT, and this substantially unblocks the STUCK

I claimed `vgmstream` was absent. **It is present, built, and proven** — just not installed
system-wide, which is why `which vgmstream` found nothing:

```
$ /mnt/ssd/gt6/tools/vgmstream/build/cli/vgmstream-cli
vgmstream CLI decoder r2117 (Sep 28 2026)
```

And the sibling project demonstrates the **whole method end to end**: it unpacked its `.VOL` into
a tree and fed the containers to that CLI.

```
$ ls /mnt/ssd/gt6/vol_unpacked
car  carparts  carsound  character  crowd  crs  database  description  effect  font  game_parameter  icon ...
$ ls /mnt/ssd/gt6/vol_unpacked/sound_gt/track | head
0545.sgb  0561.sgb  0454.sgb ...
$ ls -la /mnt/ssd/gt6/music | head
GT5_Menu01_Casino_Drive.ogg   40,605,572 B
GT5_Menu02_Liberty.ogg       48,775,080 B
```

So the pipeline is: **unpack the VOL → hand each container to `vgmstream-cli` → real audio out.**
Its own driver script is `/mnt/ssd/gt6/tools/extract_music.py`.

**The one thing that does not transfer:** gt6's unpacker is
`gt6tools gttools unpack` (`/mnt/ssd/gt6/tools/m0_unpack2.sh`), which targets a **PS3** `GT.VOL` and
is preceded by a decryption step (`m0_volcrypto.sh`, `disc_decrypted`). **GT4 is PS2**, and its
volume is a different, simpler container — magic `0xacb990ad`. That tool will not read it.

**How far the PS2 volume format is understood** (measured, partial):

- Header: `magic 0xacb990ad`, then `0x00020002`, two size/offset words, and a **count of 23**
  top-level entries at `+0x18`.
- The entry table is a list of **24-byte records**. The first record, at `+0x78`:

  ```
  +0x00 63 ba 00 01   0x0100BA63   absolute offset, high bit set
  +0x04 04 00 00 00   type 4
  +0x08 14 00 00 00   20
  +0x0C 04 05 00 00   0x504   vol-relative
  +0x10 14 05 00 00   0x514
  +0x14 30 05 00 00   0x530
  ```

  with types 2, 3 and 4 all present. **The record semantics and the name field are not yet
  decoded**, so a full walk is not yet possible. That is the single remaining piece.

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
NEED:    ONE thing now, not two. A decoder is NO LONGER the blocker: a working
         vgmstream-cli r2117 exists at
         /mnt/ssd/gt6/tools/vgmstream/build/cli/vgmstream-cli, and the sibling project has proved
         the exact pipeline (unpack VOL -> vgmstream-cli -> audio). The sole remaining unknown is
         the PS2 volume directory format: magic 0xacb990ad, 23 top-level entries, 24-byte records
         whose type codes and name field are not yet decoded. A dish that finishes that parser
         should produce a real GT4 sound with the tool already on disk.
         Note the audio itself is probably NOT .sgb -- GT4 has no SGDP, per section 2.
```
