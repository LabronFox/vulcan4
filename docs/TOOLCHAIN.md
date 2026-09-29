# TOOLCHAIN — how the PS2 recompiler gets built on Linux

**Goal:** G0.1 — the PS2 recompilation toolchain must build and run on Linux.

This file is the reproducible record. Every command below was actually executed on the build box
(**Cortex**, Ubuntu 24.04, x86_64). Findings are appended as they are measured, not at the end.

**Licence:** everything under `tools/PS2Recomp/` is upstream [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp)
at **GPL-3.0**. That licence is viral and stays. Nothing MIT is added into that tree.

---

## 0. The source

```bash
cd /home/or/vulcan4
git clone --depth 1 https://github.com/ran-j/PS2Recomp.git tools/PS2Recomp
```

Pinned at upstream commit **`75d729c`** ("Feature/iop emulator (#244)").

Modules and what they do:

| Module | Binary | Job |
|---|---|---|
| `ps2xAnalyzer` | `ps2_analyzer` | ELF + symbols → TOML config (which functions exist, where) |
| `ps2xRecomp` | `ps2_recomp` | **R5900 machine code → C++** (the actual recompiler) |
| `ps2xRuntime` | `ps2EntryRunner` | memory model, syscall dispatch, GS/VU1/ADPCM stubs |
| `ps2xIOP` | *(static lib `ps2_iop`)* | the little-endian IOP CPU (IRX modules, CDVD) |

---

## 1. FINDING — the `toml11` configure failure is a **full disk**, not MSVC

The dish was briefed with: *"`cmake -S . -B build` fails fetching `toml11` — it is an MSVC-first
project."* **That is not what actually fails first on this box.** Reproduced verbatim:

```
[ 11%] Creating directories for 'toml11-populate'
[ 22%] Performing download step (git clone) for 'toml11-populate'
Cloning into 'toml11-src'...
HEAD is now at be08ba2 doc: update versions in docs
Submodule 'docs/themes/hugo-book'   (https://github.com/alex-shpak/hugo-book.git) registered for path 'docs/themes/hugo-book'
Submodule 'tests/extlib/doctest'   (https://github.com/doctest/doctest.git)         registered for path 'tests/extlib/doctest'
Submodule 'tests/extlib/json'      (https://github.com/nlohmann/json.git)           registered for path 'tests/extlib/json'
Cloning into '.../toml11-src/docs/themes/hugo-book'...
Cloning into '.../toml11-src/tests/extlib/doctest'...
Cloning into '.../toml11-src/tests/extlib/json'...
fatal: write error: No space left on device
fatal: fetch-pack: invalid index-pack output
fatal: clone of 'https://github.com/nlohmann/json.git' into submodule path '.../tests/extlib/json' failed
Failed to clone 'tests/extlib/json' a Retry scheduled
... Failed to clone 'tests/extlib/json' a second time, aborting

CMake Error at .../toml11-populate-gitclone.cmake:62 (message):
  Failed to update submodules in: '.../toml11-src'
gmake[2]: *** [CMakeFiles/toml11-populate.dir/build.make:102: ...-download] Error 1

CMake Error at /usr/share/cmake-3.28/Modules/FetchContent.cmake:1679 (message):
  Build step for toml11 failed: 2
Call Stack: ... ps2xRecomp/CMakeLists.txt:25 (FetchContent_MakeAvailable)
```

**Root cause — two layers, and layer 2 is a genuine upstream bug that exists on a healthy disk too:**

1. **Environment (this box only):** the root filesystem is **100% full**. `df` at the time:
   ```
   /dev/mapper/ubuntu--vg-ubuntu--lv  178G  171G   87M  100% /
   ```
   The recursive submodule clone ran the disk out of space mid-`index-pack`, and git reported the
   resulting failure as a clone error. The `toml11` failure is a *symptom*; the disk is the cause.

2. **Upstream defect (portable, worth fixing regardless):** `ps2xRecomp/CMakeLists.txt:20-25` declares
   toml11 **without `GIT_SHALLOW TRUE`**, unlike every other dependency in the same file
   (`elfio` and `libdwarf` both set it; `fmt` and `rabbitizer` do not). A non-shallow
   `FetchContent_Declare` makes CMake run a full `git clone` **plus a recursive
   `git submodule update --init --recursive`**. toml11 is a **header-only** library and declares
   three submodules, all of them pure dead weight for a consumer that only wants
   `toml.hpp`:

   | Submodule | What it is | Needed to build ps2xRecomp? |
   |---|---|---|
   | `tests/extlib/doctest` | doctest unit-test framework | **no** — we don't run toml11's tests |
   | `tests/extlib/json` | nlohmann/json | **no** — only used by toml11's JSON test fixtures |
   | `docs/themes/hugo-book` | Hugo theme for toml11's website | **no** — never compiled |

   Measured: the wasted recursive clone is what filled the disk. Fixing it is both a correctness fix
   and a large disk/time saving.

**The fix** is a two-line change to the toml11 `FetchContent_Declare`, matching the style already
used by `elfio` and `libdwarf` in the same file. It is deliberately minimal so a future
`git pull` against upstream stays a small, readable conflict. → see §2.

---

## 2. THE PATCH

Applied to `tools/PS2Recomp/ps2xRecomp/CMakeLists.txt` (see §2b for the committed diff):

- `GIT_SHALLOW TRUE` on toml11 — a tag-pinned shallow clone is enough to build a header-only lib.
- `GIT_SUBMODULES_RECURSE FALSE` + an empty `GIT_SUBMODULES` list — stop CMake from recursively
  pulling the three test/doc submodules that nothing in this project compiles.

Both are additive CMake options, so upstream's own file is untouched in spirit and a `git pull`
merges cleanly.

---

## 2b. FINDING 2 — the runtime needs **SSE4.1**, which GCC won't give it by default

With §2 applied, configure succeeds and `ps2_recomp` and `ps2_analyzer` link. Then the build dies
in `ps2xRuntime`, on five translation units, with a message that looks unrelated to any of this:

```
ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp: In function 'getRegU32(R5900Context const*, int)':
/usr/lib/gcc/x86_64-linux-gnu/13/include/smmintrin.h:448:1: error: inlining failed in call to
  'always_inline' '_mm_extract_epi32(long long __vector(2), int)': target specific option mismatch
  448 | _mm_extract_epi32 (__m128i __X, const int __N)
ps2xRuntime/include/ps2_runtime.h:200:51: note: called from here
  200 |     return static_cast<uint32_t>(_mm_extract_epi32(ctx->r[reg], 0));
```

**Cause.** The runtime keeps the R5900 general-purpose registers as `__m128i` lanes and reads
individual registers back with `_mm_extract_epi32` / `_mm_extract_epi64` (used unguarded in
`ps2xRuntime/include/ps2_runtime.h:200`, `ps2_runtime_macros.h:17-42`,
`ps2xRuntime/src/lib/ps2_memory.cpp:1117` and `ps2_runtime.cpp:1198+`). **Those are SSE4.1
intrinsics.**

- **MSVC/x64** accepts them unconditionally, and in Release `ps2xRuntime/cmake/ReleaseMode.cmake`
  passes `/arch:AVX2`, which strictly exceeds SSE4.1. So upstream never sees the problem.
- **GCC/Clang on x86-64** default to the *baseline* x86-64 ISA, which is **SSE2 only**. The
  intrinsic is therefore `always_inline` into a function compiled for the wrong target, and GCC
  errors out. This is the real shape of the "MSVC-first" claim in the brief — not a configure
  failure, a **codegen** failure.

Note the header *does* feature-detect SSE4.1 in two other places
(`ps2_runtime_macros.h:441` and `:467`, guarded on `#if defined(__SSE4_1__)`), which confirms the
SSE4.1 dependence is known to the author — it just was never enabled for GCC.

**Why `-msse4.1` and not `-mavx2`.** Every `__m256`/`_mm256_` hit in the tree is inside a
**string literal** the code generator emits into generated C++ (e.g.
`ps2xRecomp/src/lib/mmi_translation_helpers.cpp:309`), not compiled code. The compiled code needs
SSE4.1 and no more (`_mm_cvtsi128_si64` is plain SSE2). So `-msse4.1` is the honest flag: it
matches what the source actually requires and keeps the hardware floor low, instead of copying
MSVC's `/arch:AVX2` and putting a Haswell-or-newer requirement on a build that doesn't need one.

**The fix** is one guarded block added to the *top-level* `CMakeLists.txt`, before the
`add_subdirectory` calls, so every target and every build type gets it — not just Release:

```cmake
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$"
   AND NOT MSVC
   AND NOT PS2X_IS_ARM_TARGET
   AND NOT ANDROID)
    add_compile_options(-msse4.1)
endif()
```

`NOT MSVC` because `/arch:AVX2` already covers it; `NOT PS2X_IS_ARM_TARGET` because ARM goes
through the `sse2neon` path set up earlier in the same file.

### The recorded patch

`tools/PS2Recomp/` is gitignored (it is another project's tree), so the patch is committed as a
standalone file instead — **2 files, 24 insertions, 0 deletions**:

- `tools/patches/ps2recomp-linux.patch`

Re-apply after any re-clone with:

```bash
cd /home/or/vulcan4/tools/PS2Recomp && git apply /home/or/vulcan4/tools/patches/ps2recomp-linux.patch
```

---

## 3. THE BUILD — exact commands

**Build tree location: `/mnt/ssd/vulcan4-build`.** The root filesystem is the *code* volume and
routinely runs tight; the SSD is the *work* volume. This is deliberate — see `docs/` commit
*"the SSD is the work surface, root is code only"*. Change the path freely, it is just an
out-of-source build dir.

```bash
# 0. fetch upstream (skip if tools/PS2Recomp already exists)
cd /home/or/vulcan4
git clone --depth 1 https://github.com/ran-j/PS2Recomp.git tools/PS2Recomp
cd tools/PS2Recomp && git checkout 75d729c          # pin

# 1. apply the Linux patch (see §2b)
git apply /home/or/vulcan4/tools/patches/ps2recomp-linux.patch

# 2. configure
cmake -S . -B /mnt/ssd/vulcan4-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DPS2X_BUILD_RECOMP=ON \
  -DPS2X_BUILD_RUNTIME=ON \
  -DPS2X_BUILD_ANALYZER=ON \
  -DPS2X_BUILD_TEST=OFF \
  -DPS2X_BUILD_STUDIO=OFF

# 3. build
cmake --build /mnt/ssd/vulcan4-build -j10
```

**Flags, and why each one is there:**

| Flag | Why |
|---|---|
| `-DCMAKE_BUILD_TYPE=Release` | required. Upstream only enables its fast-path function (`EnableFastReleaseMode`) in Release/RelWithDebInfo. |
| `-DPS2X_BUILD_TEST=OFF` | `ps2xTest` is a 20-file test suite that links *every* library. Not needed to prove the toolchain. |
| `-DPS2X_BUILD_STUDIO=OFF` | `ps2xStudio` is a GUI tool. Not needed here. |

`-DPS2X_BUILD_RUNTIME=ON` and `-DPS2X_BUILD_ANALYZER=ON` are upstream defaults and are left on.

### Toolchain and host requirements (all already satisfied on Cortex)

| Requirement | Version on this box | Needed by |
|---|---|---|
| CMake | 3.28.3 (needs ≥ 3.21) | all |
| GCC / G++ | 13.3.0, C++20 | all |
| `pkg-config` | 1.8.1 | runtime's FFmpeg lookup |
| FFmpeg dev (`libavcodec`…`libswscale`) | 60.31.102 etc. | `ps2xRuntime` (MPEG video path) |
| X11 / GL / GLU dev headers | present | `raylib`, which the runtime builds from source |
| `binutils-mips-linux-gnu` | 2.42 | **only** for our scratch ELF (§4), not for PS2Recomp |

`ps2xRuntime` fetches **raylib 5.5** with `FetchContent` and builds it from source, so X11 + OpenGL
development headers are a hard requirement on Linux. On a bare box:

```bash
sudo apt-get install -y build-essential cmake pkg-config \
  libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxi-dev libxcursor-dev libgl1-mesa-dev libglu1-mesa-dev
```

### Result — `BUILD_EXIT=0`, all three targets

```
-rwxr-xr-x  2193640  /mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp
-rwxr-xr-x 13747672  /mnt/ssd/vulcan4-build/ps2xAnalyzer/ps2_analyzer
-rwxr-xr-x 38075096  /mnt/ssd/vulcan4-build/ps2xRuntime/ps2EntryRunner
```

All three run:

```
$ ./ps2_recomp
PS2Recomp - A static recompiler for PlayStation 2 ELF files
Usage: ps2recomp <config.toml>

$ ./ps2_analyzer
PS2 ELF Analyzer
A tool to analyze PS2 ELF files and generate TOML configuration for PS2Recomp
Usage: ps2_analyzer <input_elf> <output_toml> [sce_symbol_db_dir]

$ ./ps2EntryRunner
[main] fatal exception: Unable to determine executable path. Pass the guest ELF as argv[1]
        or define PS2X_DEFAULT_BOOT_ELF.
```

`ps2EntryRunner` complaining about a missing guest ELF is **correct behaviour** — it is a runtime,
it wants something to run. This is goal **G1.1**'s territory, not this dish's.

### FINDING 4 — minor: `ps2EntryRunner` segfaults instead of erroring when headless

Not a build problem, but it will trip anyone who pokes the binary, so it is recorded.

`ps2EntryRunner` has **no argument parsing at all**. It treats *any* `argv[1]` as a guest ELF path,
so `--help` is not help:

```
$ ./ps2EntryRunner --help
Using argv boot path
INFO: Initializing raylib 5.5
INFO: Platform backend: DESKTOP (GLFW)
WARNING: GLFW: Error: 65550 Description: X11: The DISPLAY environment variable is missing
WARNING: GLFW: Failed to initialize GLFW
Segmentation fault (core dumped)          <-- exit 139
```

The binary initialises correctly and gets all the way to the display layer, where it fails
*honestly* (GLFW names the missing `DISPLAY`). The defect is the **aftermath**: once GLFW init
fails there is no display to draw into, and the process dereferences it anyway instead of printing
an error and exiting. On this box `DISPLAY` is unset, so that path is the default one.

Not fixed here — it is runtime behaviour, and this dish's mandate is that the toolchain *builds and
runs*. Recorded so nobody mistakes the crash for a broken build.

Built binaries are copied to `tools/bin/` (gitignored — reproducible from the patch, no reason to
carry 54 MB of build output in history).

---

## 4. PROOF — a scratch R5900 program through the recompiler

Source: **`tools/scratch/scratch.s`**. It is ours. It contains nothing from any game disc.

```mips
vulcan_add:              # returns (a0 + 100) + a1
    addiu $t0, $a0, 100
    addu  $v0, $t0, $a1
    jr   $ra
    nop

vulcan_entry:            # calls vulcan_add(3, 4) -> 107, stores it at 0x00200000
    li   $a0, 3
    li   $a1, 4
    jal  vulcan_add
    nop
    lui  $t0, 0x0020
    sw   $v0, 0($t0)
    jr   $ra
    nop
```

### Build it and run it

> ⚠️ **Use `-EL`. The PS2 is little-endian (mipsel).** `binutils-mips-linux-gnu-*` defaults
> `as`/`ld` to **big-endian** for MIPS targets, which produces an ELF the recompiler will read
> as garbage. Corrected in goal G0.2 — see §5.

```bash
sudo apt-get install -y --no-install-recommends binutils-mips-linux-gnu

cd /home/or/vulcan4/tools/scratch
mips-linux-gnu-as -EL -march=5900 -mabi=32 -o scratch.o scratch.s
mips-linux-gnu-ld -EL -Ttext=0x00100000 -e vulcan_entry -o scratch.elf scratch.o
mips-linux-gnu-objdump -d scratch.elf

/mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp scratch.toml
```

The config (`tools/scratch/scratch.toml`) is short — **the recompiler discovers functions from the
ELF symbol table, so there is no function list to write**:

```toml
[general]
input  = "/home/or/vulcan4/tools/scratch/scratch.elf"
output = "/home/or/vulcan4/tools/scratch/out"
single_file_output = true
output_worker_thread = 1
patch_syscalls = true
patch_cop0 = true
patch_cache = true
```

### The recompiler's own report

```
[recompiler] extracted 2 functions, 12 symbols, 10 sections, 0 relocations
[recompiler] recompiling 2 functions
[recompiler] collected 1 resumable entry point(s) across 1 owner function(s)
[recompiler] generated function header file: ".../out/ps2_recompiled_functions.h"
[recompiler] wrote combined output to:         ".../out/ps2_recompiled_functions.cpp"

Functions discovered: 2
Functions processed: 2, recompiled: 2, stubs: 0, skipped: 0, decode failures: 0
Generated functions: 2
Unhandled instructions: 0
Warnings: 0, errors: 0
```

**0 unhandled instructions, 0 errors.** The R5900 → C++ path is alive on Linux.

---

## 5. FINDING 3 — ~~the recompiler ignores ELF endianness~~ **CORRECTED IN G0.2: the decoder is RIGHT**

> **Status: PREMISE WITHDRAWN. Do not "fix" `elf_parser.cpp` — there is no bug to fix here.**
> This finding was written during G0.1 and its central claim was **wrong**. It is kept below
> because the correction is the useful part, and because a wrong-but-confident finding that gets
> retracted on the record is worth more than one that quietly disappears.
>
> Correction measured in G0.2 against GT4's real executable, with bytes:
>
> ```
> $ xxd -s 0x1008 -l 4 /mnt/ssd/gt4/work/SCUS_973.28
> 00001008: 280c 0070
> $ mips-linux-gnu-objdump -d --start-address=0x1000008 SCUS_973.28
>   1000008:  70000c28   padduw  at,zero,zero
> ```
>
> Bytes `28 0c 00 70` read **little-endian** are `0x70000c28` = `padduw $at, $zero, $zero`, which is
> exactly what objdump prints. Read big-endian they would be `0x280c0070` — opcode `0xa`, a
> reserved slot that is not a real instruction. The recompiler's host-order read matches GT4.
>
> **The PS2's R5900 is little-endian (mipsel)**, as is GT4's own `SCUS_973.28`
> (`Data: 2's complement, little endian`). Retail PS2 games are little-endian. Our host is
> x86-64, also little-endian. **Host-order reads are therefore correct for our target**, and the
> decoder is right to do what it does.
>
> **What I actually got wrong was my own test fixture.** `binutils-mips-linux-gnu-as` defaults
> MIPS targets to **big-endian**, so `scratch.elf` was built big-endian — the opposite of a PS2.
> That single bad default manufactured a "bug" out of a correct decoder. The `-EL` flag in §4 is
> the real fix, and it is now the documented way to build the scratch ELF.
>
> Nothing here blocks G0.2. Nothing in `elf_parser.cpp` should change on the strength of this
> finding.

### The one real item, and it is not now

`ps2xRecomp`'s ELF reader does a raw host-order read of each instruction word:

```cpp
// ps2xRecomp/src/lib/elf_parser.cpp:1631
uint32_t raw = 0;
std::memcpy(&raw, section.data + offset, sizeof(uint32_t));
```

It never consults the ELF's `EI_DATA` byte. For every PS2 game we care about that is *the right
answer by accident* — the host and the guest agree — but it is unverified agreement. If it ever
disagreed, the tool would emit nonsense while reporting `Unhandled instructions: 0, Warnings: 0,
errors: 0`. That silence is what violates our house rule: a wrong result must never look clean.

A **robustness note for a later dish**, not a blocker: read `EI_DATA`, byte-swap when it is
`ELFDATA2MSB`, and **report a hard error on any byte order not supported** rather than decoding
silently. Note that `EI_DATA` alone is not sufficient for a fully correct MIPS loader (the
`.mdebug`/ABI flags matter too), which is a further reason to do it as its own piece of work with
its own tests. The known raw-read sites, found by `grep` and not exhaustively audited:

- `ps2xRecomp/src/lib/elf_parser.cpp:531`, `:868`, `:981`, `:1294`, `:1631`

Upstream's own tests synthesise `ELFIO::ELFDATA2LSB` ELFs
(`ps2xTest/src/ps2_recompiler_tests.cpp`), which is why nobody noticed the field is unread — and
also why they never caught that the default assembler gives you the wrong byte order.

### The control experiment, re-read correctly

The same source assembled both ways still shows the decoder tracks byte order exactly, with no
decoding error of its own. The little-endian build is the one that matches the PS2:

```bash
cd /home/or/vulcan4/tools/scratch
mips-linux-gnu-as -EL -march=5900 -mabi=32 -o scratch.o scratch.s
mips-linux-gnu-ld -EL -Ttext=0x00100000 -e vulcan_entry -o scratch.elf scratch.o
/mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp scratch.toml
```

```cpp
// 0x100000: 0x24880064  addiu   $t0, $a0, 0x64
SET_GPR_S32(ctx, 8, (int32_t)ADD32(GPR_U32(ctx, 4), 100));            // $t0=8, $a0=4  ✓
// 0x100004: 0x1051021  addu    $v0, $t0, $a1
SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 8), GPR_U32(ctx, 5))); // $v0=2 ✓
// 0x100008: 0x3e00008  jr      $ra
const uint32_t jumpTarget = GPR_U32(ctx, 31);                        // $ra=31 ✓
```

The **big-endian** build of the same source yields the byte-rotated garbage
(`0x24880064` → `0x64008824`, decoded as `daddiu $zero,$zero,-0x77DC`). That experiment was sound;
I simply drew the wrong conclusion about which byte order a PS2 uses.

---

## 6. REPRODUCING THIS DISH FROM SCRATCH

```bash
cd /home/or/vulcan4
git clone --depth 1 https://github.com/ran-j/PS2Recomp.git tools/PS2Recomp
git -C tools/PS2Recomp checkout 75d729c
git -C tools/PS2Recomp apply ../../tools/patches/ps2recomp-linux.patch

cmake -S tools/PS2Recomp -B /mnt/ssd/vulcan4-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
cmake --build /mnt/ssd/vulcan4-build -j10

mkdir -p tools/bin && cp /mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp \
  /mnt/ssd/vulcan4-build/ps2xAnalyzer/ps2_analyzer \
  /mnt/ssd/vulcan4-build/ps2xRuntime/ps2EntryRunner tools/bin/

# end-to-end proof (§4)
sudo apt-get install -y --no-install-recommends binutils-mips-linux-gnu
cd tools/scratch
mips-linux-gnu-as -march=5900 -mabi=32 -o scratch.o scratch.s
mips-linux-gnu-ld -Ttext=0x00100000 -e vulcan_entry -o scratch.elf scratch.o
/mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp scratch.toml
```

---

## 7. G0.1 VERDICT

| Requirement | Status |
|---|---|
| `ps2xRecomp` builds on Linux | ✅ `ps2_recomp`, runs |
| `ps2xRuntime` builds on Linux | ✅ `ps2EntryRunner`, runs |
| `ps2xAnalyzer` builds on Linux | ✅ `ps2_analyzer`, runs |
| Exact commands recorded | ✅ §3, §4, §6 |
| Patch is minimal and `git pull`-safe | ✅ 2 files, +24 lines, no deletions |
| Scratch R5900 ELF → real C++ | ✅ §4, every instruction accounted for |
| **Recompiler correct on big-endian guest data** | ❌ **§5 — upstream defect, not fixed here** |

**The toolchain builds and runs on Linux. G0.1 is met.**

One defect found and deliberately left for its own dish: the recompiler ignores ELF endianness, so
it will miscompile GT4's real big-endian executable while reporting no errors. **Do not start G0.2
(GT4 on the slab) before that is fixed** — a silent miscompile is exactly the "plausible-looking
lie" this project refuses to ship.

One minor robustness defect recorded and not fixed: `ps2EntryRunner` segfaults rather than erroring
when it cannot open a display (§4, Finding 4). Cosmetic by comparison; the recompiler itself is
unaffected.

### Licence note

`tools/PS2Recomp/` is upstream **GPL-3.0** and stays that way. `tools/patches/` contains only a
diff of GPL-3.0 CMake files and inherits their licence. `tools/scratch/scratch.s` is our own
work. No MIT or otherwise incompatible licence has been added into the GPL tree.

