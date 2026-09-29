# DISC-MAP — what is actually on the Gran Turismo 4 disc

**Goal:** G0.2 — GT4's executable on the slab. State, with numbers, what the disc contains.

Every number below was measured on the build box (**Cortex**, Ubuntu 24.04, x86_64) with the
commands shown. The toolchain came from `docs/TOOLCHAIN.md`; the analyzer is
`/mnt/ssd/vulcan4-build/ps2xAnalyzer/ps2_analyzer`, built in goal G0.1.

**No game data is in this repository.** All extraction happened in `/mnt/ssd/gt4/work/`, outside
the repo. The hashes below identify the build; they are not a redistribution.

---

## 1. THE DISC

**Source:** `/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso` — **5,314,478,080 B (4.95 GiB)**
(the 4.95 GB the game shipped on).

ISO9660 identity, from `7z l`:

| Field | Value |
|---|---|
| System | `PLAYSTATION` |
| Volume label | **`GRANTURISMO4`** |
| Publisher | `SCEA` |
| Preparer | `POLYPHONY DIGITAL INC.` |
| Copyright | `SCEI` |
| Created | 2005-01-26 21:59:28 |
| Files / folders | 136 files, 9 folders |

Boot descriptor (`SYSTEM.CNF`, extracted):

```
BOOT2 = cdrom0:\SCUS_973.28;1
VER = 2.00
VMODE = NTSC
```

`BOOT2` names `SCUS_973.28` as the **one executable the disc boots**. The runtime then pulls in
`IOPRP300.IMG` and drivers from `IRX\`.

### The four root files that matter

| File | Size (B) | Share of disc | What it is |
|---|---:|---:|---|
| **`GT4.VOL`** | 2,459,502,592 | 46.3 % | the data container — see §5 |
| **`CORE.GT4`** | 2,020,861 | 0.038 % | opaque high-entropy blob — see §5.3 |
| `IOPRP300.IMG` | 278,465 | 0.005 % | IOP module bundle (IOPRP300) |
| **`SCUS_973.28`** | **273,020** | **0.0051 %** | **the PS2 ELF — the recompilation target** |

`GT4.VOL` is **9,009× larger** than `SCUS_973.28`. That ratio is the whole story of this dish, and
§5 is where it gets answered.

The rest of the disc is `IRX\` (27 IOP drivers: PADMAN, MCMAN, LIBSD, DEV9, USBD, INET, …),
`EPSON\` (printer driver resources, the largest files after `GT4.VOL`, ~4.6 MB each), `NET\`, and
`SYSTEM.CNF`. None of it is game logic.

---

## 2. THE EXECUTABLE — identity

Work directory: `/mnt/ssd/gt4/work/` (outside the repo, per the game-data rule).

```bash
mkdir -p /mnt/ssd/gt4/work && cd /mnt/ssd/gt4/work
7z x -y -o. "/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso" \
      "SCUS_973.28" "CORE.GT4" "SYSTEM.CNF"
sha256sum SCUS_973.28 CORE.GT4
```

```
f8f10823160e2b5cef5c7032628134632b291b1df87a9aee3c144794dd8019fa  SCUS_973.28
85d26aa8430154967b2633eede929286694ac39e99762527edcec365fd642ff9  CORE.GT4
```

> ### 🔑 `f8f10823160e2b5cef5c7032628134632b291b1df87a9aee3c144794dd8019fa` is GT4 v2.00 US.
>
> This is the **identity of the build we are recompiling**. Every result in this document, and
> every result in every later dish, is tied to this one hash. If a future run sees a different
> hash, it is a different disc and different results are expected.

### ELF header

```bash
mips-linux-gnu-readelf -h SCUS_973.28
```

| Field | Value |
|---|---|
| Class | ELF32 |
| **Data** | **2's complement, little endian** |
| Type | `EXEC` (Executable file) |
| Machine | MIPS R3000 (R5900 EE) |
| **Entry point** | **`0x01000008`** |
| Program headers | 3 |
| Section headers | 14 |
| Flags | `0x20924001` — `noreorder, 5900, eabi64, mips3` |
| Symbols | **0** (stripped) |

The binary is **stripped** — no symbol table survives. Everything below is recovered by the
analyzer's own analysis, not by reading names from the file.

---

## 3. THE SECTIONS

```bash
mips-linux-gnu-readelf -S -W SCUS_973.28
```

| # | Name | Type | Addr | Size (B) | Flags | Notes |
|---|---|---|---|---:|---|---|
| 1 | `.text` | PROGBITS | `0x01000000` | **187,408** | AX | **the code** — 46,852 instructions |
| 2 | `.ctors` | PROGBITS | `0x0102DC10` | 24 | WA | |
| 3 | `.dtors` | PROGBITS | `0x0102DC28` | 20 | WA | |
| 4 | `.reginfo` | MIPS_REGINFO | `0x0102DC3C` | 24 | A | |
| 5 | `.data` | PROGBITS | `0x0102DC80` | 37,096 | WA | initialised data |
| 6 | `.rodata` | PROGBITS | `0x01036D80` | 37,880 | A | **includes the strings §5.2 relies on** |
| 7 | `.rdata` | PROGBITS | `0x01040200` | 2,688 | WA | |
| 8 | `.eh_frame` | PROGBITS | `0x01040C80` | 2,388 | WA | C++ unwind tables |
| 9 | `.gcc_except_table` | PROGBITS | `0x01041600` | 292 | WA | C++ exception tables |
| 10 | `.sbss` | NOBITS | `0x01041780` | 56 | WAp | small BSS |
| 11 | `.bss` | NOBITS | `0x01041800` | 65,964 | WA | uninitialised data |
| 12 | `.mdebug.eabi64` | PROGBITS | — | 0 | — | empty |
| 13 | `.shstrtab` | STRTAB | — | 131 | — | section-name table |

Loadable segments:

| Segment | VirtAddr | FileSiz | MemSiz | Flags |
|---|---|---:|---:|---|
| REGINFO | `0x0102DC3C` | 24 | 24 | R |
| LOAD (text) | `0x01000000` | 187,444 | 187,444 | **RWE** |
| LOAD (data) | `0x0102DC80` | 80,548 | 147,244 | RW |

**The first 273,020 bytes of the file, and the whole of `.text`, are 187,408 bytes — 46,852
instructions.** Hold that number; §5 is about it.

`.eh_frame` and `.gcc_except_table` being present confirms this is a **C++ program** built with GCC
(`eabi64`, `-mips3`, `noreorder`).

---

## 4. THE ANALYZER'S OUTPUT

```bash
cd /mnt/ssd/gt4/work
/mnt/ssd/vulcan4-build/ps2xAnalyzer/ps2_analyzer SCUS_973.28 gt4.toml
```

**Exit code 0. It ran clean on a real retail disc — no patch was needed.**

Its own report, verbatim:

```
SCE symbol DB: embedded
Extracted 707 functions
Extracted 0 symbols
Extracted 14 sections
Extracted 0 relocations
Discovered 337 SCE SDK symbol match(es) from embedded database
- renamed 207 existing function(s)
- added 130 function(s)
Found entry point from ELF header: 0x1000008 in function sub_01000008 (starts at 0x1000008)
Found entry call to: _InitSys at 0x10001e8
Found entry call to: sub_0101F6A0 at 0x10001f0
Found entry call to: sub_01009098 at 0x10001fc
Found entry call to: sub_01000558 at 0x1000210

- 136 library functions to stub
- 197 detected library functions without runtime handlers
- 0 potential patches identified
- 2 jump tables detected
Generated TOML configuration: gt4.toml
```

### The numbers

| Metric | Value |
|---|---|
| **Functions discovered** | **707** |
| Symbols in ELF (stripped) | 0 |
| Sections | 14 |
| Relocations | 0 (fully linked) |
| SCE SDK symbols matched | 337 |
| — of which *renamed* an existing guess | 207 |
| — of which *newly added* | 130 |
| Functions to stub (SCE libs) | 136 |
| Library funcs with **no runtime handler** | **197** ⚠️ |
| Jump tables detected | 2 |
| Entry point | `0x01000008` |

**707 functions across 187,408 bytes of `.text`** = an average of **265 bytes ≈ 66 instructions
per function**. That is a *dense, real* function mix — not a stub, and not padding.

Artefacts on disk: `/mnt/ssd/gt4/work/gt4.toml` (16,975 B, 551 lines) and
`/mnt/ssd/gt4/work/analyzer.log` (301,902 B). Both outside the repo.

### What the 197 are

`gt4.toml`'s `stubs = [...]` list names the 136 library functions the runtime must intercept —
`sceCd*`, `scePad*`, `sceMc*`, `sceScf*` and so on. The remaining **197 detected library functions
have no runtime handler yet**: they are named and identified, but `ps2xRuntime` does not implement
them. That is a concrete, countable backlog for goal **G1.1** ("every unimplemented call named, not
silently stubbed") — this is the list of what is still missing.

### 2 jump tables

Both at concrete addresses, so G0.3 ("read one function as text") has real material:

- `0x0103D6D0` — dense, targets climb `0x100CDE0`, `0x100CDE8`, `0x100CDF8`, `0x100CE20`… (a
  `switch`, ~24+ cases collapsing to a few shared handlers)
- `0x0103DB50`

### The entry point region

The analyzer reports the first four calls out of the entry path. The bytes there are ordinary
R5900 arithmetic, not a stub:

```
10101e8:  00a43021  addu  a2,a1,a0
10101ec:  2442ffff  addiu v0,v0,-1
10101f0:  02a6382a  slt   a3,s5,a2
10101f4:  10e000e9  beqz  a3,0x101059c
10101f8:  afa2058c  sw    v0,1420(sp)
```

---

## 5. WHERE DOES GT4'S BULK CODE LIVE?

This is the question the dish exists to answer, because a wrong answer poisons every later goal.
**Short answer: `SCUS_973.28` is not a thin loader — it is a substantial, complete program — and
the bulk of the disc is data, not code. But `SCUS_973.28` alone is still nowhere near a whole
Gran Turismo 4. The remaining code is not on this disc in a form we can currently execute.**

Here is the evidence, and what would falsify each claim.

### 5.1 `SCUS_973.28` is a real program, not a boot stub

**Evidence.**

- **187,408 bytes of `.text` = 46,852 instructions.** A loader that hands off to somewhere else
  would be a few KB, not 184 KB.
- **707 functions at an average of 66 instructions each** — the analyzer recovered real function
  boundaries and real internal call structure, and found 2 jump tables (`switch` dispatch at
  `0x0103D6D0`, `0x0103DB50`).
- **It is C++ with exceptions**: `.eh_frame` (2,388 B) and `.gcc_except_table` (292 B) are
  present, and `.gcc_except_table` is only emitted for a real C++ build.
- **The entry point is not a jump to elsewhere.** `0x01000008` is 8 bytes into `.text` and opens
  with the standard MIPS register-init preamble:

  ```
  1000008:  70000c28  padduw  at,zero,zero
  100000c:  70001428  padduw  v0,zero,zero
  1000010:  70001c28  padduw  v1,zero,zero
  ```

  These zero the argument registers, then real code runs. It does not trampoline out.
- **It calls `_InitSys` at `0x01001E8`** — the SCE SDK C runtime initialiser. This is a
  statically-linked C/C++ program with the SDK baked in, which is exactly why 136 SCE functions
  are already named in `gt4.toml`.

**Falsifier:** if we recompiled these 707 functions and got a program that immediately jumped out
of the address range, §5.1 would be wrong. We have not run them yet — G1.1 does that — so this
claim is *"the structure says real code"*, not *"we have watched it run"*. Marked honestly.

**So: 707 functions / 187 KB is NOT a boot stub.** The dish's suspicion that `SCUS_973.28` is
"only a loader" is **refuted by measurement**.

### 5.2 The executable contains a *file-loading subsystem*, and names `core.gt4`

Recovered from `.rodata`/`.data`:

```bash
mips-linux-gnu-objcopy -O binary --only-section=.rodata --only-section=.data SCUS_973.28 rodata.bin
strings -n 4 rodata.bin
```

Disc paths it knows about:

```
cdrom0:\
cdrom0:\IOPRP300.IMG;1
cdrom0:\IRX\
core.gt4          <-- the only game-data file SCUS_973.28 names
rom0:ROMVER
rom0:UDNL
```

And a C++ class hierarchy, visible as Itanium-mangled RTTI name strings in the stripped binary:

```
Q211ImageLoader6Source
Q211ImageLoader10DiskSource
Q211ImageLoader10HostSource
Q211ImageLoader11MCardSource
Q211ImageLoadert12MCardSource_2Ui0Ui0
```

That is `ImageLoader` with a `Source` abstraction over **Disk / Host / MCard**. The adjacent
strings name the concrete paths: `core.gt4`, `DISK`, `MCARD 0`, `MCARD 1`, `HOST`, `hot`. So the
executable has a general "load a file from disc, host, or memory card" facility, and `CORE.GT4` is
a file it opens.

**This is the strongest single piece of evidence in this section, and it cuts against the simple
story.** A pure boot loader would not carry a source-abstracted image loader.

**Falsifier:** if `ImageLoader` turned out to be only for textures and never for code, this would
say nothing about where *code* lives. I have not traced its call graph — that is real work and
G0.3 is the dish for it. Stated as a lead with evidence, not a conclusion.

### 5.3 `CORE.GT4` — 2 MB, and it is not what it looks like

| Measurement | Value | Meaning |
|---|---:|---|
| Size | 2,020,861 B | |
| **Shannon entropy** | **7.9609 bits/byte** | out of 8.0 — **encrypted or compressed** |
| Distinct byte values | 256 / 256 | every byte value appears |
| `\x7fELF` magic anywhere | **none** | **no ELF inside** |
| zlib / gzip / LZMA / bzip2 / xz magic | **none found** | **not a standard compression** |

For contrast, the same measurement on the real ELF: **6.1061 bits/byte**. Native MIPS code sits
near 6; `CORE.GT4` sits at 7.96, which is indistinguishable from random.

`file` reports it as *"PDP-11 UNIX/RT ldp"* — **that is a false positive**, a coincidental byte
match, not a real identification. The data has no structure visible at the head
(`01 01 cc 5e 5d 00 ec bd …`).

**Conclusion: `CORE.GT4` is an opaque, high-entropy container — encrypted or non-standard
compressed. It is not MIPS code we can read, and it is not a standard archive we can open with
off-the-shelf tools.**

**Falsifier / what would settle it:** finding the routine in `SCUS_973.28` that opens `core.gt4`
and reading the key or transform would turn this from "opaque" into "decodable". That is exactly
the G0.3 work. Until then the honest verdict is **REFUSED, NOT GUESSED**: 2 MB of the disc is
understood only as *"something the game loads and we cannot yet read."*

### 5.4 `GT4.VOL` — 2.29 GiB, and it is a structured table, not raw noise

**Evidence.** The head of the file is **not** high-entropy in the way `CORE.GT4` is. Read as
little-endian 32-bit words, it is a clean, repeating record structure:

```
header: 0xacb990ad  0x00020002  0x0000e9a1  0x0000ba60  0x00002f41

0x000014  tag=0x0100ba61  count=23    vals=[0, 120, 144, 160, 180, 204, 296, 564, ...]
0x000078  tag=0x0100ba63  count=4     vals=[20, 1284, 1300, 1328]
0x000090  tag=0x0100ba6d  count=2     vals=[20, 1344]
0x0000a0  tag=0x0100ba71  count=3     vals=[20, 1636, 2484]
0x0000b4  tag=0x0100ba75  count=4     vals=[20, 3332, 3348, 3364]
0x0000cc  tag=0x0100ba7f  count=21    vals=[20, 3380, 3396, 3412, 3428, ...]
0x000128  tag=0x0100ba86  count=65    vals=[20, 3716, 3732, 3748, 3764, ...]
0x000234  tag=0x0100ba8a  count=3     vals=[20, 4736, 4752]
...
```

A 6-dword header, then a **sequence of records, each `[tag:u32][count:u32][count × u32]`**. The
tags are dense and ascending (`0x0100ba61`, `0x0100ba63`, `0x0100ba6d`, `0x0100ba71`, `0x0100ba75`,
`0x0100ba7f`, `0x0100ba86`, `0x0100ba8a`, …), the counts vary per record, and the first value of
most records is **20** — a 20-byte header on the entry that follows. The payload dwords are
**ascending offsets** into the volume.

So `GT4.VOL` is an **index/directory followed by the data it points at**, and the entries carry a
uniform 20-byte header. That is a genuine container format, decoded from the bytes, not a guess.

But: **no `zlib` stream exists in its first 8 MB** (0 hits at every byte offset), and the bulk
entropy is **7.8818 bits/byte**. So the *payload* is compressed or encrypted by something that is
not stock zlib, even though the *index* is plaintext and legible.

**Falsifier:** a full walk of the record chain producing sizes that sum to ~2.29 GiB would confirm
the index accounts for the whole volume. I sampled the first 32 KB, so treat the record layout as
**solid on the head, unverified on the tail.**

### 5.5 The disc is 4.95 GiB but the ISO9660 filesystem is only 2.5 GiB

`7z` reports `Physical Size = 2,676,342,784` with `Tail Size = 2,638,135,296` — it flags **"There
is data after the end of archive"**. And sampling that region shows it is **not padding**:

```
0x09f85c000  (at the 7z boundary)   nonzero  0/32   all zeros
0x0a0eebb00  (+24 MB)              nonzero 31/32   31758d00d5ae856dd91d171b64bb6822
0x0ee6b2800  (~3.7 GB)             nonzero 32/32   7080ebf856701a938e63b37b51ca638e
0x13be79500  (near end)            nonzero  0/32   all zeros
```

The zero regions are the 2 GiB DVD alignment padding. The **non-zero regions past 2.5 GiB are real
data**, and the ISO9660 PVD's volume-space-size field (big-endian, per the spec, ≈2,282 GiB — an
unreliable DVD value) does not describe them. **Roughly 2.4 GB of this disc lies outside the
ISO9660 filesystem and is not accounted for by `GT4.VOL`.**

**We do not know what that is.** Candidates — DVD-specific backup structures, or a second data
layer the game reads by raw LBA — were **not** tested. Recording it as a known unknown, because a
2.4 GB unaccounted region is exactly the kind of thing that hides a bulk-code answer.

### 5.6 VERDICT

**`SCUS_973.28` is a real, substantial, statically-linked C++ program — 46,852 instructions across
707 functions — and not merely a boot loader. The bulk of the disc is *not* code in any form we can
currently execute: `GT4.VOL` (2.29 GiB) is a plaintext-indexed container whose payload is
compressed or encrypted by something non-standard, and `CORE.GT4` (2 MB) is opaque at 7.96
bits/byte with no ELF inside it.**

The honest shape of the answer is a **negative** one, and it is the useful kind:

1. **The 273 KB is not a stub.** Refuted, with 187 KB of `.text` and 707 functions behind it.
2. **The engine is not sitting in the open as MIPS.** Neither `CORE.GT4` nor `GT4.VOL`'s payload
   yields MIPS to our tools. If the "real GT4" exists as executable code, it is inside those
   containers, behind a format we have **not** solved.
3. **Therefore recompiling `SCUS_973.28` recompiles the game's own statically-linked program
   including the SCE SDK and the game's C++ core — not a thin shim.** That is genuinely good news
   for a recomp: this is the real code, not a loader stub pointing elsewhere.
4. **But it is very unlikely to be the whole game**, and the missing part is behind a
   decode step that is now the project's critical path.

**What would change this answer — stated in advance, so the next dish cannot quietly rewrite it:**

- Finding MIPS code inside `CORE.GT4` or a `GT4.VOL` entry → §5.6.2 is wrong, the engine is on the
  disc as code, and the containers are archives rather than ciphers.
- Decrypting `CORE.GT4` (key or transform near the `core.gt4` open call) → §5.3 becomes solvable
  and the 2 MB becomes readable.
- A full `GT4.VOL` index walk summing to ≫2.29 GiB → §5.4 is incomplete; something else is in
  there.
- Explaining the 2.4 GB outside the ISO9660 filesystem → §5.5.

**The single most valuable next step** is to read the function that opens `core.gt4` and find out
what it does with the 2 MB. That is goal **G0.3** and it now has a specific target.

---

## 6. REPRODUCING THIS DISH

```bash
mkdir -p /mnt/ssd/gt4/work && cd /mnt/ssd/gt4/work
ISO="/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso"

# identity
7z x -y -o. "$ISO" "SCUS_973.28" "CORE.GT4" "SYSTEM.CNF"
sha256sum SCUS_973.28          # -> f8f10823...8019fa  (GT4 v2.00 US retail)
cat SYSTEM.CNF                 # -> BOOT2 = cdrom0:\SCUS_973.28;1

# shape
mips-linux-gnu-readelf -h  SCUS_973.28
mips-linux-gnu-readelf -S -W SCUS_973.28
mips-linux-gnu-readelf -l  -W SCUS_973.28

# the analyzer (needs G0.1's toolchain)
/mnt/ssd/vulcan4-build/ps2xAnalyzer/ps2_analyzer SCUS_973.28 gt4.toml
#   -> 707 functions, 14 sections, entry 0x1000008, 337 SCE symbols, 2 jump tables

# what the executable says it opens (§5.2)
mips-linux-gnu-objcopy -O binary --only-section=.rodata --only-section=.data \
        SCUS_973.28 rodata.bin
strings -n 4 rodata.bin | grep -iE "core|\.vol|cdrom|\.irx"

# CORE.GT4 is opaque (§5.3)
python3 -c "
import math,collections
d=open('CORE.GT4','rb').read(2000000)
c=collections.Counter(d); n=len(d)
print('entropy', round(-sum(v/n*math.log2(v/n) for v in c.values()),4), 'bits/byte')"
grep -c $'\x7fELF' CORE.GT4 || echo "no ELF magic inside"

# GT4.VOL is an indexed container (§5.4)
7z x -so "$ISO" GT4.VOL 2>/dev/null | head -c 8388608 > vol_head.bin
xxd -l 64 vol_head.bin
```

Requires: `p7zip-full`, `binutils-mips-linux-gnu` (for `readelf`/`objdump`/`objcopy`; note
`mips-linux-gnu-*` defaults to **big-endian**, and the PS2 is **little-endian** — see the
correction in `docs/TOOLCHAIN.md` §5).

---

## 7. G0.2 VERDICT

| Requirement | Status |
|---|---|
| Executable on the slab, SHA-256 recorded | ✅ `f8f10823…8019fa`, v2.00 US retail |
| Section list with addresses and sizes | ✅ §3 — 14 sections, 187,408 B of `.text` |
| Entry point | ✅ `0x01000008` |
| Function count from the analyzer | ✅ **707** (`gt4.toml`, 551 lines) |
| Analyzer output on disk | ✅ `gt4.toml` + `analyzer.log` |
| Where does the bulk code live? | ✅ §5.6 — answered, with falsifiers |
| Commands recorded | ✅ §6 |

**G0.2 is met.** The disc is mapped, the executable is identified and analysed, and the bulk-code
question has a measured answer rather than a guess.

### Carried forward, in priority order

1. **The containers are the critical path.** `CORE.GT4` (7.96 bits/byte) and `GT4.VOL`'s payload
   (7.88 bits/byte) are both opaque. Until one yields, we cannot know whether GT4 is fully
   recompilable from this disc.
2. **197 library functions have no runtime handler.** A named, countable backlog for G1.1.
3. **`SCUS_973.28` has never been executed.** 707 functions analysed; zero run. §5.1's "real
   program" claim is structural, not observational. G1.1 is where that stops being an assumption.
4. **~2.4 GB of the disc lies outside the ISO9660 filesystem** and is unaccounted for (§5.5).

### Licence note

This document contains **only facts and measurements** about a disc the user owns: sizes, hashes,
section names, offsets, entropy figures, and strings. **No game code, no asset, and no extracted
content is reproduced here or in this repository.** Research and preservation intent. Not
affiliated with Sony Interactive Entertainment or Polyphony Digital.
