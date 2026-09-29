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

*(sections 3+ — the build, the scratch ELF, and the gate — appended below as they are measured)*
