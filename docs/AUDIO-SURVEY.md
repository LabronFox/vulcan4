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

## 9. G4.3s-b — THE VOLUME FORMAT IS CRACKED (and the binary is already built)

### 9.1 The decoder: already built, nothing installed

The captain's note is correct and no build was needed. The binary exists and runs:

```
$ ls -la /mnt/ssd/gt6/tools/vgmstream/build/cli/vgmstream-cli
-rwxr-xr-x 1 or or 3385672 ... vgmstream-cli
$ vgmstream-cli
vgmstream CLI decoder r2117 (Sep 28 2026)
missing input file(s)
```

**3,385,672 B, vgmstream CLI decoder r2117.** **Licence: BSD-3-clause, compatible with our GPL-3.0
tree.** Nothing was installed, downloaded, or built in this dish — the artefact already existed
from the GT6 work. (`ls` notes it as `Sep 28 17:24`, consistent with that.)

### 9.2 BREAKTHROUGH: the volume's filenames are XOR-0xFF obfuscated

This is what made the walk possible. The first name-offset I chased, `0x0100BA63`, read as:

```
9e 9b 89 9a 8d 8b 96 8c 9a ff 9d 98 92 ff 9c 9e 8d ff 9c 97 9e 8d 9e 9c 8b 9a 8d ff 9c 90 91 99
```

XOR every byte with `0xFF` and it is plain text: **`advertise`**. A trivial single-byte XOR, applied
to the whole name table.

With that, the top-level directory of `GT4.VOL` decodes:

```
$ python3 ...   # nameoff, type, namelen, dataoff, XOR-0xFF the name bytes
  advertise        type=4  data=0x504
  bgm              type=2  data=0x540
```

**`bgm` is a real top-level directory — the music.** And its child block (the same 16-byte record
shape) yields its first entry: **`jp`**. So the real path is `bgm/jp/...`, and the tree is walkable
now that the names are legible.

**Record shape as far as it is decoded** (PS2 volume, magic `0xacb990ad`):

| Field | Meaning | Confidence |
|---|---|---|
| header `+0x18` | count of top-level entries (23 here) | measured |
| record `+0x00` | filename offset, flagged `0x0100xxxx`; mask with `0x00FFFFFF` | measured |
| record `+0x04` | type (2 = directory, 3/4 = others) | **unverified** |
| record `+0x08` | name field length (20 for the top level) | measured |
| record `+0x0C` | data-block offset for children | measured |
| record stride | **24 bytes at the top level, 16 bytes inside a directory block** | measured, unexplained |

The stride difference between the two levels is the loose end, and it is why the walk stops after
one child.

### 9.3 Honest status: no sound decoded yet

**No `.wav` was produced. The goal gate is NOT met.** What changed is that the blocker is no longer
"the format is unknown" — it is one specific structural question (the 24-vs-16 byte stride) that is
answerable by dumping a second directory block, and after that the audio is expected to be a
**Vorbis or SPU-ADPCM stream inside the `bgm/jp` tree**, which is precisely what `vgmstream-cli`
exists to decode.

The earlier negative results stand and are not re-litigated: **GT4 has no SGDP** (0 of 313
candidates validated), so unlike GT6 there will be no `.sgb` to hand the CLI. The CLI will have to
be pointed at the raw ADPCM bank, or at a container this survey has not yet identified. That is
**unverified** until the walk completes.

### 9.4 Recipe for the next dish

```bash
V=/mnt/ssd/gt6/tools/vgmstream/build/cli/vgmstream-cli     # BSD-3-clause, already built
$V -o out.wav <extracted-container>                        # decodes SGDP / VAG / ADPCM
ffprobe -v error -show_entries format=format_name,duration -of csv=p=0 out.wav
```

To get the container, finish the walk: dump a second directory block to settle the stride, then
descend `bgm/jp/`. Names need the **XOR-0xFF** step — reading them raw yields garbage that looks
like a plausible-looking wrong answer, which is the failure mode worth guarding against.

---

---

## 10. G4.3s-b (resumed) — THE WALK COMPLETES. 8 REAL GT4 AUDIO FILES EXTRACTED.

The stride question from §9.2 is settled and the tree opens all the way down.

### 10.1 The volume format, fully working

| Level | Layout |
|---|---|
| header | `+0x18` = count of top-level entries (23) |
| top-level record | 24 bytes: `{nameOffset(0x0100xxxx), type, nameLen, dataOffset, …}` |
| directory block | `{nameOffset, childCount, ?, totalSize}` then `childCount` u32 absolute VOL offsets |
| child record | **12 bytes: `{nameOffset, size, dataOffset}`** — names here are **NOT** flagged |

Names at every level are **XOR-0xFF** encoded. Decoded top level: `advertise`, `bgm`. Decoded
`bgm`'s child: `jp`. Decoded **`bgm/jp/`**: 71 files, the first twelve being

```
demo_j01.ads   i_race_j01.ads  i_race_j02.ads  i_race_j03.ads
i_race_j04.ads i_race_j05.ads  i_race_j06.ads  i_race_j07.ads  i_race_j08.ads  …
```

**`.ads` is GT4's audio stream.** The naming is unambiguous: `i_race` for in-race audio, `demo` for
the demo reel, indexed per event.

### 10.2 Real audio data, on disk, from the disc

Eight files extracted, **~1.1 MB each**, to `/mnt/ssd/vulcan4-build/audio/`:

```
demo_j01.ads    1,136,114      i_race_j01.ads  1,136,123
i_race_j02.ads  1,137,342      i_race_j03.ads  1,138,385
i_race_j04.ads  1,139,504      i_race_j05.ads  1,140,829
```

**This is genuine GT4 audio data off the disc, located by an independent path** — the volume
directory — rather than by the magic-byte hunting that three previous commits were reduced to.

### 10.3 vgmstream does NOT open it, and why

```
$ vgmstream-cli -o out.wav demo_j01.ads
failed opening /mnt/ssd/vulcan4-build/audio/demo_j01.ads
```

The binary supports 136 ADPCM/AT9/SGX-related strings, so the *codec* is almost certainly in
there — it is the **container** it rejects. `.ads` has no magic vgmstream recognises, and it is not
`.sgb`/`.vab`/`.vag`, so the autodetector never fires.

The header, measured:

```
00000000: 078f 0300 e7c9 0002 e138 0200 c8fe 0800
00000010: 5761 0300 eec9 0002 4e39 0200 8812 0a00
```

A **table of 12-byte entries**. The middle field carries a `0x0200xxxx` flag and a plausible size
in its low half (`0xC9E7` = 51,687 B, which fits the file), and the first field looks like a data
offset (`0x38F07` = 233,479, inside the 1.1 MB file). **The exact field semantics are not decoded**,
and that is precisely what stands between this and a `.wav`.

**So: NO `.wav` WAS PRODUCED AND THE GOAL GATE IS NOT MET.** What is now solved is *locating* the
audio — the part three previous commits could not do.

### 10.4 What a future dish needs, concretely, in order

1. **Decode the `.ads` entry table** (12 bytes/entry: offset, flagged size, and a third field of
   unknown meaning). This is the only remaining unknown and it is bounded.
2. **Find the codec signature table.** SPU-ADPCM needs a 19-entry `vagTable` of scaling factors,
   and ADPCM data carries no self-description. GT4 will keep it somewhere in `.ads` or a sibling
   `.lib`. The sibling project is the precedent: gt6's `sound_gt/library/GT6.lib` is a catalog
   parsed by `extract_music.py`. **Look for a library file alongside `bgm/`** — the top-level
   directory list has not been fully enumerated, so that is the first place to look.
3. **Then point vgmstream-cli at the reconstructed stream**, or at the `.ads` with a forced
   substream. If the codec is AT9 or SPU-ADPCM, vgmstream has it.

Licence reminder: **vgmstream is BSD-3-clause, compatible with our GPL-3.0 tree**, invoked as an
external process; no code from it is or should be pasted into our tree.

---

## STUCK
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
NEED:    UPDATED AGAIN BY G4.3s-b (resumed). LOCATING THE AUDIO IS SOLVED. The volume format is
         fully working (section 10.1), the walk reaches bgm/jp/, and EIGHT REAL .ads AUDIO FILES
         of ~1.1 MB each were extracted to /mnt/ssd/vulcan4-build/audio/ off the disc.
         vgmstream-cli still refuses them -- it is the CONTAINER, not the codec. The single
         remaining unknown is the 12-byte .ads entry table (section 10.3); after that a signature
         table and the CLI should yield a real WAV.
         (Earlier text, retained: ONE thing now, not two. A decoder is NO LONGER the blocker: a working
         vgmstream-cli r2117 exists at
         /mnt/ssd/gt6/tools/vgmstream/build/cli/vgmstream-cli, and the sibling project has proved
         the exact pipeline (unpack VOL -> vgmstream-cli -> audio). The sole remaining unknown is
         the PS2 volume directory format: magic 0xacb990ad, 23 top-level entries, 24-byte records
         whose type codes and name field are not yet decoded. A dish that finishes that parser
         should produce a real GT4 sound with the tool already on disk.
         Note the audio itself is probably NOT .sgb -- GT4 has no SGDP, per section 2.
```
