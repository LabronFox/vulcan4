# DC-MINING — what transfers from the Dark Cloud decompilation to VULCAN 4

*Source project: `https://github.com/Adubbz/DCDecomp` — a **matching decompilation** of Dark Cloud
(PS2, Level-5). Its ELF's text+data are byte-identical to the retail disc. Built with
**MWCC/MWLD 2.3.3** (see §1.1 — the brief's "2.3.1.01" was wrong), the same Metrowerks CodeWarrior
MIPS toolchain family official GT4 was built with. That is why it is worth mining: same compiler
generation, same EE, same linker, and a finished, public, *verified* matching project to copy the
machinery from.*

**Start here:** the `## Status` block below says what is done and what is open; the
`# EXECUTIVE SUMMARY` says what changes about our plan; `# 5` is the do-not-carry list.

**This document is written incrementally. The `## Status

**MINING COMPLETE.** All four targets covered, 69 findings, every one with SCOPE / EVIDENCE /
MEANS FOR US / CONFIDENCE. Doc is 2036 lines.

- **Session:** resumed after a previous run died on a full root disk having written nothing. The
  mitigation held: the skeleton went to disk before any deep reading, and findings were appended
  per step. Nothing was lost.
- **Clone:** `/mnt/ssd/dcdecomp` (outside the repo, per project law), HEAD `418591ca`
  ("editloop and gameloop improvements"), submodules `m2c` @10deabd, `mwccgap` @b092f58,
  `mwccps2-debugger` @3bca7ed. No disc image present and none needed — nothing here required
  game data.
- **Disk:** root 79% / 36G free; SSD 113G free. **Nothing was built** (no compilation was needed for
  any finding), so the `-j2` + `nice` rule was never exercised.
- **Files written:** `docs/DC-MINING.md` only. Nothing else in the repo was touched. No installs.

### Coverage

| Target | State | Sections |
|---|---|---|
| 1. THE TOOLCHAIN RECIPE | **complete** | §1.1 compiler identity · §1.2 wibo-not-Wine · §1.3 flags · §1.4 dependency fetching · §1.5 Docker assembly · §1.6 objdiff · §1.7 splat config · §1.8 the four verdicts · §1.9 match boundary |
| 2. THE MATCHING WORKFLOW | **complete** | §2.1 leaked state · §2.2 gdb state injection · §2.3 compiler debugging · §2.4 mwccgap · §2.5 symbol index · §2.6 m2c context · §2.7 Ghidra apply/verify · §2.8 mismatch detection & repair |
| 3. HARDWARE ACCESS STRUCTURE | **complete** | §3.1 boot/IOP · §3.2 DMA + VIF1 packet grammar + GS registers · §3.3 VU sections · §3.4 VU1 contract · §3.5 timers/vsync/interrupts · §3.6 audio IOP heap |
| 4. WHAT THEY DID NOT SOLVE | **complete** | §4.1 non-reproducibility · §4.2 compiler blind spots · §4.3 register-allocation failures · §4.4 match boundary · §4.5 unexplained structures · §4.6 codegen quirk · §4.7 project shape · §4.8 open list |

### The KNOWN FIND, resolved

**ps2xRecomp's `toml11`-without-`GIT_SHALLOW` flaw does NOT apply to DCDecomp.** It has zero
`FetchContent` / `ExternalProject` / `find_package` in `CMakeLists.txt` or `cmake/*.cmake`; the only
`git clone` string in the tree is prose in `scripts/host/overlay_private.sh:26`. Dependencies arrive
by submodule SHA, exact pip version, or version-pinned binary in the Dockerfile. A *related* real
weakness was found and recorded instead: `ghcr.io/decompals/wibo:latest` unpinned, and no checksums
on the three wget'd binaries. Full detail §1.4.

### Corrections to the brief

- Compiler is **MWCC/MWLD 2.3.3**, not 2.3.1.01. Six independent references agree; a 2.3 tree also
  exists in the repo and is unreferenced by the build. §1.1
- **No Wine and no MSYS.** wibo, a native Linux PE loader, runs the Windows compiler. §1.2

### `NEED:` — open, and all four are ours to settle against GT4, not the captain's time

1. Does GT4's symbol table carry `@<n>` float-literal names? That single grep confirms or kills the
   prediction that GT4 shares MWCC's whole-program-invocation counter property — which is the
   foundation of §2.1 and therefore of whether G10 byte-exactness is even meaningful. §2.1, §2.3
2. Which MWCC build, and at what optimization level, compiled GT4? Affects §4.3 entirely — the
   register-allocation material is only reachable at `-O3`, and Dark Cloud used `-O2`.
3. GT4's own `sceGsSetDBuff` default draw environment. The game inherits TEST and ZBUF from it; if
   our stand-in guesses, every draw is wrong. §3.2
4. GT4's own boot IRX module list. Dark Cloud's nine are Dark Cloud's needs. §3.1, §5

Also open and recorded rather than asserted: whether `include/sce/libgraph.h:54-78`'s `sceGifTag`
bitfield layout is a faithful GS GIF tag or a renamed `VIFdmaPacket` layout (§4.8). Needs a
compiler probe; not resolvable from the tree. The *general* lesson — byte-matching validates only
what was executed, so reconstructed headers can be wrong in untouched fields — is high confidence
and is the one that matters for our GS lane.

### Next, if this is picked up again

- The four `NEED:` items above, in order. Item 1 is a single command and unblocks the most.
- Nothing in this document was derived from a build, a run, or game data, so **nothing here needs
  re-verification against a build**. Every file:line reference is to the checked-out tree at
  `418591ca` and is independently checkable with a single `sed`/`grep`.

# EXECUTIVE SUMMARY — what changes what we do

*Read this if you read nothing else. Ten findings, ordered by how much they should alter VULCAN 4's
plan. Full evidence for each is in the numbered sections.*

### 1. A finished PS2 game touches raw hardware memory exactly ONCE. → **G2 is an SDK, not a driver.**
The only raw MMIO address in 151k lines of Dark Cloud is EE Timer 0 at `0x10000000`, used to time a
frame (§3). Everything else goes through `sceGs*`, `sceVif1Pk*`, `sceDma*`, `sceSif*`. GT4's own
code will call **the SDK**. Our GS lane must therefore implement the *SDK surface*, because that is
the interface GT4 will use — not raw registers. This also bounds the work: the hardware contract is
small and knowable. **§3**

### 2. MWCC matching compiles are NOT REPRODUCIBLE. → **our gate must run twice.**
"Because §1.2 reads uninitialised memory, two identical runs can emit different code… one that
matches may be matching by accident." (§4.1) A byte-exact result from a compiler with this property
is evidence, not proof. **For G10: build twice, require identical bytes both times.** Cheap, and it
catches exactly the false positive the biggest project in this space cannot catch for itself. **§4.1**

### 3. GS state is a stateless register file driven in stream order. → **constrains G2's design.**
The game holds its own copies of TEST/ZBUF/ALPHA and writes them into the GS explicitly, restoring
them after every temporary override (§3.2). Our GS must take writes in stream order and remember
nothing implicitly. A scene-graph-style renderer with hidden state will produce subtly wrong images
and we will lose a week to it. **§3.2**

### 4. `sceGsSetDBuff`'s DEFAULTS are load-bearing. → **a concrete G2 `NEED:`.**
`mgPixelTest = mgDBuff.draw0.test1; mgZBuffer = mgDBuff.draw0.zbuf1;` — the game *inherits* TEST and
ZBUF from the SDK's drawing environment rather than building them (§3.2). If our stand-in fills
those with zeros or "sensible" values, **every subsequent draw is wrong**. The values must come out
of GT4's own image, not from a guess. **§3.2**

### 5. Frame timing and vsync are load-bearing, and they are not optional. → **G2 acceptance.**
The whole frame loop is driven by a vsync callback (`sceGsSyncVCallback`) incrementing a counter the
game polls, and by Timer 0 (§3.5). "Draw a picture" is not enough; the game has to believe it is
running at 60Hz. We must model EE timers at the right rate and deliver vsync **asynchronously** — the
game installs its callback behind a hand-rolled spin guard that assumes concurrency (§3.5).

### 6. The "no BIOS" bill is nine IRX modules. → **this is the real G1 cost.**
`init_all()` is recoverable in full: SIF RPC init → CD init → reboot IOP from the disc → sync →
re-init → fs reset → load nine IRX modules by name, spinning until each loads (§3.1). Every one is
IOP-side code we must replace or do without. **SIO2MAN first** (no pad, no input). Good news: the
IOP image comes off the user's disc, so we ship no IOP replacement. Dark Cloud's nine are Dark
Cloud's needs — GT4's list must come from GT4. **§3.1**

### 7. Audio goes through the IOP heap, and the heap RESETS. → **audio lane design constraint.**
`sceSifInitIopHeap()` / `AllocIopHeap` / `FreeIopHeap` bracket every buffer, and **every init resets
the heap, invalidating outstanding IOP pointers** (§3.6). Model it as a resettable allocator with
invalidation, or buffer lifetimes will be wrong in ways that look like corruption. **§3.6**

### 8. wibo, not Wine. → **removes a whole class of environment breakage.**
The Windows Metrowerks compiler is run by a native Linux PE loader, copied from a prebuilt OCI image
(§1.2). No prefix, no `wineboot`, no MSYS. If our toolchain work ever needs to execute a
Windows-hosted tool on Linux, this is the known-good path — **but pin it by digest, not `:latest`**,
which is DCDecomp's own reproducible-build weakness (§1.4).

### 9. Matching compares OBJECTS, never disassembly text. → **how G10 must be gated.**
objdiff parses both ELFs into a common IR and diffs that, so a match means both compiled to the same
machine instructions (§1.6). Diffing two disassemblies is not a function of the source and cannot be
trusted. **Our gate must compare the artefact the compiler actually emitted.**

### 10. A byte-exact build is NOT evidence the sources are understood. → **how we report.**
`CDebugFont` is a deliberately wrong-shaped stand-in for an unidentified retail global, committed with
a TODO, in a project that matches byte-for-byte (§4.5) — and 310 functions still carry `@unknownret`,
meaning nobody knows what they return (§4.5). Matching and understanding are different axes.
**Also: reconstructed hardware headers are only validated in the fields the game touches** (§4.8) —
which is precisely the "plausible-looking stub" trap. Our reports must not conflate the axes.

### And one correction to the brief itself
DCDecomp uses **MWCC/MWLD 2.3.3**, not 2.3.1.01, and the **known `toml11`-without-`GIT_SHALLOW`
finding does not apply to it at all** — it has no CMake dependency fetching whatsoever (§1.1, §1.4).

---

# 1. THE TOOLCHAIN RECIPE

*Order of mining: Dockerfile → CMakeLists → invocation scripts → verify/diff machinery.*

## 1.1 Which MWCC build, and a correction to the brief

**The brief said `MWCC/MWLD 2.3.1.01`. DCDecomp does not use that. It uses a 2.3.3 build.**

Both trees are checked in, and only one of them is referenced:

```
tools/compilers/mw/2.3/    asm_r5900_elf.exe  mwccmips.exe  mwldmips.exe  LMGR326B.dll
tools/compilers/mw/2.3.3/  asm_r5900_elf.exe  mwccmips.exe  mwldmips.exe  LMGR326B.DLL
```

Every reference in the build and tooling points at **2.3.3**:

- `CMakeLists.txt:65` — `set(MW ${TOOLS_DIR}/compilers/mw/2.3.3/)`
- `scripts/build/mwccgap.sh:63` — `: "${MW_DIR:=tools/compilers/mw/2.3.3}"`
- `scripts/build/check_unmatched.py:31`, `scripts/build/score_drafts.py:27`,
  `scripts/build/literals.py:14`, `scripts/build/.score_drafts.py:15`,
  `tools/mwcc-debug/README.md:49`

`tools/compilers/mw/2.3/` is dead weight in the tree as far as the build is concerned.

```
FINDING: DCDecomp's matching build uses the Metrowerks MWCC/MWLD **2.3.3** binaries, not 2.3.1.01;
         a 2.3 tree also sits in the repo but nothing in the build references it.
SCOPE:   TOOLCHAIN
EVIDENCE: /mnt/ssd/dcdecomp/CMakeLists.txt:65 ; tools/compilers/mw/{2.3,2.3.3}/ ; the five
          referencing sites listed above
MEANS FOR US: Whatever we assumed about DCDecomp's compiler version was wrong. Before borrowing any
          flag set or codegen expectation from this project, pin the *actual* compiler we are
          comparing against. `docs/TOOLCHAIN.md` in our repo should record GT4's own MWCC version
          from GT4's ELF/known build info, and the two must not be conflated. Raising confidence:
          run the bundled `mwccmips.exe` under wibo and ask it for its banner.
CONFIDENCE: high (six independent references in the tree, all agreeing on 2.3.3)
```

## 1.2 No Wine: wibo is what runs the Windows compiler

This is the single most transferable trick in the whole project, and it is the opposite of what
you would guess.

```
FINDING: The Windows-hosted Metrowerks `mwccmips.exe`/`mwldmips.exe` are NOT run under Wine.
         They are run by **wibo**, a native Linux userland PE loader, copied out of a prebuilt OCI
         image rather than built in the Dockerfile.
SCOPE:   TOOLCHAIN
EVIDENCE: Dockerfile:56 — `COPY --from=ghcr.io/decompals/wibo:latest /usr/local/bin/wibo /usr/bin/`
          CMakeLists.txt:70-71 — `set(CC_MW ... wibo ${MW}mwccmips.exe)` / `set(LD_MW wibo ${MW}mwldmips.exe)`
MEANS FOR US: If our own toolchain work ever needs to execute the retail Windows toolchain on
          Linux, wibo is the known-good path — no Wine prefix, no `WINEPREFIX`, no `wineboot`, no
          MSYS. That removes an entire class of environment breakage. Note it also removes the
          ability to inspect the tool through Wine's debugger tooling.
CONFIDENCE: high (two independent files, one being the authoritative build definition)
```

`scripts/build/statefix-wibo.sh` exists as a *name* to tell you there is a wibo-native counterpart to
a gdb-under-Wine approach — see §2.5, it is one of the most interesting findings in this whole doc.

## 1.3 The compiler invocation, verbatim

```
SCOPE:   TOOLCHAIN
EVIDENCE: CMakeLists.txt:62-105
```

```cmake
set(MIPS_TOOL_PREFIX mips-ps2-decompals-)                     # :62
set(MW ${TOOLS_DIR}/compilers/mw/2.3.3/)                      # :65
set(STD_INCLUDE_DIR ${INCLUDE_DIR}/std)                       # :67
set(SCE_INCLUDE_DIR ${INCLUDE_DIR}/sce)                       # :68
set(LIB_INCLUDE_DIRS "${STD_INCLUDE_DIR}\;${SCE_INCLUDE_DIR}") # :69
set(CC_MW ${CMAKE_COMMAND} -E env MWCIncludes=${LIB_INCLUDE_DIRS} wibo ${MW}mwccmips.exe)  # :70
set(LD_MW wibo ${MW}mwldmips.exe)                              # :71
set(AS ${MIPS_TOOL_PREFIX}as) / objcopy / objdump              # :72-74

set(AS_FLAGS     -EL -march=r5900 -mabi=eabi -g -mno-pdr -non_shared -G0 -I include)        # :96
set(CC_MW_FLAGS  -O2 -c -Cpp_exceptions off -RTTI off -strings readonly
                 -pragma "divbyzerocheck on" -i include)                                   # :97-98
set(LD_MW_FLAGS      -map -nostdlib -m _start -nodead -g)                                 # :103
set(LD_MW_ISO_FLAGS   -nostdlib -m _start -nodead -sym on,noelf)                           # :104
set(BIN_FLAGS    -B mips:5900 -I binary -O elf32-littlemips)                               # :105
```

Four things in there are worth stealing outright, and one is a trap:

1. **`MWCIncludes` is the mechanism for the compiler's system-header search path.** Not `-I`. The
   env var is set with `cmake -E env` and points at *two* directories, `include/std` and
   `include/sce`. This is how MWCC is told where its own headers live. Anyone driving MWCC by hand
   and putting these on the command line is doing it the hard way.
2. **`-pragma "divbyzerocheck on"`** — the divide-by-zero check is part of the matching target. Turn
   it off and you get a mismatch that looks like a codegen bug.
3. **`-Cpp_exceptions off -RTTI off`** — no exception tables, no RTTI data. Both are sections you
   would otherwise have to match and would not.
4. **`-G0` on the assembler and `-non_shared`** — no small-data/-G0 model on the assembler side.
5. **TRAP:** `-DPAL` is appended to `CC_MW_FLAGS` for the PAL region (`CMakeLists.txt:101`) as a
   *single argv token*. `-DPAL` is not an MWCC preprocessor-define spelling — MWCC takes
   `-define PAL` (as `scripts/build/.score_drafts.py:15` correctly uses). Depending on how MWCC
   parses it, `-DPAL` either works by accident or silently does nothing. `scripts/build/region.py`
   then reasons about `#ifdef PAL` from the *config*, not from the compiler, which would hide the
   difference. **Do not copy the `-D` spelling from DCDecomp into our own flag sets.**

```
FINDING: DCDecomp passes `-DPAL` on the MWCC command line (CMakeLists.txt:101) while its own
         tooling uses the correct `-define PAL` spelling elsewhere — a latent inconsistency in a
         project that otherwise matches byte-for-byte.
SCOPE:   TOOLCHAIN
EVIDENCE: CMakeLists.txt:101 vs scripts/build/.score_drafts.py:15
MEANS FOR US: Never copy DCDecomp's flag strings verbatim. If we ever need a region define with
          MWCC, use `-define <NAME>`. And when a matching build mysteriously ignores a define,
          check this exact class of bug before suspecting the linker or the codegen.
CONFIDENCE: medium — it may be a valid MWCC alias that I could not execute to confirm. Raised by
          running `wibo mwccmips.exe -DPAL ...` on a probe file and checking whether the macro
          expands.
```
## 1.4 Dependency fetching — the KNOWN FIND, confirmed as a NON-issue here

**Verdict: DCDecomp does not have the ps2xRecomp flaw, because it does not fetch dependencies from
CMake at all.**

Exhaustive search of `CMakeLists.txt` and both files in `cmake/`:

```
grep -n "FetchContent|ExternalProject|GIT_REPOSITORY|GIT_SHALLOW|GIT_TAG|find_package" \
     CMakeLists.txt cmake/*.cmake
  → (no matches)

grep -rn "GIT_SHALLOW|GIT_REPOSITORY|GIT_TAG|git clone|--depth" \
     --include=*.sh --include=*.py --include=*.cmake --include=*.yml --include=*.yaml .
  → only ./scripts/host/overlay_private.sh:26, which prints `git clone <private repository> $src`
    as an INSTRUCTION TO THE HUMAN when the private overlay is missing. Not an automated fetch.
```

The only `execute_process` calls in CMake are to **the project's own Python scripts**, never to
`git` or a downloader: `scripts/build/region.py overlay_origin` (`CMakeLists.txt:79`),
`scripts/build/layout.py --list-extra-objects` (`CMakeLists.txt:281`), and
`cmake/ObjectLists.cmake:15` which calls `disassemble.py`.

DCDecomp's dependencies arrive by exactly three doors:

| Door | What | Pinned how |
|---|---|---|
| **git submodules** | `tools/m2c`, `tools/mwccgap`, `tools/mwccps2-debugger` (`.gitmodules`) | commit SHA in the index — inherently immutable |
| **Dockerfile apt/pip** | cmake, ninja, gdb, `splat64[mips]==0.50.0`, `libclang`, `pycdlib` | base image tag `debian:trixie-slim`; pip version **exact** |
| **Dockerfile wget** | `binutils-mips-ps2-decompals` `v0.10`, `clangd` `22.1.6`, `objdiff-cli` `v3.7.3` | `ARG *_VERSION` — version pinned, **no checksum** |
| **Dockerfile COPY --from** | `ghcr.io/decompals/wibo:latest` → `/usr/local/bin/wibo` | **`:latest` — not pinned at all** |

```
FINDING: DCDecomp has no CMake-level dependency fetching, so the ps2xRecomp `toml11`-without-
         GIT_SHALLOW flaw does NOT apply to it. The known find is confirmed as a non-issue for
         this project.
SCOPE:   TOOLCHAIN
EVIDENCE: grep over CMakeLists.txt + cmake/{ObjectLists,Overlays}.cmake finds zero
          FetchContent/ExternalProject/find_package; the only `git clone` string in the tree is
          prose in scripts/host/overlay_private.sh:26
MEANS FOR US: The lesson transfers *in reverse*. When a project has a real reason to fetch a
          dependency in CMake, the correct shape is DCDecomp's discipline (submodule SHA, or a
          version-pinned prebuilt binary in the Dockerfile), not ps2xRecomp's. If VULCAN 4 ever adds
          a FetchContent, it must set GIT_SHALLOW **and** GIT_SUBMODULES shallow — and DCDecomp
          gives the stronger example: for a single header-only library, do not fetch at all.
CONFIDENCE: high (exhaustive grep over every build-definition file in the tree)
```

**But the same class of problem does exist here, one level up, and it is worth recording:**

```
FINDING: DCDecomp's toolchain image is not reproducible in the strict sense — wibo is pulled as
         `ghcr.io/decompals/wibo:latest` with no digest, and the three wget'd binaries are pinned
         by version but carry no checksum verification.
SCOPE:   TOOLCHAIN
EVIDENCE: Dockerfile:56 (`--from=ghcr.io/decompals/wibo:latest`), :48-53 (binutils v0.10, no
          sha256), :88-94 (clangd 22.1.6), :99-102 (objdiff v3.7.3)
MEANS FOR US: When we pin OUR toolchain (GT4's MWCC, binutils, any diff tool), pin by digest or
          checksum, not by version tag, and record the digest in the doc. An upstream `:latest` that
          changes under a byte-exact matching build is a failure mode that looks like a codegen
          regression and would burn real time.
CONFIDENCE: high (read directly off the Dockerfile)
```

## 1.5 How the Docker image is assembled

Plain OCI, two targets off one `base`. Podman is what the project targets; Docker works.

```
FROM debian:trixie-slim AS base      # :11  linux/amd64, glibc-linked toolchain binaries
  ENV BINUTILS=/usr/local/binutils-mips-ps2-decompals   # :14
  ENV VIRTUAL_ENV=/opt/venv                            # :15
  ENV PATH=$PATH:${BINUTILS}:${VIRTUAL_ENV}/bin        # :16
  apt: apt-transport-https ca-certificates git gnupg gpg-agent sudo unzip wget   # :19-29
  apt: python3 python3-venv cmake ninja-build gdb                                # :36-43
  wget binutils-mips-ps2-decompals $BINUTILS_VERSION -> $BINUTILS                 # :48-53
  COPY --from=ghcr.io/decompals/wibo:latest wibo                                  # :56
  venv + pip: pycdlib "splat64[mips]==0.50.0" libclang                            # :59-67
FROM base AS dev     # + less build-essential doxygen unzip, clangd 22.1.6, objdiff-cli v3.7.3
FROM base AS build   # WORKDIR /dcdecomp ; COPY . . ; CMD cmake.sh elf ctx && verify_built.sh
```

Four decisions that are individually small and collectively the reason this project is buildable by
anyone:

1. **`trixie`, not `bookworm`.** `Dockerfile:9-10` says it outright: `binutils-mips-ps2-decompals`
   needs **glibc 2.38**, and bookworm ships 2.36. A toolchain-image base is a functional
   requirement, not a taste choice.
2. **`gdb` (full), not `gdb-minimal`.** `Dockerfile:32-35` — `scripts/build/statefix.py` runs *as a
   gdb Python script*, so it needs the full gdb. See §2.5; this is the cleverest thing here.
3. **`libclang` from pip, not clang from apt.** `Dockerfile:60-63` — the wheel bundles LLVM's own
   shared library, so `scripts/diff/m2ctx.py` gets a working C context with no clang installed. And
   it lives in **`base`**, not `dev`, because the *build* stage also generates `build/ctx.c`.
4. **`splat64[mips]==0.50.0`, MIPS support as an explicit extra.** `Dockerfile:64-66` — splat's
   MIPS support (spimdisasm + rabbitizer) is an optional extra, so without ` [mips]` splat *imports
   fine and then fails at runtime*. A comment-only dependency that fails late. Real trap.

```
FINDING: The Docker image is the single authoritative description of the toolchain — build.sh,
         run.sh, diff.sh, dev.sh and the CI workflow all funnel through the same image and the same
         scripts/build/cmake.sh, so the configure rules cannot drift between environments.
SCOPE:   TOOLCHAIN
EVIDENCE: Dockerfile:1-7 (two targets, one command per entry point); CMakeLists.txt:1-2;
          scripts/build/cmake.sh:1-3 ("The one place that runs cmake"); README.md "Both run in the
          dcdecomp_dev container image"; .github/workflows/progress.yml "Build the toolchain image"
          step reuses the Dockerfile rather than restating the steps
MEANS FOR US: Our toolchain should have exactly ONE description that every entry point goes
          through. If our docs and our scripts can disagree about a compiler flag, they eventually
          will. Worth auditing `docs/TOOLCHAIN.md` against whatever actually invokes GT4's
          toolchain.
CONFIDENCE: high
```

```
FINDING: The source tree is bind-mounted into the container rather than copied in, and both CMake
         and objdiff take a `.build.lock` flock so only one build of the tree runs at a time.
SCOPE:   TOOLCHAIN
EVIDENCE: Dockerfile:118-122 ("The tree is mounted rather than copied in, so both are
          incremental"); scripts/build/cmake.sh:35-42 (flock, with the reason: the split rewrites
          thousands of files under asm/ and objdiff's GUI watcher starts a build the moment they
          change, so a unit compiled against a half-written split links into a wrong image);
          cmake.sh:31-34 (lock lives outside build/ so CLEAN cannot delete it mid-build)
MEANS FOR US: The concurrency hazard is real and general — a watcher-driven rebuild racing a
          destructive regenerate. If VULCAN 4 ever has a background watcher rebuilding our
          translation units, it needs the same lock. Adopt the lock-outside-the-wipeable-directory
          detail; it is easy to get wrong.
CONFIDENCE: high
```

```
FINDING: `scripts/build/cmake.sh` defends against CMakeCache.txt recording an absolute source
         directory, which makes cmake refuse to run when the same tree is seen at a different path
         (build image vs devcontainer vs host bind mount) — and it defends against that on
         `cmake --build`, not just `cmake`, because --build re-runs configure through build.ninja.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/cmake.sh:14-21 (the why), :51-58 (cache_is_stale reads
          CMAKE_HOME_DIRECTORY out of the cache), :60-73 (configure prefers `--fresh` only when the
          recorded dir differs, otherwise falls back to it on *any* configure failure)
MEANS FOR US: Directly relevant to us: our build tree is run from several contexts (native x86-64
          build, ARM/Odin 2 cross build, possibly a container), and CMakeCache staleness across
          those is a known, self-inflicted time sink. Adopt the "inspect CMAKE_HOME_DIRECTORY, then
          --fresh" pattern. Also adopt "wrap every cmake invocation, nothing else calls cmake".
CONFIDENCE: high
```
## 1.6 How matching is proved — objdiff, and it compares OBJECTS not text

This is the answer to "which diff tool, what does it compare".

```
FINDING: Matching is proved by **objdiff** (`objdiff-cli`), which compares **ELF object files** —
         the reference is the object splat built from the retail disassembly, the base is whatever
         now provides that function — never a text diff of disassembly.
SCOPE:   TOOLCHAIN
EVIDENCE: diff.sh:19-21 ("objdiff compares object files, not disassembly text: the target is the
          reference dump as retail wrote it, the base is whatever provides that function now, and
          objdiff.json pairs them -- so a unit is `<section>/<symbol>`");
          diff.sh:99-103 (`objdiff-cli diff -p . -u "$unit" "$sym"`);
          diff.sh:70-75 (`objdiff-cli report generate -o progress/report.json`);
          Dockerfile:99-102 (objdiff-cli v3.7.3, a single static binary, GUI deliberately not
          installed because the image has no display)
MEANS FOR US: **This is the single most important transferable idea in the document.** A text diff
          of two disassemblies cannot be trusted: assembly is not a function of the source, so a
          textual difference tells you nothing about whether the C is right. objdiff parses both
          objects into a common IR and diffs *that*, so a match means both compiled to the same
          machine instructions. For our G10 ("grow native code, verify byte-exact") the equivalent
          gate must likewise compare the artefact the compiler actually emitted, not a rendering of
          it. If we ever diff GT4's disassembly against our translation as text, we are measuring
          the wrong thing.
CONFIDENCE: high (stated by the project in diff.sh's own header and exercised by diff.sh/CI)
```

**How a symbol is located, and why `main` is a special case:**

- Symbols are the **mangled names the compiler uses** — `SetDay__9CSaveDataFi`
  (`diff.sh:15-17`). Look one up with `grep <name> config/*/*.symbols.txt`.
- `scripts/diff/ref_index.py` resolves a symbol to a *reference dump path*, which is how the
  section is discovered when the caller did not name it (`diff.sh:82-89`).
- The unit name is then derived by stripping `asm/*/nonmatchings/` or `asm/*/matchings/` and the
  trailing `/` (`diff.sh:86-89`) — objdiff names a unit after its source, e.g. `camera` or
  `dun/gameloop`.
- `main` is both a section name and a real symbol, so it only counts as a section when something
  that is not a flag follows it (`diff.sh:43-45`). A genuinely gnarly edge case worth knowing
  before writing the same resolver.

**Timestamps — a small correctness detail that transfers:**

`diff.sh:105-107` allocates a TTY (`-it`) for objdiff's interactive view **only when both stdin and
stdout are actually terminals**, because `diff.sh ... | cat` and CI runs must not be given one.

```
FINDING: DCDecomp gates the interactive TTY on `-t 0 && -t 1` rather than always passing one, so
         piped and CI invocations get machine-safe behaviour from the same script.
SCOPE:   TOOLCHAIN
EVIDENCE: diff.sh:59, diff.sh:105-107
MEANS FOR US: Adopt the same guard on any harness that has both an interactive mode and a
          machine-readable mode — one script, no divergence between the human path and CI.
CONFIDENCE: high
```

## 1.7 The split configuration — how retail code becomes a target you can compile against

`config/ntsc/main.yaml` is the heart of the matching setup and it is hand-maintained, not generated.

```
FINDING: The reference assembly is produced by **splat** from a hand-written YAML that names every
         translation unit by its exact file offset, its type (`cpp` / `asmtu` / `data` / `.rodata`)
         and — for the whole-section bss blocks — its exact VRAM address.
SCOPE:   TOOLCHAIN
EVIDENCE: config/ntsc/main.yaml — `options:` at :4-59, `segments:`/`subsegments:` at :61-474.
          Each of the ~130 `.text` subsegments is one TU at a named file offset, e.g.
          :199 `{start: 0x24030, type: cpp, name: camera}`, :207 `{start: 0x2c270, type: cpp, name:
          mglib}`, :209 `{start: 0x34860, type: cpp, name: visualvu1}`. The bss block at :453-472
          gives each library TU its own `vram:` (e.g. `lib/sce/libpad` at 0x2a9c40).
          `sha1: d9965d6908a334a5dfc982f3384428ad42048d54` at :3 pins the retail binary.
MEANS FOR US: The unit-boundary map is the asset. Splitting the ELF into per-TU regions with known
          VRAM addresses is what makes "compare one function against retail" possible at all, and
          it has to come from GT4's own ELF the same way. Ours should be generated once and checked
          in, keyed to a hash of GT4's binary, not re-derived by guesswork each session.
CONFIDENCE: high
```

Six splat options in that file are load-bearing and worth copying verbatim in spirit:

| Option | Value | Why it matters |
|---|---|---|
| `rodata_string_guesser_level`, `data_string_guesser_level` | **`0`** | `:28-29` "A wrong guess makes the build non-matching, so nothing is guessed." **This is the single best practice in the file.** |
| `find_file_boundaries` | `False` | :31 — same reason |
| `create_c_files` | `False` | :45 — every source already exists; splat only *reads markers* out of them to decide which functions still need reference assembly |
| `migrate_rodata_to_functions` | `True` | :33-35 — a constant belongs to the function that uses it and travels with it |
| `create_bss_pads` | `True` / `create_data_pads` `False` | :25-26 |
| `asm_inc_header` / `generated_s_preamble` | both `.include "macro.inc"` | :41-42 — every reference file must assemble *on its own*, because objdiff builds each as the object it compares against |

```
FINDING: DCDecomp's splat config sets both string-guesser levels to 0 with the comment "A wrong
         guess makes the build non-matching, so nothing is guessed" — the tool is deliberately
         hobbled so it cannot invent structure.
SCOPE:   TOOLCHAIN
EVIDENCE: config/ntsc/main.yaml:28-29 (and `find_file_boundaries: False` at :31)
MEANS FOR US: Directly matches VULCAN 4's Law 3 ("refused, not guessed at") and Law 4. Any tool we
          point at GT4's ELF must be configured to refuse to infer rather than infer. Every
          automated structure guess is a future false match that costs more than the refusal.
CONFIDENCE: high
```

**One more, and it is a genuinely nasty linker fact:**

```
FINDING: DCDecomp hand-writes include/macro.inc because splat's generated version makes `alabel`
         global, and a branch to a global label becomes a relocation that mwld and gas disagree
         about. splat's own macros must be disabled (`generate_asm_macros_files: False`).
SCOPE:   TOOLCHAIN
EVIDENCE: config/ntsc/main.yaml:46-50 — "include/macro.inc is written by hand and load-bearing:
         its `alabel` is deliberately not global, because a branch to a global label becomes a
         relocation mwld and gas disagree about. splat's generated one makes it global, so it must
         not overwrite this project's."
MEANS FOR US: A concrete, transferable trap in cross-toolchain assembly: **symbol binding
          differences between the linker you link with and the assembler you assemble the
          reference with turn into relocation mismatches, not into assembly errors.** Any project
          that assembles reference asm with `gas` but links with MWLD walks into this. Worth
          checking early in our own setup, because it presents as a linker error and gets
          misdiagnosed as a codegen problem.
CONFIDENCE: high (the project's own comment states the mechanism and the consequence)
```
## 1.8 The four verdicts — and the anti-laziness rules that go with them

`scripts/build/verify.py` is the verifier. It is where most of this document's value is.

```
FINDING: DCDecomp classifies every function into exactly four verdicts — PERFECT, FUZZY, ASM,
         UNMATCHED — and two of the rules are anti-laziness mechanisms, not measurements.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:39 (the four constants), :77-91 (classify), :268-301
          (categorise, where both anti-laziness rules live); the project's own summary is
          scripts/build/verify_built.sh:4-8
MEANS FOR US: Directly usable as the shape of our G10 gate. Copy the four-way split and both
          rules below.
CONFIDENCE: high
```

**The rules, verbatim in intent:**

1. **A fuzzy match must be declared in the source, and the declaration is *checked*, not trusted.**

   `verify.py:292-296`:
   > "Undeclared drift looks like a fuzzy match but nothing promised it would stay one, so it is
   > not allowed to pass as one."

   The declaration is a marker in the C++ itself: `FUZZY_MATCH("<image>", <symbol>)`, scanned out of
   the sources by `declarations()` (`verify.py:33, 112-118`). So the *programmer* asserts "this one
   may differ in register choice", and the verifier independently confirms that it does — and
   demotes it to UNMATCHED if it does, or if the programmer never said so.

   `verify.py:19-26` states what fuzzy means, and the definition is tight:
   > "A fuzzy match is a function whose compiled code has retail's instructions in retail's order,
   > and differs only in which registers the compiler picked. It occupies the same bytes, so
   > nothing after it moves... same length, same opcodes, same immediates and branch offsets,
   > register fields free to differ. Anything looser is not a fuzzy match."

   The mask is built per instruction class (`verify.py:44-64`) — jumps get `0xFFFFFFFF` (no
   registers, so *any* difference is real), SPECIAL/MMI/COP0 mask `rs|rt|rd`, COP1/COP2 mask
   `ft|fs|fd` because coprocessor formats keep the fmt field in `rs`, and everything else masks
   `rs|rt`.

   ```
   FINDING: DCDecomp's fuzzy-match rule is an explicit R5900 instruction-class bitmask — jumps are
            unmasked (no register fields, so any difference is disqualifying), COP1/COP2 mask
            ft/fs/fd because coprocessor formats keep `fmt` in `rs`, SPECIAL/MMI/COP0 mask rs/rt/rd,
            and all other formats mask rs/rt. Length must be equal and a multiple of 4.
   SCOPE:   HARDWARE-UNIVERSAL (the instruction encoding) applied under TOOLCHAIN (the rule)
   EVIDENCE: scripts/build/verify.py:44-64 (`_RS_RT_RD`, `_RS_RT`, `_FT_FS_FD`, `_mask`),
              :67-74 (`same_but_for_registers`), :77-91 (classify)
   MEANS FOR US: If we ever want a tolerance class below "byte-exact" (and for G10 we should want
            to know how *close* a near-miss is), this mask is a ready-made, encoding-correct
            definition of "only the register allocator differed". GT4 is the same R5900, so the
            mask applies unchanged. Do not invent a looser ad-hoc tolerance.
   CONFIDENCE: high (read directly; the bit positions are MIPS/R5900 ISA, not project opinion)
   ```

2. **`INCLUDE_ASM` bytes are not a match, and are counted separately.**

   `verify.py:296-300`:
   > "Retail's own bytes, straight from the marker. Right, but not decompiled, so it is not a
   > perfect match in the sense that counts."

   `verify.py:36-38`:
   > "ASM is not a quality of the bytes -- a function an INCLUDE_ASM marker supplies is retail's
   > own -- but of the source: nothing about it is decompiled, so it is worth counting apart from a
   > function whose C++ reproduces retail exactly."

   ```
   FINDING: DCDecomp counts "retail bytes pasted in via INCLUDE_ASM" as a *separate* category from
            "C++ that compiles to retail's bytes" — and says so explicitly, on the grounds that the
            category measures the SOURCE, not the output.
   SCOPE:   TOOLCHAIN
   EVIDENCE: scripts/build/verify.py:36-39 ; :296-300 ; scripts/build/verify_built.sh:6-8
   MEANS FOR US: Directly applicable to how VULCAN 4 reports progress and to how it talks to the
            captain. Any project that compiles the game at all can produce byte-identical output by
            shipping retail's bytes; reporting that as "matched" is a lie that hides all real work.
            Our reports must keep "runs retail's own code" separate from "we reproduced the
            behaviour", permanently. This is the same distinction as our Law 2 (no faking).
   CONFIDENCE: high
   ```

3. **Data never differs — there is no fuzzy for data.**

   `verify.py:268-273`:
   > "`data_differs` is the number of bytes outside any function that do not match, which is always
   > a failure -- a fuzzy match is a register choice in code, and there is no such thing for data."

   The implementation is elegant and worth copying: a `covered` bytearray the length of the image is
   marked `0x01` across every function's span (`verify.py:283`), and then **every byte not inside a
   function is compared byte-for-byte** (`verify.py:305-309`).

   ```
   FINDING: The verifier builds a coverage map of every function's span and requires every byte
            *outside* those spans to match exactly — one pass catches stray edits in rodata, vtables,
            jump tables, padding and literals, not just in functions.
   SCOPE:   TOOLCHAIN
   EVIDENCE: scripts/build/verify.py:281-283 (`covered[start:end] = b'\x01' * entry.size`),
              :305-309
   MEANS FOR US: Adopt the coverage-map idea for GT4. It means our gate catches data corruption
            that a function-by-function check would walk straight past, and it needs no separate
            "data test" — the data is what is left over.
   CONFIDENCE: high
   ```

4. **A length mismatch is reported, not crashed on.**

   `verify.py:303-309`:
   > "A build that is not the same length as retail is a failure like any other, and has to be
   > reported rather than crash the count: every byte past the end of the shorter of the two differs
   > by definition."

   `shared = min(len(retail), len(build))`, compare that, then
   `data_differs += abs(len(retail) - len(build))`. Robust against the single most common failure
   mode of a matching build.

## 1.9 What "matching" is allowed to exclude — and how that is kept honest

```
FINDING: DCDecomp compares the executable only over a **span** (offset 0x100, size 0x1A2380 for
         NTSC) because its symbol and string tables are not reproduced; both overlays
         (TITLE.BIN, DUN.BIN) must come out **byte-identical** as whole files.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:376-382 — "How much of each built file has to match the retail
          original. Only the executable is allowed to differ anywhere: it is checked over its code
          and data, because the retail symbol and string tables are not reproduced yet. Anything not
          listed here -- both overlays -- has to come out byte-identical."
          The span is mirrored in the *retail* hash table, which carries two hashes for
          SCUS_971.11: whole-file and the same span (verify.py:219 / :257).
MEANS FOR US: **The exclusion must be structural, not a tolerance.** Ours is the mirror image: our
            artefact is not a byte-image at all, it is behaviour, so the honest gate is GT4's own
            executed trace — syscalls, MMIO, and observable state — compared against recorded GT4
            runs. Same discipline: name exactly what is out of scope, and make everything else
            mandatory. Also note the two-hash trick: the retail table knows both the whole file and
            the span, so a span hash can never be satisfied by an unrelated file.
CONFIDENCE: high
```

```
FINDING: Verification is **informational only** in the default build: verify_built.sh ends with
         `|| echo "Verification found unmatched build output (informational only)."` and the
         script itself is `set -eu`, so the `||` is what keeps the build green.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify_built.sh:10-16 ; README.md "The build produces the main executable
          SCUS_971.11 with matching text and data sections and completely matching TITLE.BIN and
          DUN.BIN overlays" (README also says matching the main executable's symbol/string tables
          "may be explored in future, though this isn't a current priority")
MEANS FOR US: Note the *shape*: they track "how much is matched" as a first-class published metric
            (decomp.dev shields, per-region percentages) rather than enforcing a binary gate, so a
            partially-matched tree still builds and still runs. For us the same instinct is right
            for the *matching* project, but our own dishes DO have hard gates (AGENTS.md law 4), so
            do not copy the "informational only" softness into our gate — copy the metric.
CONFIDENCE: high
```

```
FINDING: The progress metric is **bytes, not functions** — percentages are computed over total code
         bytes, with function counts printed alongside as a secondary number.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:348 (`code = sum(v[1] for v in totals.values()) or 1`) and
          :349-353 (`pct(kind) = 100.0 * totals[kind][1] / code`); :354-361 prints "Code, by byte".
          decomp.dev reads the same thing: .github/workflows/progress.yml summarises
          `matched_code_percent` with `matched_functions`/`matched_code`/`total_code`.
MEANS FOR US: If we adopt a match percentage, weight by bytes. A function count rewards stubbing
            out many one-liner accessors and tells you nothing about the bulk of the code. Bytes is
            the honest denominator.
CONFIDENCE: high
```

```
FINDING: Failure output is capped at 10 with an "... and N more" tail, and the worst category is
         printed last so the eye lands on the failures.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:361-365 (`for section, symbol in failures[:10]` … `if len(failures)
          > 10: print(f'    ... and {len(failures) - 10} more')`) ; :257 ("Worst last, so the eye
          lands on the failures", `ORDER = (PERFECT, FUZZY, ASM, UNMATCHED)`)
MEANS FOR US: Trivially copyable. A verifier that dumps 3000 unmatched symbols trains people to
            ignore it. Ours should cap and summarise.
CONFIDENCE: high
```

```
FINDING: The verifier locates functions by **virtual address, not file offset** —
         `start = entry.vram - image.vaddr` — and treats any function whose span falls outside
         either image as silently skipped rather than an error.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:279-284
MEANS FOR US: Same address discipline applies to GT4: everything that talks about GT4's code has to
            go through load address → virtual address. If we ever compare GT4's own outputs
            against ours, the virtual address is the only coordinate system both sides share.
CONFIDENCE: high
```

```
FINDING: Scanning sources for markers skips files starting with `tmp` and applies a region-aware
         conditional filter first, because "compiler temporaries live beside sources and can
         disappear during a scan" and a marker under `#ifdef PAL` belongs to the PAL build alone.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/verify.py:118-125 ; region.py:137-212 (`evaluate_guard`, `active_text` —
          which blanks out the lines a region's `#ifdef` leaves out, so a marker scan sees what the
          compiler actually compiles). The same `tmp*` exclusion is mirrored in CMakeLists.txt:130
          and the comment there explains why (mwccgap compiles a temporary beside its source).
MEANS FOR US: If our build ever drops a temporary next to a source, every source-scanning tool
          needs the same exclusion, and the reason has to be written down or it gets "cleaned up"
          later. Also: make marker scanning region-aware from the start — retrofitting conditional
          evaluation into it is painful.
CONFIDENCE: high
```

---

# 2. THE MATCHING WORKFLOW

*How one function goes from disassembly to verified byte-exact.*

## 2.1 The obstacle that makes matching hard at all: MWCC carries state between files

This is the finding that explains everything else in this project, and it is the one thing GT4
almost certainly shares with it.

```
FINDING: MWCC carries compiler state from one source file of an invocation to the next and NEVER
         resets it, and it reads memory that nothing ever wrote. Retail compiled a whole program in
         ONE invocation; DCDecomp compiles one unit per invocation — so that state is empty where
         retail's was not. This, not codegen, is the fundamental obstacle to a matching PS2 decomp.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/statefix.py:7-11 (verbatim: "MWCC carries state from one source file of an
          invocation to the next and never resets it, and it reads memory nothing ever wrote
          (re/ai/compiler/leaked_state.md). Retail compiled every unit of a program in one
          invocation; this build compiles one unit per invocation, so that state is empty where
          retail's was not.")
MEANS FOR US: **Expect this.** If GT4 was also built as one multi-file MWCC invocation, then any
          per-function static recomp we produce is compiling in a state retail never used, and
          small codegen differences are expected and are NOT evidence our translation is wrong.
          This is a direct argument for our G10 gate having more than one class of verdict — some
          differences are provably attributable to invocation granularity, not to us. Raising
          confidence for GT4: check whether GT4's ELF shows one-invocation artefacts (e.g. a single
          continuous literal/name counter across the whole image) the same way retail's `@<n>`
          counter did for Dark Cloud — see §2.3.
CONFIDENCE: high for Dark Cloud (the project's own statement); medium for GT4, which must be
          checked against GT4's own ELF before the conclusion is drawn for us.
```

## 2.2 The fix: drive the compiler under gdb and inject state through fake pragmas

This is the most impressive engineering in the entire project and it is fully transferable.

```
FINDING: DCDecomp restores the lost compiler state by running `mwccmips.exe` under gdb, standing at
         the compiler's own `#pragma` handler, acting on **invented pragma names**, and then
         redirecting execution to the compiler's "unknown pragma" exit — so the compiler never sees
         them. The source therefore still compiles on its own (mwcc ignores an unknown pragma
         silently); it just does not get the state unless this script runs.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/statefix.py:26-30 (verbatim, quoted in §2.1 block below); the mechanism is
          documented at statefix.py:26-35 and :62-71, and the addresses are stated at :69-71:
          PRAGMA=0x004440C6, PRAGMA_IGNORE=0x00444505, TOKEN=0x005570FC, LEXER=0x00555614,
          NAME_TEXT=10
MEANS FOR US: This is a template, not a trick to copy blindly. The general shape — *when the
          original build's ambient state cannot be reproduced by flags, reproduce it by
          controlling the compiler at its own entry points* — is exactly what a "grow native code,
          verify byte-exact" project needs when a discrepancy is attributable to compiler state
          rather than to the source. The specific addresses are MWCC-2.3.3-specific and MUST NOT be
          assumed valid for GT4's compiler.
CONFIDENCE: high (read directly off the script's own docstring and constant table)
```

The full quote (`statefix.py:26-35`):

> "The compiler has no such pragmas. This runs it under gdb, stands at its pragma handler, acts on
> these spellings and sends them to its own 'unknown pragma' exit, so the compiler never sees them;
> mwcc ignores an unknown pragma silently, so a source carrying them still compiles without this
> script -- it just does not get the state."

The eight state knobs it exposes as invented pragmas (`statefix.py:16-24`):

```c
#pragma helper_mask_gpr 0x30       // set the integer helper-argument mask
#pragma helper_mask_fpr 0x1000     // set the float one
#pragma helper_mask_gpr +0x20      // or a bit into it
#pragma argument_flag 0            // what every float argument of a call reads
#pragma argument_flag_ones 4,9     // which of those reads 1 instead
#pragma argument_flag_free 7,8     // which of them to leave to the node's own byte
#pragma order_flag_zeros 3,7       // which of the ordering pass's writes to drop
#pragma literal_reload 0x3C23D70A  // which float literals every unknown store kills
#pragma name_counter 910           // where the invented-name counter starts
```

**Read what that list says about MWCC.** Every one of these is a piece of *ambient state that leaks
between functions*: register masks, per-argument order flags, a global ordering-pass toggle, a
literal-invalidation mask, and a **global counter**. That is not a coincidence — it is the shape of
"a compiler that never resets its working memory between functions".

```
FINDING: The state that has to be restored is dominated by things that are only well-defined after
         a *previous* function ran: a global invented-name counter, per-argument evaluation-order
         bytes that a pass at 0x004C78C0 flips to 1 and nothing clears, and a literal-reload mask
         keyed to whether a store through an unknown address could have clobbered a float.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/statefix.py:109-122 (NAME_COUNTER: "The counter behind every name the
          compiler invents -- the `@<n>` a floating constant is filed under, which retail's linker
          kept as `@<n>`. 0x0042E540 hands out the value and increments it, and nothing resets it
          between units, so retail's ran for the whole program.");
          :109-115 (the ordering pass); :130-139 (MAY_BE_STORED_TO: "Retail's literals were created
          once for the whole program, next to other units' data; a unit compiled alone creates them
          next to its own, so the byte can differ")
MEANS FOR US: **A concrete, testable prediction for GT4.** If GT4's symbol table contains `@<n>`
          style float-literal names, that is direct evidence MWCC's name counter ran once for the
          whole GT4 program — i.e. GT4 shares this exact property, and any per-function recomp of
          GT4 will face the same class of difference. Checking GT4's own symbol table for `@` names
          is cheap and would upgrade this finding from medium to high confidence *for us*.
          This is a `NEED:` item, not something we can settle from the Dark Cloud tree.
CONFIDENCE: high for Dark Cloud; the GT4 side is a testable prediction, not yet tested.
```

**`--wibo` makes it a drop-in for wibo itself** (`statefix.py:32-35`): it takes the guest executable
as its first argument and otherwise keeps quiet, and `scripts/build/mwccgap.sh` reaches it by
handing mwccgap `--wibo-path` pointing at `scripts/build/statefix-wibo.sh`. Clever layering — the
compiler never learns it is being driven.

```
FINDING: State injection is composed with the loader rather than replacing it: statefix.py's
         `--wibo` mode makes it a transparent stand-in for wibo, and mwccgap is pointed at the
         stand-in rather than at wibo.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/statefix.py:32-35 ; scripts/build/mwccgap.sh (--wibo-path wiring)
MEANS FOR US: Layering interception behind the existing loader entry point keeps every other tool
          (mwccgap, the CMake rules, the CI) working unchanged. Worth copying as a general tactic:
          intercept at the seam everyone already uses, and change nothing above it.
CONFIDENCE: high
```

## 2.3 Debugging the compiler itself — `tools/mwcc-debug`

The same project also built a tool to *read the compiler's internal state*, and its README is
honest about both what works and what does not.

```
FINDING: `tools/mwcc-debug/capture.py` dumps what MWCC is thinking while compiling one function —
         the PCode at each backend pass boundary, and the register allocator's priority list,
         colours and interference edges — and `replay.py` re-runs the compiler's own colouring loop
         over the captured interference graph to test alternative allocation rules.
SCOPE:   TOOLCHAIN
EVIDENCE: tools/mwcc-debug/README.md:3-6 (what it dumps), :16-22 (output layout),
          :24-37 (replay), :1 (titled "MWCC 2.3.3 internal-state capture")
MEANS FOR US: This is the difference between guessing and knowing when a near-miss is the register
          allocator. If our G10 gate ever produces "same instructions, different registers" cases
          against GT4, this is the shape of the tool that explains them — capture the allocator's
          decision, then replay it under a candidate rule. Worth remembering as a known-good
          approach rather than planning to build it now.
CONFIDENCE: high
```

**Two methodology rules in there that are worth more than the tool:**

1. **The replay must first reproduce the real allocation** (`README.md:26-30`):
   > "With no rule it only checks that the replay reproduces the real allocation, which is the sanity
   > check that makes any variant result meaningful; with a rule it reports which functions would be
   > allocated differently, flagging any that currently match retail."

   *Validate the simulator against the real thing before you trust the simulator.* That is a
   universally applicable discipline and our verification work should follow it.

2. **The profile refuses to run against the wrong binary** (`README.md:48-50`):
   > "The compiler itself is the one the build uses, `tools/compilers/mw/2.3.3/mwccmips.exe`, and
   > the capture refuses to run against any other image: the profile pins its SHA-256, size, and PE
   > timestamp."

```
FINDING: DCDecomp's debugger profile pins the compiler's **SHA-256, file size and PE timestamp**,
         and hard-refuses to run against any other binary; and every address in it was recovered
         from that exact image — "Nothing was inherited from the toolkit's 2.4 or b210 profiles,
         and the differences are real: this build packs a PCode operand into 14 bytes where 2.4
         uses 22."
SCOPE:   TOOLCHAIN
EVIDENCE: tools/mwcc-debug/README.md:48-50, :58-62
MEANS FOR US: Two rules to adopt wholesale. (1) Any hardcoded-offset tooling must be pinned to a
          fingerprint of the exact binary it was derived from and must refuse to run otherwise —
          silently running against a different compiler build produces plausible garbage. (2) Do
          not inherit internal-structure knowledge from a different version of the tool even when
          it is "the same tool". Struct layouts demonstrably differ between MWCC 2.3.3 and 2.4.
          For GT4 we must derive offsets from GT4's own compiler, never from any published
          profile of a different version.
CONFIDENCE: high
```

### The three wibo-transport traps — worth having, they cost real time to rediscover

`tools/mwcc-debug/README.md:66-76` states them as "Three things follow from that":

```
FINDING: Driving a 32-bit Windows PE compiler under gdb via wibo has three specific traps, all
         solved: (1) wibo maps `mwccmips.exe` at 0x400000 only AFTER it loads the image, so guest
         breakpoints must be armed at wibo's import-resolution symbol rather than at `starti`;
         (2) GDB has a 64-bit inferior running 32-bit guest code, so out-of-line breakpoint
         stepping decodes guest instructions with the wrong rules and faults —
         `set displaced-stepping off` is required, not cosmetic; (3) the image to fingerprint is a
         guest file, not the process GDB started, so it is named explicitly.
SCOPE:   TOOLCHAIN
EVIDENCE: tools/mwcc-debug/README.md:66-76, each commented where it is done
MEANS FOR US: If we ever instrument a Windows-hosted tool under gdb on Linux, these three are
          already-known landmines. `set displaced-stepping off` in particular produces a *fault*
          that reads like a code bug. Save ourselves the day.
CONFIDENCE: high (specific, non-obvious, and stated as hard-won)
```

```
FINDING: DCDecomp uses mwccps2-debugger's decoding but supplies its own profile and breakpoint
         wiring; the submodule is NOT patched. "The toolkit's own `mwccps2_debugger.py` assumes a
         Windows-hosted compiler and so cannot drive this compiler directly; nothing in the
         submodule is patched."
SCOPE:   TOOLCHAIN
EVIDENCE: tools/mwcc-debug/README.md:54-64, :78-79
MEANS FOR US: Reuse a submodule's *knowledge* without forking its *assumptions*. This is the same
          discipline as our AGENTS.md law 8 (patch upstream, don't fork blindly) applied to a
          third-party tool we depend on.
CONFIDENCE: high
```
## 2.4 mwccgap — how a half-decompiled translation unit still links at retail's addresses

This solves a problem our G10 will absolutely hit.

```
FINDING: MWCC emits a translation unit's functions as one contiguous `.text`, so a hole in the
         middle cannot be filled from an external `.s`. mwccgap works around this by compiling each
         source TWICE — once as written, to learn which functions the C++ already defines, and once
         with each INCLUDE_ASM marker replaced by a run of `nop` the size of the function it stands
         for — then assembling the reference file splat wrote and putting those bytes over the nops.
         The result is ONE object holding both, which lets a half-decompiled unit link at retail's
         addresses.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/mwccgap.sh:7-13 (verbatim, quoted below)
MEANS FOR US: **This is the answer to "how do we ship a partial translation and still have the
          whole image stand up".** Any target that emits a unit as one contiguous block cannot
          substitute a foreign object for part of it; you must compile a placeholder of the exact
          size and patch the bytes afterwards. If our G10 ever needs to grow native code into
          GT4's layout incrementally, this is the proven technique and the reason is architectural,
          not incidental.
CONFIDENCE: high
```

> "tools/mwccgap compiles the source twice: once as written, to learn which functions the C++ already
> defines, and once with each marker replaced by a run of `nop` the size of the function it stands
> for. It then assembles the reference file splat wrote and puts those bytes over the nops. The
> result is one object holding both, which is what lets a half-decompiled translation unit link at
> retail's addresses -- mwcc emits a unit's functions as one contiguous .text, so a hole in the
> middle cannot be filled from an outside .s."

**Four more non-obvious details in that one script, each of which is a trap someone else will hit:**

1. **The second compile reads a temporary always named `.c`**, so mwcc can no longer infer the
   language from the extension — `-lang` is passed explicitly, picked from the real source.
   *"The game's own code is C++; everything under src/lib/ is C, being the SDK, newlib and
   libgcc."* (`mwccgap.sh:24-27`, and the `case "$src"` at :48-51). Inference from extension is
   destroyed by the workaround, so the real value must be carried explicitly.

2. **Argument order matters** (`mwccgap.sh:29-31`): mwccgap takes two positionals, passes the rest to
   mwcc, and `--as-flags` takes a *list* — so it must come last or it swallows the compiler flags.

3. **MWCC's dependency output is rewritten by awk** (`mwccgap.sh:89-118`): MWCC writes its dep map
   to `<stem>.d` in the CWD, using Windows path conventions (`C:\`, backslash continuations). The
   awk normalises CRs, strips continuation backslashes, drops the leading drive-letter path,
   converts `\` to `/`, makes every dependency absolute, and emits Ninja's `\\\n\t` form. Then the
   *second* compile's leftover `tmp*.d` is swept (`mwccgap.sh:120-122`).

   ```
   FINDING: MWCC emits Makefile-style `.d` dependency files with Windows path spellings and CRLF,
            so any build integration must normalise them — MWCC does not produce Ninja-compatible
            output and will not tell you.
   SCOPE:   TOOLCHAIN
   EVIDENCE: scripts/build/mwccgap.sh:89-118 (the awk), :120-122 (the sweep)
   MEANS FOR US: Same class of problem for us if we ever consume GT4-era MWCC output. Do not assume
            a Windows toolchain's depfiles are usable by a Linux build system; budget a
            normalisation step. The `tmp*.d` sweep is also why every source scanner in the project
            ignores files starting with `tmp` (§1.9).
   CONFIDENCE: high
   ```

4. **`MWCIncludes` is mwcc's `<>` search list, scoped to library headers only** (`mwccgap.sh:65-67`):
   > "mwcc's <> search list: the library headers, and only those, so a library is spelled
   > `#include <libvu0.h>` while the game's own headers come through `-i`."

   The distinction between `<>` (MWCIncludes) and `-i` (project include) is real and load-bearing.

**Marker convention, from the only two remaining markers in the tree:**

```c
// src/shot_freefuncs.cpp:973-980
#if defined(PAL) && !defined(NON_MATCHING)
// NON_MATCHING under PAL: 7 left -- 10.0f loads before the other arguments of AddNowLife; its
// evaluate_first 1 orders it but swaps the status and chara registers.
void BtStatusErrStep(void);
INCLUDE_ASM("asm/pal/nonmatchings/shot_freefuncs", BtStatusErrStep__Fv);
/* Retail's data for the function the marker above supplies. */
unsigned int pal_at1188__2[4] __attribute__((aligned(16))) = {0,0,0,0x3F800000};
#pragma name_counter 562

// src/shop.cpp:3935-3942
#if defined(PAL) && !defined(NON_MATCHING)
int ItemShopKey2();
/* Retail's data for the function the marker below supplies. */
char pal_at2857[0x10] __attribute__((aligned(16))) __attribute__((section(".rodata"))) = " %d \tis \t\t%d\n";
INCLUDE_ASM("asm/pal/nonmatchings/shop", ItemShopKey2__Fv);
unsigned int pal_at2690[4] __attribute__((aligned(16))) = {0x64, 0x3C, 0x28};
#pragma name_counter 2824
```

Read those carefully — they are direct evidence for §2.3:

```
FINDING: The global symbols `pal_at1188__2`, `pal_at2857` and `pal_at2690` are retail's `@<n>` float
         literal names, transcribed into C as ordinary globals, and each site carries the matching
         `#pragma name_counter` so the compiler's own counter resumes where retail's left off. The
         counter value is *restored to the state it would have had*, not merely set to match one
         function.
SCOPE:   TOOLCHAIN
EVIDENCE: src/shot_freefuncs.cpp:979-980, src/shop.cpp:3938-3942 ; the counter's meaning is
          documented at scripts/build/statefix.py:118-122
MEANS FOR US: Visible proof that the whole-program-counter theory is right, and a model for how to
          express it: state restoration is written *next to the function that needs it*, as a
         pragma plus named globals, not hidden in a central table. If GT4 has the same property,
         our own source will eventually need exactly this shape to be byte-exact.
CONFIDENCE: high
```

**Scale — how complete this actually is.** `find src -name '*.cpp' | xargs wc -l` = **150,858 lines
of C++**, with exactly **2 `INCLUDE_ASM` markers** remaining in the tree. For scale, the three
largest files are `src/dun/gameloop.cpp` (9,491), `src/battlemenu.cpp` (9,149) and
`src/editloop3.cpp` (7,705).

```
FINDING: DCDecomp is at a very high match rate — ~151k lines of C++ with 2 remaining INCLUDE_ASM
         markers — so it is a fair picture of what a FINISHED matching decomp looks like, not just
         a starting point.
SCOPE:   TOOLCHAIN
EVIDENCE: `find src -name '*.cpp' | xargs wc -l` → 150858 ; `grep -rn INCLUDE_ASM src/` → 2 hits
          (src/shot_freefuncs.cpp:977, src/shop.cpp:3939); both are `#if defined(PAL)`, i.e. the
          NTSC build is fully decompiled and the two stragglers are PAL-only.
MEANS FOR US: The realistic target for G10 is therefore known and finite, not open-ended. It also
          means the *workflow*, not the tooling, is where the effort went — worth reading §2.1–2.6
          as a process, not just as a tool list.
CONFIDENCE: high (counted directly)
```
## 2.5 The symbol table — and why a link map is not one

```
FINDING: DCDecomp's symbol table is the splat symbol index (`config/<region>/*.symbols.txt`) plus
         the per-function assembly layout, NOT the link map. Two reasons, both important: the index
         covers every function *including the ones already superseded by a compiled .cpp* (the link
         map does not), and it names them the way the compiler does (mangled) rather than the way
         mwld prints them (demangled, with spaces in the signature).
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/diff/ref_index.py:4-11 (verbatim quote); lookup prints
         `<section> <source> <vram> <size> <image>` with hex numbers and exits non-zero when the
         symbol is not indexed (ref_index.py:15-18); the index is fed by splat via
         `symbol_addrs_path` (config/ntsc/main.yaml:14) and indexed with `bisect` by address
         (ref_index.py:20, :39).
MEANS FOR US: **Directly actionable for GT4.** Our function inventory must come from something that
          survives re-compilation — a static index keyed by address, not a linker map that changes
          when the build changes. And keep BOTH spellings: mangled for matching against compiler
          output, demangled for reading. Note the search order too: main first, then the overlays,
          because "the overlays reuse its address space, so a plain search order would otherwise be
          ambiguous" — **the same trap applies to GT4**, where `GT4.VOL` overlays share one virtual
          address space with the main executable. Lookups there need the same explicit
          disambiguation.
CONFIDENCE: high. The "overlays reuse its address space" fact is HARDWARE-UNIVERSAL about how PS2
          overlays work, and it applies to GT4's VOL exactly as it does here.
```

**`decompile.sh <symbol>` — the m2c path, end to end:**

1. `scripts/diff/ref_index.py` locates the symbol → `<section> <source> <vram> <size> <image>`
   (`decompile.sh:54-63`).
2. `scripts/diff/m2c_prep.py` prepares the dump (`decompile.sh:66-68`).
3. `scripts/diff/m2ctx.py` generates `build/<region>/ctx.c` if missing, preferring the venv python
   because "m2ctx needs libclang's Python bindings, which the container keeps in its venv"
   (`decompile.sh:69-75`).
4. `m2c.py --target mipsee-mwcc-c++ --context ctx.c -f <symbol> <prepared.s>` piped through
   `m2c_calls.py` (`decompile.sh:77-81`). `--target mipsee-mwcc-c++` is the target triple **matching
   the game's compiler**, not a generic MIPS target.
5. `exit "${PIPESTATUS[0]}"` — the script's exit status is m2c's, not the filter's
   (`decompile.sh:80`).

**`m2c_prep.py` fixes two things that are pure spelling problems** (`m2c_prep.py:4-14`):

> "* a `jr` through a jump table. m2c only recognises a table whose label starts with `jtbl_`, `jpt_`,
> `lbl_` or `jumptable_`, and splat calls it `@N` like any other invented constant, so the function
> comes out as one failure comment. The table's own words are appended too, since m2c reads the
> branch targets out of them.
> * a `$gp` displacement. Left alone every small-data global reads as `saved_reg_gp->unk-6358`;
> resolved against `_gp` each one becomes the symbol the address belongs to."

```
FINDING: m2c depends on *label naming conventions* to recognise jump tables (`jtbl_`, `jpt_`,
         `lbl_`, `jumptable_`) and on `$gp` displacements being resolved to real symbols. Both are
         broken by the disassembler's output, not by the code — so a prep pass fixes them before m2c
         ever sees the dump.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/diff/m2c_prep.py:4-14 ; the regexes at :34-36 show the exact patterns
          (`%lo("...")`, `(-?0x..+)\(\$28\)`, `jr $<reg>`)
MEANS FOR US: A concrete, transferable reminder: **a decompiler's quality is bounded by how well its
          input is dressed, and a large fraction of "the decompiler is bad" is actually a naming
          convention.** When our own tooling reads GT4's disassembly, check whether its input needs
          the same dressing before judging its output. Also: `(-?0x...)\(\$28)` is the R5900
          `$gp`/`$t9` small-data displacement pattern — HARDWARE-UNIVERSAL, and it is how the EE
          addresses small data and is the reason `-G0` on the assembler side exists.
CONFIDENCE: high
```

**`m2c_calls.py` folds mangled calls back to source spelling** (`m2c_calls.py:2-14`):

> "m2c prints every call by the retail symbol, so a method reads as `Down__8CGamePadFi(&GamePad,
> 0x40)`. The mangling already says which argument is the receiver -- a method's first argument
> always is -- so the same call can be written the way the source had it, `GamePad.Down(0x40)`, and
> a free function can drop its argument encoding. ... Nothing here is a fact about the code: it is a
> rename, and a call it cannot parse is left exactly as m2c printed it."

The safety property is the good part: **it is a filter that degrades to "leave it alone"**, never to
"guess".

```
FINDING: DCDecomp's post-processing tools are explicitly non-inferring: m2c_calls.py "is a rename,
         and a call it cannot parse is left exactly as m2c printed it". m2c_prep.py handles only
         spellings it can pattern-match. m2ctx.py "handles two things here rather than by editing
         them" in the headers.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/diff/m2c_calls.py:13-14 ; scripts/diff/m2c_prep.py:1-19 ;
          scripts/diff/m2ctx.py:12-17 ("both handled here rather than by editing them")
MEANS FOR US: The house style is **transform in a tool, never edit the ground truth in place**, and
          every transform is allowed to decline. Both halves should be ours. A post-processing pass
          that "fixes up" something it is not certain about is how a plausible-looking wrong answer
          gets committed — exactly the failure in our Law 3.
CONFIDENCE: high
```

## 2.6 The context file — teaching a C decompiler about C++ headers

`scripts/diff/m2ctx.py` is a small, clever piece of engineering.

```
FINDING: m2c parses its context as **C**, so DCDecomp folds every project header into one C++
         file (`ctx.cpp`) and re-emits that content as C by running clang. "anything that compiles
          it -- decomp.me, with mwcc and -lang=c++ -- wants the C++ one." The C conversion keeps
          record layouts, enums, typedefs and free functions, and drops member functions, access
          specifiers and templates, which carry no layout m2c can use.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/diff/m2ctx.py:8-11 and :19-23 (the emission order — "enums first (C has no
          incomplete enum type), then forward typedefs so bare C++ names resolve, then the remaining
          typedefs, the records that use them, and finally functions"); the libclang dependency is
          documented at Dockerfile:60-63
MEANS FOR US: The *idea* is the transferable part — if our tooling ever needs a structural model of
          GT4's types to do its job, it must be generated from GT4's headers, and the target
          language of that model is chosen for the consumer's benefit, not the producer's. The
          specific C-vs-C++ problem is DCDecomp/m2c-specific.
CONFIDENCE: high
```

Two more details that are quietly excellent:

- **`-ffreestanding` keeps the host's system headers out of a context describing a PlayStation 2
  binary** (`m2ctx.py:37-39`). The generated model must describe the *target*, never the machine
  that generated it. `M2CTX` is the escape hatch for headers that need to behave differently here.
- **It does not edit the headers to make them work** — it handles the two problems in the tool
  (`m2ctx.py:12-17`): `common.h`'s `STATIC_ASSERT` pastes `__COUNTER__` instead of expanding it, so
  every use declares the same typedef name; and `CRunScript` is defined twice.

```
FINDING: The generation flags deliberately define away the target's own language extensions —
         `-D__attribute__(...)=`, `-D__asm__(...)=`, `-DSCRIPT(...)={}` — rather than allowing the
         host compiler's interpretations to leak into a description of the target.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/diff/m2ctx.py:40-50 (`CPP_FLAGS`)
MEANS FOR US: Any tool we build that models GT4 (struct layouts, type sizes, call graphs) must
          neutralise host-compiler extensions or it will silently describe the host instead of the
          EE. `_MIPS_SZLONG=32` and `-D_LANGUAGE_C` in that list are the EE-specific facts, not
          decoration.
CONFIDENCE: high
```
## 2.7 Ghidra's role — annotations that must be evidence-bound and are verified

DCDecomp uses Ghidra as a **naming and decompilation aid**, never as the source of truth, and the
scripts enforce that:

- `DarkCloudApplyAnnotations.java` — "Apply **evidence-bound** Dark Cloud names, types, signatures,
  and comments", writing into a `/DarkCloud` type category.
- `DarkCloudVerifyAnnotations.java` — "**Verify persisted** Dark Cloud annotations **against their
  retail evidence**." So annotations are not trusted; there is a checker.
- `DarkCloudInventory.java` — "Report the **persistent analysis state** of the Dark Cloud retail
  executable" (functions, named vs default, memory blocks, language id, compiler spec, image base).
- `DarkCloudExportDecompilation.java` — "Export manifest functions through Ghidra's decompiler,
  **grouped by translation unit**", with a timeout (`DC_DECOMPILE_TIMEOUT`, default 120s).

```
FINDING: Ghidra output is treated as derived material with a verification step, not as truth: there
         is a manifest (`config/ntsc/ghidra_annotations.json`, 725 KB), an apply script, and a
         separate verify script that checks persisted annotations against retail evidence.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/ghidra/DarkCloudApplyAnnotations.java:1 ; DarkCloudVerifyAnnotations.java:1-2 ;
          DarkCloudInventory.java:1-2 ; DarkCloudExportDecompilation.java:1-2 ;
          scripts/ghidra/export_decompilation.sh:1-11, :15-20
MEANS FOR US: **This is the pattern we want for GT4.** A Ghidra-derived annotation set is cheap to
          produce and very easy to over-trust. Making "apply" and "verify" separate scripts, with
          verify checking each annotation back against the retail bytes, is what stops a plausible
          name from becoming a load-bearing fiction. Adopting Ghidra for GT4 is fine — adopting it
          *unguarded* is how our own Law 3 gets violated by accident.
CONFIDENCE: high
```

Two more details:

- **Export is grouped by translation unit, with the unit name validated** against
  `[A-Za-z0-9_./-]+` before it is used as a path (`DarkCloudExportDecompilation.java:24-25`). Script
  input derived from a manifest should not be trusted to be a safe filename.
- **Ghidra runs on the HOST, not in the container** — `generate_dark_cloud_annotations.py` "must run
  on the host because it reads the retail ELF under `re/`" (`export_decompilation.sh:8-10`). The
  container holds the toolchain and the compilers; the Ghidra project is private host state.

```
FINDING: `progress_report.py` carries an explicit warning about its own metric: "Objdiff's data
         score includes sections it cannot compare. It is not a byte-for-byte linked-image
         comparison, so do not call its gap a diff."
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/progress_report.py:59-60
MEANS FOR US: **Write the caveat next to the number, not in a footnote somewhere.** Any metric we
          publish to the captain must carry its own limitation in the same place, or it will be
          quoted without it and we will have made a claim we cannot support. This is our Law 2 and
          Law 4 in practice, and it costs one line.
CONFIDENCE: high
```

**One inconsistency worth recording as a warning** — `progress_report.py:37-40` prints
`f"0 asm, {unmatched} unmatched"` with a **literal 0**, and the `Asm` row at :55 is likewise
`0.0, 0`. But `scripts/build/verify.py` *does* have a real ASM category (`verify.py:296-300`). So the
objdiff-based report cannot distinguish ASM-supplied functions and hardcodes zero, while the
byte-verifier can. Anyone reading only the progress report will over-count "perfect" relative to
`verify.py`'s stricter reading.

```
FINDING: The two verifiers disagree about the ASM category: `verify.py` classifies INCLUDE_ASM
         functions as ASM and subtracts them from perfect, but `progress_report.py` prints a
         hardcoded `0 asm` and a hardcoded `0.0` for that row.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/progress_report.py:37-40 and :52-57 (literal 0 / 0.0) vs
          scripts/build/verify.py:39 (ASM is a real category), :296-300 (ASM demotes PERFECT)
MEANS FOR US: A warning about borrowing a number without reading the code that makes it. If we ever
          publish two verifiers, they must agree by construction, not by luck — and the category a
          number excludes must be visible in the number's own output. Worth checking against
          `docs/STATUS.md` for the same class of drift.
CONFIDENCE: high (both files read directly; the NTSC build genuinely has 0 asm markers today, so
          the discrepancy is invisible *right now* and would appear on the next partial unit)
```

---

## 2.8 Mismatch detection and repair — the part that actually closes functions

The brief asked specifically about mismatch detection. DCDecomp's answer is that **detection and
repair are separate tools**, and the repair tools are the interesting half.

**Detection — three levels of "what differs now":**

| Tool | Answers |
|---|---|
| `objdiff` / `diff.sh` | does this one function match, and if not, how does the IR differ |
| `scripts/build/classify.py` | "Every unmatched function, sorted by **how** it differs from retail's" — clusters failures by cause so the next fix is chosen by data, not by taste |
| `scripts/build/fdiff.py` | "One function, retail's beside ours, in any image" — a raw side-by-side, for when you want to look yourself |

`classify.py` parses both ELFs directly (its own `elf_text()` reads `e_shoff` at 0x20 and section
headers at 0x2E, collecting `SHT_PROGBITS` sections with a nonzero address) and sorts by difference
shape rather than by name — that is the point.

**Repair — three tools, each fixing a different leaked-state mechanism:**

```
FINDING: `argfix.py` pins, for ONE function, the byte that call lowering reads for each float
         argument — and it does so by *searching*, not guessing: (1) take the compiler's own residue
         at every read and pin it, which reproduces what the unit already builds AND makes it
         reproducible; (2) toggle each read in turn and record which instructions move, keeping the
         *window* around the ones that respond, because "a read that changes nothing on its own can
         still matter in company"; (3) cluster the function's wrong instructions by call site and
         search the window until the cluster is clean. It prints the `#pragma argument_flag_ones`
         block the source needs.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/argfix.py:2-22
MEANS FOR US: **The shape is the lesson: a repair tool that ends by emitting a source-level
          annotation, derived by measurement.** Step 2 in particular — treat a single toggle as
          inconclusive and keep a *window* — is how you avoid overfitting a fix to the one function
          you are looking at. Worth remembering for any automated-fix work.
CONFIDENCE: high
```

```
FINDING: The difference between the two float-constant mechanisms is precise and is why there are
         two tools. The **expression-node override** sets the byte per float *constant*
         (`config/<region>/expression_node_overrides.json`, 1.3 MB, keyed by translation unit →
         function → ordinal → value bits → evaluate_first). It "cannot separate repeated reads of the
         same node"; `argfix.py` sets it per *read*, which can.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/argfix.py:8-9 ; scripts/build/statefix.py:153-155 (`PRAGMAS` tuple names the
          source-level hooks, and the comment says the remaining non-expression globals use source
          pragmas while expression constants use the external exact-identity config)
MEANS FOR US: Worth noting the **shape of the override file**: a 1.3 MB JSON keyed
          translation-unit/function/ordinal/value-bits/flag, validated on load with explicit range
          and duplicate checks (statefix.py:153-200 rejects duplicate node identities and out-of-range
          bits). If we ever need a large behavioural corpus for GT4, this is a proven layout —
          and note that it is keyed by *exact value bits*, not by a float literal, so two constants
          that reach the same bits are the same key.
CONFIDENCE: high
```

```
FINDING: `literals.py` documents the entire float-literal placement mechanism, and says plainly that
         it was established **by experiment, because the manual describes only the first line of
         it** — and it notes that MWCC 2.3 (also in the tree) behaves identically.
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/literals.py:12-30
MEANS FOR US: Two things. (1) The mechanism, which is fully general to any MWCC-built PS2 game and
          therefore a candidate for GT4: each *distinct* float constant of a TU gets its own
          `.lit4`/`.lit8` section holding one value, with a local `@<digits>` symbol, loaded through
          an `R_MIPS_LITERAL` relocation on a gp-relative `lwc1`/`ld`; numbers rise in order of
          first use, come off the same counter as other invented names so they are **not
          contiguous**, and the sections appear in the object in **reverse** order; dedup is per-TU
          with no limit, and constants that reach the same bits by another route land on the same
          section. (2) **The practice:** when a toolchain's behaviour is not documented, determine it
          by experiment and write down that you did, and which version you did it against.
CONFIDENCE: high (stated as measured, and it explains the `@<n>` names in §2.3)
```

```
FINDING: `check_unmatched.py` compiles every source with `NON_MATCHING` defined as a **syntax and
         type check of the drafts**, throwing the object away and bypassing mwccgap — because a draft
         behind `#ifdef NON_MATCHING` "is never compiled by the build -- that is the point of the
         guard -- so nothing otherwise stops it from being written against a field that does not
         exist."
SCOPE:   TOOLCHAIN
EVIDENCE: scripts/build/check_unmatched.py:2-16
MEANS FOR US: **A `#ifdef` guard is a promise nobody keeps by default, and this is the fix.** Any
          project that parks work behind a disabled guard needs a checker that compiles the parked
          work, or the park becomes a landfill. We have exactly this shape already — VULCAN 4 uses
          `VULCAN 4 LIMITATION:` markers for unimplemented paths, and those markers are exactly as
          prone to rot as a never-compiled `#ifdef`. **We should compile-check what is behind our
          limitation markers.** Cheap, and it is the difference between a deferral and an abandonment.
CONFIDENCE: high
```

---

# 3. HARDWARE ACCESS STRUCTURE

*How a finished PS2 game talks to the machine in readable C. These are the patterns VULCAN 4's
GS (G2) and VU1 (G3) lanes need, so this section is the most directly load-bearing one.*

**The headline: this game touches raw hardware memory exactly once.**

```
FINDING: Across ~151k lines of decompiled C++, the ONLY raw MMIO address in the entire tree is
         `*(volatile u_int *) 0x10000000` — the EE Timer 0 count register — used four times, all in
         mglib.cpp, to time a frame. Everything else goes through the SDK.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: `grep -rhoE "\(volatile u_[i|h]?[a-z]*t \*\) 0x..." src/` → 4 hits, all
          `(volatile u_int *) 0x10000000`; src/mglib.cpp:372 (MGBeginFrame, top-of-frame stamp),
          :459 (frame cost print), :501 and :525 (in MGEndFrame)
MEANS FOR US: **The strongest architectural lesson in this document.** A finished PS2 game does not
          program the hardware — it programs the *SDK*, which programs the hardware. For VULCAN 4
          that means the interface we must supply is not "the GS registers" but "what
          `sceGsSetDBuff` / `sceGsSwapDBuff` / `sceVif1Pk*` / `sceDmaSend` / `sceSif*` do". Our
          support layer must be the **SIF/GS/VIF/EE SDK surface**, because that is what GT4's own
          code will call. If GT4 calls `sceGsSetDBuff` and we only implement raw register writes,
          nothing runs. Conversely this bounds the work: one raw address in a whole game means the
          hardware contract is small and knowable.
CONFIDENCE: high for Dark Cloud (counted directly). For GT4: high expectation, but it must be
          confirmed against GT4's own calls — see `NEED:` in Status.
```

## 3.1 Boot: the IOP reboot and IRX module load sequence

This is the single most relevant function in the whole tree for VULCAN 4's G1 wall.

```
FINDING: `init_all()` (src/main.cpp:282-321) is the canonical PS2 boot sequence, and it is
         recoverable from the decompilation in full, step by step, with every SIF call named.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/main.cpp:282-321
MEANS FOR US: This is a checklist for what "no BIOS in the path" actually means, in order.
CONFIDENCE: high
```

```c
void init_all() {
    sceSifInitRpc(0);
    sceCdInit(0);
    sceCdMmode(2);
    while (!sceSifRebootIop("cdrom0:\\MODULES\\IOPRP211.IMG;1")) { }
    while (!sceSifSyncIop()) { }
    sceSifInitRpc(0);                       // <-- AGAIN, after the reboot
    sceCdInit(0);                          // <-- and again
    sceCdMmode(2);
    sceFsReset();
    while (sceSifLoadModule("cdrom0:\\MODULES\\SIO2MAN.IRX;1", 0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\PADMAN.IRX;1",  0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\MCMAN.IRX;1",   0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\MCSERV.IRX;1",  0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\LIBSD.IRX;1",   0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\SDRDRV.IRX;1",  0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\MODMIDI.IRX;1", 0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\MODHSYN.IRX;1", 0, 0) < 0) { }
    while (sceSifLoadModule("cdrom0:\\MODULES\\EZMIDI.IRX;1",  0, 0) < 0) { }
    InitCDFile(); DevInit();
    d1 = sceDmaGetChan(1); d2 = sceDmaGetChan(2); d8 = sceDmaGetChan(8);
    MGInit();
    InitMemoryFile(); BufferAllClear(); InitReadBG();
}
```

Points that matter to us, in order of how much they will save us:

- **`sceSifInitRpc(0)` / `sceCdInit(0)` / `sceCdMmode(2)` are called TWICE — once before the IOP
  reboot and once after.** The first set configures the pre-reboot IOP; the reboot throws all of it
  away. This is the standard PS2 idiom and it is easy to "optimise away" in a reimplementation,
  which then breaks.
- **Every wait is a `while` loop that spins until success**, never a sleep and never an error path.
  `sceSifRebootIop` must return 0; `sceSifLoadModule` must return >= 0. Our SIF/IOP layer therefore
  has to be *actually able to fail and then succeed*, or these loops are unhangable. This is a
  concrete G1 requirement.
- **The IOP is rebooted from an image on the disc**: `cdrom0:\MODULES\IOPRP211.IMG;1`. VULCAN 4's
  Law 1 says the user's disc is the only input — so the IOP image comes off their disc, exactly as
  here. **We do not ship an IOP replacement.** Good: the boot path becomes tractable.
- **`sceFsReset()`** is called before any module load, and the `sifrpc`/`sifdev` headers are pulled
  in at `src/main.cpp:4-5`.
- **Nine IRX modules** are loaded: SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD, SDRDRV, MODMIDI, MODHSYN,
  EZMIDI. Map them to our Stage 1 goals: **SIO2MAN = pad/serial (input, goal 4)**, **MCMAN/MCSERV =
  memory card (saves)**, **SDRDRV = SD/music data (goal 6, audio)**, **LIBSD = file system**,
  **MODMIDI/MODHSYN/EZMIDI = the audio stack (goal 6)**. Every one of these is an IOP-side program,
  so a "no BIOS" recomp has to supply IOP equivalents for whichever it needs. **That is the real
  cost of "no BIOS", and this function is the bill.**
- **Overlay loading is a separate concern**: `LoadOverlay(int mode)` (`src/main.cpp:268-280`) calls
  `mwLoadOverlay(path, address)` with `path = "cdrom0:\" + binfile[mode] + ";1"` and
  `address = _overlay_group_addresses[1]`. Confirms the overlay model: overlays are loaded into a
  caller-supplied address in a shared address space — the same property that made `ref_index.py`'s
  search order non-trivial (§2.5), and the same one GT4's `GT4.VOL` has.

```
FINDING: The PS2 boot contract is: SIF RPC init → CD init → reboot IOP from disc → sync IOP →
         re-init RPC and CD → fs reset → load each IRX module by name, spinning until each loads →
         open DMA channels → init graphics. It is fully expressible without a BIOS, but every
         IRX module is IOP-side code that a no-BIOS target must replace or do without.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/main.cpp:282-321 (init_all), :268-280 (LoadOverlay / mwLoadOverlay)
MEANS FOR US: For G1, the honest sequence is: (a) make SIF RPC work to the degree the boot loop
          needs, (b) provide an IOP that answers `sceSifRebootIop`/`sceSifSyncIop` from the disc's
          own IOPRP image OR run without it if GT4's own boot tolerates it, (c) accept or replace
          the nine modules in dependency order, starting with SIO2MAN because without pad there is
          no input (goal 4). **This ordering should drive our G1 plan.** It is also the strongest
          available argument for *not* trying to be a general PS2 emulator: we need exactly these
          nine modules and GT4's calls into them.
CONFIDENCE: high for the sequence; medium for which modules GT4 needs — that must come from GT4's
          own `init_all`-equivalent, not from this file. Game-specific parts of the module list are
          Dark Cloud's needs, not a rule for GT4.
```

## 3.2 DMA channels and the GIF/VIF packet path

```
FINDING: The game claims DMA channels **1, 2 and 8** (`sceDmaGetChan`), sets channel 1's `chcr.TTE`
         (transfer-trigger-enable) before every send, and drives the GS entirely through a
         VIF1 packet builder writing into a caller-supplied buffer.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:126-131 (MGInit: DmaCH1/2/8, "The frame is a source chain rather than a flat
          transfer, so channel 1 has to read the tags it is handed instead of treating them as
          data" → TTE=1) ; src/main.cpp:314-316 ; src/mglib.cpp:499, 557-558, 560 (the same TTE=1
          discipline repeated before each send)
MEANS FOR US: Two things. (1) **Our DMA emulation must support source-chain (TTE) mode**, because
          PS2 GS traffic is nearly always a linked list of DMA tags walked by channel 1 — a flat
          linear copy is not enough and would be a rewrite of the whole GS path. (2) The repeated
          `DmaCH1->chcr.TTE = 1` immediately before each `sceDmaSend` is a real discipline: whatever
          else happened to that channel in between, TTE is reasserted every time. Our DMA channel
          state must be per-channel and mutable the same way.
CONFIDENCE: high
```

**The VIF1 packet builder call sequence is the GS packet grammar, in full:**

```
FINDING: GS work is emitted as an explicit, readable call sequence — sceVif1PkCnt → PkOpenDirectCode
         → PkOpenGifTag → a run of PkAddGsAD(reg, value) → PkCloseGifTag → PkCloseDirectCode, then
         PkEnd + PkTerminate + sceGsSyncPath at end of frame, then sceDmaSend on channel 1.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:1557-1614 (MGClearScreen — the clearest single example: three successive
          GIF-tag groups, each opened and closed), :458-459 (PkEnd/PkTerminate),
          :547 (sceGsSyncPath), :560 (sceDmaSend)
MEANS FOR US: **This is the shape our GS layer has to accept.** Note what it implies: the game does
          not hand the GS a blob; it asks a packet builder to emit GIF tags into a ring, and the
          hardware reads them. So our emulator needs a *packet builder that produces GIF tags*, and
          the registers named by `sceVif1PkAddGsAD(Vif1Packet, SCE_GS_XXX, value)` — not a
          "draw a triangle" API. This is the concrete answer to "what does G2 have to be".
CONFIDENCE: high
```

The canonical GS register numbers are all in one place, `include/sce/libgraph.h:22-41`, and they are
**HARDWARE-UNIVERSAL** (GS register IDs are the same silicon everywhere):

```
SCE_GS_PRIM 0 · SCE_GS_RGBAQ 1 · SCE_GS_UV 3 · SCE_GS_XYZF2 4 · SCE_GS_XYZ2 5
SCE_GS_TEX0_1 6 · SCE_GS_CLAMP_1 8 · SCE_GS_XYZF3 12 · SCE_GS_TEX1_1 20
SCE_GS_TEXA 59 · SCE_GS_TEXFLUSH 63 · SCE_GS_SCISSOR_1 64 · SCE_GS_ALPHA_1 66
SCE_GS_TEST_1 71 · SCE_GS_FRAME_1 76 · SCE_GS_ZBUF_1 78 · SCE_GS_BITBLTBUF 80
SCE_GS_TRXPOS 81 · SCE_GS_TRXREG 82 · SCE_GS_TRXDIR 83
```

**`MGClearScreen` in full is the best single reference in the document for GS state setup** — it
shows the save/restore discipline that any correct GS layer must get right:

```c
// src/mglib.cpp:1557-1614 (abridged — the shape is what matters)
// 1. flush the texture cache through a one-register GIF tag
sceVif1PkCnt(Vif1Packet, 0); sceVif1PkOpenDirectCode(Vif1Packet, 0);
sceVif1PkOpenGifTag(Vif1Packet, *(u_long128 *) &GiftagAD);
sceVif1PkAddGsAD(Vif1Packet, SCE_GS_TEXFLUSH, 0);
sceVif1PkCloseGifTag(Vif1Packet); sceVif1PkCloseDirectCode(Vif1Packet);

// 2. a second tag: override test/zbuf/alpha, draw one screen-sized SPRITE, flush again
sceVif1PkCnt(...); PkOpenDirectCode(...); PkOpenGifTag(...);
    test.bits.ate = 0; test.bits.zte = 1; test.bits.ztst = SCE_GS_ALWAYS; test.bits.date = 0;
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_TEST_1,  *(u_long *) &test);
    zbuf.bits.zmsk = 0;
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_ZBUF_1,  *(u_long *) &zbuf);
    alpha.bits.a = 2; alpha.bits.b = 2; alpha.bits.c = 2; alpha.bits.d = 0;
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_ALPHA_1, *(u_long *) &alpha);
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_TEX1_1, 1);
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_PRIM,
                     SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 0,0,0, 1,0,1, 0,0));
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_RGBAQ, SCE_GS_SET_RGBAQ(r,g,b,a,0));
    for (x = 0; x < 640*16; x += 16*16) {          // 16-pixel-wide vertical strips
        sceVif1PkAddGsAD(Vif1Packet, SCE_GS_XYZF2, SCE_GS_SET_XYZF2(27648+x, GS_Y_OFFSET, 0, 0));
        sceVif1PkAddGsAD(Vif1Packet, SCE_GS_XYZF2,
                         SCE_GS_SET_XYZF2(27648+x+16*16, GS_Y_OFFSET+SCREEN_HALF_HEIGHT*16, 0,0));
    }
    sceVif1PkAddGsAD(Vif1Packet, SCE_GS_TEXFLUSH, 0);
PkCloseGifTag(...); PkCloseDirectCode(...);

// 3. and a third tag that puts the game's own persistent state back
sceVif1PkAddGsAD(Vif1Packet, SCE_GS_TEST_1,  *(u_long *) &mgPixelTest);
sceVif1PkAddGsAD(Vif1Packet, SCE_GS_ZBUF_1,  *(u_long *) &mgZBuffer);
sceVif1PkAddGsAD(Vif1Packet, SCE_GS_ALPHA_1, *(u_long *) &mgAlpha);
```

```
FINDING: GS register writes are **not stateful in the C** — the game holds its own copies
         (mgPixelTest, mgZBuffer, mgAlpha, mgTEX1Env) and writes them into the GS explicitly, in
         explicit order, restoring them after any temporary override. The GS is treated as a
         stateless register file driven by a stream.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:1557-1614 (MGClearScreen: override → draw → restore) ;
          src/mglib.cpp:182-200 (MGInit: "The two registers the game edits per draw start from
          whatever the SDK's own drawing environment put in them, so they are copied out of it
          rather than built from nothing" — mgPixelTest = mgDBuff.draw0.test1, mgZBuffer =
          mgDBuff.draw0.zbuf1)
MEANS FOR US: **Our GS layer must be a faithful register file where writes take effect in stream
          order and nothing is implicitly remembered between packets** — because that is what the
          game's own code assumes. If our GS quietly caches "current alpha" and applies it
          differently, GT4 will produce subtly wrong images and we will chase it for a week. This
          is a concrete design constraint on G2, and it is the opposite of what a "scene graph"
          style renderer would do.
CONFIDENCE: high
```

Two more details in `MGInit` worth having:

```
FINDING: `mgPixelTest` and `mgZBuffer` are initialised by COPYING them out of the SDK's own drawing
         environment (`mgDBuff.draw0.test1`, `mgDBuff.draw0.zbuf1`) rather than being built from
         zero — "so they are copied out of it rather than built from nothing".
SCOPE:   TOOLCHAIN (a decompilation note) with HARDWARE-UNIVERSAL consequence
EVIDENCE: src/mglib.cpp:182-190
MEANS FOR US: **A trap for any reimplementation.** `sceGsSetDBuff` leaves behind a draw environment
          with specific defaults, and this game *depends on those defaults* — it inherits TEST and
          ZBUF from them. If our `sceGsSetDBuff` fills those registers with zeros or "sensible"
          values, every subsequent draw in the game is wrong. Our SDK stand-ins must reproduce the
          **retail SDK's defaults exactly**, and the honest way to get them is out of GT4's own
          image, not by guessing. This is a concrete, high-value `NEED:` for G2.
CONFIDENCE: high for the dependency; medium for which values (needs GT4's own image).
```

## 3.3 The frame-start packet lives in data, not in code

```
FINDING: The VU1 startup packet is a hand-authored array of 128-bit words in a dedicated `.vudata`
         section, sent as a source chain: `FlushCache(0); sceDmaSend(d1, My_dma_start0);
         sceGsSyncPath(0,0); FlushCache(0); sceDmaSend(d1, Vu_progmain); sceGsSyncPath(0,0);`
SCOPE:   HARDWARE-UNIVERSAL (the mechanism) / GAME-SPECIFIC (the contents)
EVIDENCE: src/main.cpp:563-569 and :657-661 (both send sites) ;
          src/vudata.cpp:5-15 — "The DMA chain and GS environment the frame starts with",
          `u_int My_dma_start0[20] __attribute__((section(".vudata")))` ;
          src/vudata.cpp:18-30 — `My_DrawEnv[44]` in the same section
MEANS FOR US: Two things. (1) **The mechanism**: the GS startup packet is data, not code, and it is
          sent by DMA chain with a `FlushCache` before and a `sceGsSyncPath` between — that
          flush/sync discipline is exactly what a host-side emulator has to respect or it will
          see stale/unsynchronised state. (2) **The contents are Dark Cloud's, and are labelled
          GAME-SPECIFIC.** Do not carry `My_dma_start0` or `My_DrawEnv` across to GT4 as a pattern;
          take the shape only. The section layout (`.vutext` for VU1 code, `.vudata` for VU1 data) is
          the interesting transferable idea — a linker script with dedicated VU sections — and even
          that must be checked against GT4's own linker script.
CONFIDENCE: high for the mechanism; the specific 20 words are GAME-SPECIFIC and must not be reused.
```

The `.vutext` section is where the VU1 microcode itself lives, and it is written as readable
initialised data with jump targets marked:

```
FINDING: VU1 microprograms are stored as `u_int name[N] __attribute__((section(".vutext")))`
         initialiser arrays, with the words as literal VU1 instruction pairs and jump targets
         annotated as comments (`// _$J1`, `// _$table_start`).
SCOPE:   HARDWARE-UNIVERSAL (VU1 is VU1) / GAME-SPECIFIC (these particular programs)
EVIDENCE: src/vutext.cpp:5-6 — "The VU1 microprograms the renderers upload, in the order retail lays
          them out. The words are VU1 instruction pairs; the comments mark the jump targets the
          programs branch to." ; src/vutext.cpp:9 begins `u_int Vu_prog0[896] ... = { 0x100000DE, ... }`
          with `// _$table_start` at :11 and `// _$J1` … `// _$J14` through the table
MEANS FOR US: **Two concrete things for G3.** (a) The microcode is DATA in the ELF, in a named
          section, loaded by DMA — so our VU1 lane must be able to read a microcode block out of
          GT4's image at a known address and feed it to the VU1 emulator. It is not code we
          translate; it is a payload we execute. (b) Retail's linker **kept the internal jump-target
          labels** (`_$J1`…), which is why they can be recovered at all. If GT4's linker did the
          same, we get our VU1 jump targets for free from its symbol table — worth checking early,
          it turns VU1 from guesswork into transcription. The `896` words and these particular
          program names are GAME-SPECIFIC.
CONFIDENCE: high
```

## 3.4 VU1 invocation and matrix work

```
FINDING: The renderer hands VU1 a packet pointer and gets back an advanced pointer —
         `packet += visual->DrawVu1(packet, matrix, info, VU1_PROGRAM_UNKNOWN6, 0, 0, 0);`
         — and there are named microcode selectors (`VU1_PROGRAM_UNKNOWN6`) plus a
         `SetGsReg3(packet, ...)` helper that writes three GS registers into the raw packet.
SCOPE:   HARDWARE-UNIVERSAL (the contract) / GAME-SPECIFIC (the program set)
EVIDENCE: src/frame.cpp:988 (`int CFrameVu1::DrawVu1(unsigned int *packet, RenderInfo *info)`),
          src/frame.cpp:1232, :1240-1241
MEANS FOR US: The contract — *VU1 output is written into the same packet buffer the GIF stream is
          being built in, interleaved with GS register writes, and the routine advances the pointer*
          — is universal and is a real constraint on our GS/VU1 split. A design where VU1 and GS
          are separate pipelines that hand off at the end would not match this calling convention.
          Note also `VU1_PROGRAM_UNKNOWN6`: a named-but-unknown microcode selector, which is an
          honest refusal and exactly the labelling our Law 3 wants.
CONFIDENCE: high for the contract; the specific program enum is GAME-SPECIFIC.
```

Matrix work goes entirely through VU0 — `sceVu0UnitMatrix`, `sceVu0CopyMatrix`, `sceVu0RotMatrixX/Y/Z`,
`sceVu0AddVector`, `sceVu0CopyVector` — with the vectors and matrices living in `.vudata`
(`include/vudata.hpp`, `mgZeroVector2`/`mgUnitVector2`, `mgUnitMatrix`/`mgZeroMatrix`).

```
FINDING: This game uses **VU0 for all scalar/matrix math and VU1 only for the per-frame geometry
         transform** — the classic split. VU0 holds the vectors and matrices (`.vudata`); VU1 holds
         the microcode (`.vutext`) and is fed by DMA.
SCOPE:   HARDWARE-UNIVERSAL (the conventional division) — verify against GT4's own code
EVIDENCE: src/mglib.cpp:210-228 (MGInit building VU0 vectors/matrices with sceVu0* calls) ;
          src/frame.cpp:43-141 (MulFrameMatrix/ScaleMatrix/CopyMatrix/ZeroMatrix written as static
          inlines over sceVu0 types), :299-301, :441-521 (CFrame::GetLWMatrix walking the frame
          hierarchy with sceVu0CopyMatrix/RotMatrix/AddVector)
MEANS FOR US: For G3, the VU1 lane needs VU0's memory to be coherent because the matrices VU1 reads
          were written by VU0 (or by the EE). **A host-side VU1 emulator that cannot read the data
          VU0 wrote will not work**, so VU0 and VU1 memory have to be one shared buffer with VU1
          reads observing VU0 writes. That is a specific architectural requirement for our VU1
          plan and it is worth stating in docs/VU1-PLAN.md. Whether GT4 uses the same division
          must be checked against GT4's own code — do not assume it.
CONFIDENCE: high for Dark Cloud; the transfer to GT4 needs GT4's own evidence.
```

## 3.5 Timers, vsync and interrupts

```
FINDING: Frame timing is read straight from EE Timer 0 (`0x10000000`) and divided by **262** to get
         a percentage — 262 lines is one NTSC field. Vsync is a registered GS callback
         (`sceGsSyncVCallback(VSyncCallBack)`) that increments a counter the frame loop polls.
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:226 (`sceGsSyncVCallback(VSyncCallBack)`) ;
          src/mglib.cpp:372 (`h_count = *(volatile u_int *) 0x10000000` — "Timer 0's count at the
          top of the frame, which MGEndFrame subtracts from the count at the bottom to say how long
          the frame took") ; src/mglib.cpp:459 and :525 (`/ 262.0f`)
MEANS FOR US: **Two hard requirements, both easy to miss.** (1) Our EE must expose a readable
          Timer 0 at 0x10000000 — if GT4 reads it, we must model the EE's timers with the right
          clock rate, or every timing-derived number in the game is wrong. (2) `sceGsSyncVCallback`
          must actually **call back**, and our frame pacing must deliver vsync interrupts, because
          the game's whole frame loop is driven by that counter. This is where "draw a picture"
          stops being enough and becomes "the game must believe it is running at 60Hz".
          262 = NTSC field lines is HARDWARE-UNIVERSAL; PAL's 313 is why the PAL branch exists
          (:138-144, `mgDBuff.disp0.display.DY = 88`).
CONFIDENCE: high
```

```
FINDING: The vsync wait loop's delay is a body that negates its own counter twice, with the comment
         "The gap between polls has to cost something and do nothing, which is what a body that
         negates its own counter twice is: the count is the whole of the effect."
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:434-437 (`for (i = 0; i < 10; i++) { i = -i; i = -i; }`)
MEANS FOR US: A recognised PS2 idiom for an empty-but-not-optimised delay loop. **If our EE
          recompiler constant-folds `i = -i; i = -i;` away — and most will — any game code using this
          pattern will spin forever.** That is a genuine hazard for a *static recompiler*, unlike an
          emulator that runs the original code. Worth an explicit check in the recompiler's
          folding rules, and worth knowing if GT4 uses the same idiom.
CONFIDENCE: high for the idiom; the consequence for our recompiler is a prediction to verify.
```

```
FINDING: A callback is installed with a hand-rolled spin guard — `while (call_back_active) ;` — with
         the comment "Waiting out a handler that is already running is what keeps the pointer from
         changing under it."
SCOPE:   HARDWARE-UNIVERSAL
EVIDENCE: src/mglib.cpp:231-236 (MGInitVSyncCallBack)
MEANS FOR US: The game assumes the vsync handler runs **asynchronously** and mutates state the main
          loop reads. Our vsync delivery therefore has to be genuinely concurrent (or at least
          genuinely interleaved at the right points), not deferred to a safe point. If we deliver
          vsync callbacks only between frames, this code is safe by luck; if we deliver them at
          arbitrary points, it is correct by construction. Prefer the latter.
CONFIDENCE: high
```

**`MGEndFrame` is the frame-flip sequence, end to end** — this is the closest analogue to what our
GS lane must implement:

```
FINDING: End of frame is: PkEnd + PkTerminate → sceGsSyncPath (with a diagnostic dump of
         pBase/pCurrent and remaining buffer on failure, then Exit(-1)) → GPU picking by reading
         the Z buffer → TTE=1 → read Timer 0 → over_vsync bookkeeping → WaitVSync → sceGsSetHalfOffset
         → FlushCache(0) → sceGsSwapDBuff → sceDmaSync(DmaCH2) → re-claim channel 1, TTE=1 →
         FlushCache(0) → sceDmaSend(DmaCH1, Vif1Packet->pBase) → flip DBuffID.
SCOPE:   HARDWARE-UNIVERSAL (the discipline)
EVIDENCE: src/mglib.cpp:447-562, in that order (individual calls at :458, :459, :499, :501, :513,
          :525, :536-537, :545, :556-561)
MEANS FOR US: This is the **frame contract** in its entirety, and it is the checklist for our G2
          acceptance: terminate the packet, sync the path, allow a Z-buffer read-back, set the
          half-offset, flush cache, swap double buffers, sync the other DMA channel, then kick.
          Three things our GS layer must get right that a naive implementation would not: (a)
          `sceGsSyncPath` must be able to report failure with usable diagnostics (`Vif1Packet->pBase`,
          `pCurrent`, remaining buffer); (b) `sceGsSwapDBuff` and `sceGsSetHalfOffset` are ordered
          operations with the PAL branch tweaking display DX/DY only across the swap
          (src/mglib.cpp:531-542, :547-555 — "The display position is shifted by the screen
          adjustment only while the swap sends it"); (c) the picking read-back reads the Z buffer
          through VRAM after the frame, which means **our GS must support a host read of the depth
          buffer at the right moment** (src/mglib.cpp:467-497).
CONFIDENCE: high
```

## 3.6 Audio: the IOP heap is the audio transport

```
FINDING: Audio data is moved to the IOP with an explicit heap — `sceSifInitIopHeap()`,
         `sceSifAllocIopHeap(size + 0x10)`, `sceSifFreeIopHeap(addr)` — bracketing every buffer
         transfer, and the same pair appears around every MIDI bank/sequence allocation.
SCOPE:   HARDWARE-UNIVERSAL (SIF DMA heap is silicon-level)
EVIDENCE: src/sound.cpp:145-161 (bd_address / hd_address alloc, :158-161 the paired free+alloc) ;
          src/sound.cpp:348, :898-899, :903-904, :924-928, :933-934, :940-941, :963-964, :968-969,
          :988-989, :993-994, :1017-1018 (every MIDI port's alloc/free pair)
MEANS FOR US: **Directly relevant to our audio lane (goal 6).** If the audio path is
          `EE → SIF DMA → IOP heap → SPU2`, then the no-BIOS cost is real and specific: the IOP heap
          allocator and the SPUs have to exist. Every one of those `sceSifInitIopHeap()` calls is a
          **reset of the heap**, meaning all outstanding IOP pointers become invalid — so anything
          holding one across a call must be re-fetched. If our audio implementation does not model
          the IOP heap with that reset semantics, buffer lifetimes will be wrong in ways that look
          like corruption. **Model the heap as a resettable allocator with invalidation.**
          Which SPU/ADPCM path GT4 uses is GAME-SPECIFIC and must come from GT4's own code.
CONFIDENCE: high for the mechanism and the reset semantics; medium for GT4's audio path (unverified).
```

---

# 4. WHAT THEY DID NOT SOLVE

*Their unsolved problems are our warnings, and they are worth as much as their solved ones. This
section is deliberately the bluntest in the document.*

## 4.1 The most important thing in this entire document

```
FINDING: **MWCC matching compiles are NOT reproducible.** Because the float-constant path reads
         uninitialised memory, two identical runs of the same command can emit different code. Any
         function whose float arguments are constants may differ between runs — and, stated
         plainly, "one that matches may be matching by accident."
SCOPE:   TOOLCHAIN
EVIDENCE: docs/MWCC.md:94-98, verbatim: "Because §1.2 reads uninitialised memory, two identical
          runs can emit different code. Any function whose float arguments are constants may
          differ, and one that matches may be matching by accident."
MEANS FOR US: **This should change how we talk about our own results.** A byte-exact match against
          a compiler with this property is evidence, not proof, and a *reproducible* byte-exact
          match is stronger evidence than a one-off. For our G10 gate: **run the build twice and
          require the same bytes both times** before calling anything matched. That is cheap, it
          catches exactly this class of false positive, and it is the one thing DCDecomp's single-run
          CI cannot do for itself. If our gate cannot make a match reproducible, it must say so
          out loud rather than reporting a percentage.
CONFIDENCE: high (the project's own doc, stated without hedging)
```

## 4.2 Compiler internals they could not reach

```
FINDING: The compiler debugger has three declared blind spots, each with a stated reason:
         (1) the **scheduler is not profiled** — "CodeGen_Generator only reaches the scheduling
         pass when the optimization level byte is 3 or more, and the project compiles at -O2, so
         no scheduler anchor was recovered and none is guessed"; (2) the **frontend/IRO anchors
         are unrecovered** — the profile carries an empty `frontend_ir` section "so no frontend
         breakpoint is armed"; (3) nothing is recovered from a different MWCC version's profile.
SCOPE:   TOOLCHAIN
EVIDENCE: tools/mwcc-debug/README.md:81-90 ("Limits"); :58-62
MEANS FOR US: (a) Note the discipline in "**and none is guessed**" — an unrecovered anchor stays
          unrecovered. That is our Law 3 applied to tooling internals. (b) It also means the
          project cannot explain *scheduling* differences in its own output. If GT4's build used a
          higher optimization level than Dark Cloud's `-O2`, we would have even less to go on, so
          **GT4's actual optimization level is worth knowing** (part of `NEED:` (b)).
CONFIDENCE: high
```

## 4.3 Register allocation: known, documented failures with worked examples

The register allocator is the second big source of near-misses, and `docs/MWCC.md` §2 is a list of
**rule violations that cost real time**, each with the function that exposed it. That list is worth
more than the rules above it, because it is the shape of the failures to expect:

- A ternary-assigned local becomes the ternary's optimizer temporary, so *moving its declaration
  changes nothing*; written as `if`/`else` it keeps the declared local's number and declaration
  order works again (`EventItemSelectDraw`).
- A loop counter shared at function scope puts every loop in one register; declaring it in the
  `for` does not.
- **A different set of spilled values means a different counter layout** — which values spill
  depends on live-range lengths, not just on which registers they get. `DepthOfField` spilled two
  loop locals where retail spilled the `alpha` argument; the fix was giving one run-once loop its
  own counter rather than the shared `i`.
- **A node at exactly K when simplify reaches it** colours a pass late instead of in reverse-number
  order, pushing it *down* the registers, and every temporary around it shifts with it. The lever
  named is a **dead assignment** beside an existing one, which creates a CSE temp whose removal
  takes the address node under K without emitting anything (`CWater::CreateVUData`).
- A counter reused as a later loop's countdown is one node, and takes its colour from the loop with
  more pressure.

```
FINDING: MWCC's register allocator is graph-colouring with reverse-Chaitin-removal order, and its
         failures are *structural*, not stylistic: declaration order only steers callee-saved
         registers, a ternary-assigned local ignores declaration order entirely, and the SET of
         spilled values changes the register layout, not just the register choices.
SCOPE:   TOOLCHAIN
EVIDENCE: docs/MWCC.md:113-140 (§2 Register allocation, with the named failing functions
          EventItemSelectDraw, DepthOfField, CWater::CreateVUData)
MEANS FOR US: If we ever pursue byte-exactness for GT4 arithmetic, this is the shape of the fight,
          and the named lever — **a dead assignment used purely to create and remove a CSE temp** —
          is a striking one worth remembering. But the honest framing for VULCAN 4 is different:
          our gate is *behavioural*, so a different register choice is not a failure at all, it is
          invisible. This whole section is relevant only to our optional G10 byte-exact work, and
          it is the strongest argument for keeping G10 scoped to functions we actually need rather
          than trying to match everything.
CONFIDENCE: high
```

## 4.4 The match boundary: what is deliberately NOT matched

```
FINDING: The main executable's **symbol and string tables are not reproduced**, and matching them
         is explicitly "not a current priority". Verification therefore covers the executable only
         over a span, with both overlays required to be byte-identical.
SCOPE:   TOOLCHAIN
EVIDENCE: README.md — "Matching the main executable's symbol/string tables may be explored in
          future, though this isn't a current priority"; scripts/build/verify.py:376-382
MEANS FOR US: Worth stating plainly because it bounds the claim. "Matching decompilation" does not
          mean the whole image is reproduced — in this project it means the code and data are, and
          the tables are not. When we describe what we have achieved, use the same precision.
CONFIDENCE: high
```

```
FINDING: Two functions remain undecompiled, and both are PAL-only. The PAL ones are not stuck so
         much as *parked*: `shop.cpp:3935` `ItemShopKey2__Fv` and `shot_freefuncs.cpp:973`
         `BtStatusErrStep__Fv`, each with a written explanation of why the C does not reproduce PAL's
         bytes ("7 left -- 10.0f loads before the other arguments of AddNowLife; its evaluate_first
         1 orders it but swaps the status and chara registers").
SCOPE:   TOOLCHAIN (the workflow) / GAME-SPECIFIC (these functions)
EVIDENCE: src/shop.cpp:3935-3942 ; src/shot_freefuncs.cpp:973-980 ; docs/PAL.md:62-96
          ("Functions not yet decompiled for PAL")
MEANS FOR US: **The practice to copy is the comment, not the count.** Each parked function carries a
          precise statement of what differs and which specific mechanism causes it. That is what
          makes it resumable by someone else. When our G10 stalls on a function, the deliverable is
          a note in that shape — what differs, and why — not just "not done".
CONFIDENCE: high
```

## 4.5 Unexplained structures they left in place, on purpose

```
FINDING: `CDebugFont DebugFont;` is a **deliberate wrong-shaped stand-in** for an unidentified
         retail global (`nm` name `DebugFont`, 0x1ce7340, LOCAL, 0x21C bytes). The comment explains
         that "A same-sized stand-in is needed or this object's .bss runs 0x220 short and everything
         after it lands early", that declaring 0x220 made the symbol four bytes longer than retail's,
         and closes with `TODO: identify the real type; the name suggests a font glyph table.`
SCOPE:   TOOLCHAIN
EVIDENCE: src/main.cpp:135-144
MEANS FOR US: **A textbook example of the wrong kind of right, and worth studying precisely because
          it works.** A fake of the right SIZE makes every subsequent address land correctly, so the
          build matches — while nothing about the object is understood. This is exactly the
          plausible-looking stub our own AGENTS.md Law 3 warns about, and it is committed in a
          150k-line byte-exact project with a TODO next to it. Two lessons: (1) if we ever need such a
          stand-in, it must be labelled loudly at the point of use, as this one is, and (2) **a
          byte-exact build is not evidence that the sources are understood.** Matching and
          understanding are different axes. Our reports must not conflate them.
CONFIDENCE: high
```

```
FINDING: **310 functions carry a `@unknownret` Doxygen tag across 45 files** — meaning the return
         value's meaning was never determined — and unknowns are otherwise named as unknowns
         (`unknown1`, `unknown2` parameters throughout `visualvu1.cpp`/`cloth.cpp`/`visualshadow.cpp`;
         `VU1_PROGRAM_UNKNOWN6`; "Part 75 is unknown" in dungeonmap.cpp:374).
SCOPE:   TOOLCHAIN
EVIDENCE: `grep -rho "@unknownret" include/ src/ | wc -l` → 310, in 45 files; examples at
          include/battle_globals.hpp:198-246, include/btmisc.hpp:56, include/dataread.hpp:246,
          src/dungeonmap.cpp:374, src/cloth.cpp:430-447, src/visualvu1.cpp:202/381/491/868/890
MEANS FOR US: **Refusal at scale, and it is the most respectable thing in the project.** A finished,
          byte-exact decompilation of a 150k-line game still has 310 functions whose return value
          nobody knows. That is the realistic standard, and it is far better than a guessed one.
          It also gives us a metric for ourselves: we do not need to understand GT4's every return
          value to ship Stage 1 — we need to understand the ones on the path to a picture, a car, a
          track and a race.
CONFIDENCE: high
```

## 4.6 A codegen quirk that forced the *source shape*, recorded so nobody "fixes" it

```
FINDING: A `switch` was written where an `||` chain would do, because MWCC's codegen differs: as a
         chain it merges cases 9 and 10 into a range check (`addiu v0,v1,-9; sltiu at,v0,2`) which
         retail does not have — and "MWCC emits a sparse switch's comparisons in reverse written
         order, so these are written 9,7,10,14 to get retail's 14,10,7,9."
SCOPE:   TOOLCHAIN
EVIDENCE: src/main.cpp:548-561
MEANS FOR US: A general warning about matching projects: **source that looks redundant is often
          load-bearing, and the reason lives in a comment next to it.** If we ever normalise or
          "tidy" borrowed source, this is the class of edit that silently breaks byte-exactness.
          The specific quirk is MWCC's, and needs confirming for GT4's compiler.
CONFIDENCE: high for the quirk; it is quoted from the project's own comment
```

## 4.7 The project-level shape of the unsolved work

```
FINDING: DCDecomp's unsolved work is tracked as a *region* problem plus a *boundary* problem, not a
          backlog of broken functions: NTSC is fully decompiled (0 INCLUDE_ASM), PAL has 2 parked
          markers, the main ELF's symbol/string tables are out of scope by decision, and CI simply
          skips the progress job when the private binary repository is unavailable rather than
          failing.
SCOPE:   TOOLCHAIN
EVIDENCE: `grep -rn INCLUDE_ASM src/` → 2 hits, both `#if defined(PAL)`;
          README.md's matching claim and the "not a current priority" sentence;
          .github/workflows/progress.yml — "Skip without access to the private repository" and
          "the job reports that it was skipped rather than failing"
MEANS FOR US: The pattern is right: know what is *parked* and why, know what is *out of scope* by
          decision, and make the parts you cannot check report "skipped" rather than "failed" —
          because a gate that cries wolf when it cannot see is worse than one that admits it is
          blind. Our own gates should have an explicit "could not verify" state, distinct from both
          pass and fail. **We do not currently have one, and should.**
CONFIDENCE: high
```

## 4.8 What this whole document did NOT establish — the honest list

Recorded so a future session does not mistake silence for a negative result.

| Question | Status | What would settle it |
|---|---|---|
| Is DCDecomp's compiler really 2.3.3? | **high confidence yes** (6 sites agree) | run the bundled exe under wibo and read its banner |
| Does ps2xRecomp's toml11 flaw apply here? | **settled: NO** (§1.4) | nothing further; DCDecomp has no CMake dependency fetching |
| Does GT4 share MWCC's whole-program name counter? | **OPEN** | grep GT4's symbol table for `@<n>` float-literal names |
| Which MWCC built GT4? | **OPEN** | GT4's own build info / our own toolchain record |
| Was GT4 optimised higher than Dark Cloud's `-O2`? | **OPEN** | GT4's own build info; affects whether §4.3 is even reachable |
| Is `sceGifTag` in `include/sce/libgraph.h` a faithful GS GIF tag layout? | **OPEN, see below** | compile a probe setting `EOP/NREG/REGS0` and read the emitted bits against the R5900/GS spec |
| What are GT4's boot IRX modules? | **OPEN** — Dark Cloud's nine are Dark Cloud's | GT4's own `init_all`-equivalent |
| What is GT4's `sceGsSetDBuff` default draw environment? | **OPEN** | GT4's own image |
| Does GT4's own code use the `i = -i; i = -i;` delay idiom? | **OPEN** | grep GT4's disassembly — matters for our recompiler's folding rules |

### The one open item I want to flag hardest

`include/sce/libgraph.h:54-78` declares `sceGifTag` as
`NLOOP:15; EOP:1; pad16:30; PRE:1; PRIM:11; FLG:2; NREG:4; REGS0..REGS15:4`. Read least-significant-bit
first, that is the **VIF1 DMA *packet* tag** layout (the field widths and the `pad16` name line up
exactly with the standard `VIFdmaPacket` declaration, whose `CE` and `SP` fields appear to have been
renamed to `EOP` and `FLG`) — **not** the GS GIF tag, whose `EOP` is bit 31, `FRAME` is bits 24-28,
`NREG` is word1 bits 28-31 and `REGS0` is bits 0-4.

I could not resolve this without running the compiler, and I am not going to assert it. What is
worth recording is the *reason it is worth checking*, which generalises beyond this project:

```
FINDING: In a byte-exact decompilation, **byte-matching validates only what was executed.** A
         reconstructed hardware header must therefore be right in exactly the fields the game
         touches; fields it never writes are unconstrained by the match and can be wrong.
SCOPE:   TOOLCHAIN (the epistemics) with HARDWARE-UNIVERSAL stakes
EVIDENCE: the declarations at include/sce/libgraph.h:54-78 are a reconstruction (the file is 492
          lines of reconstructed SDK, and the author's own comment at :446-447 admits only the
          *extent* of `sceGsStoreImage` is stated by the image); scripts/build/verify.py compares
          bytes only, with nothing checking header semantics.
MEANS FOR US: **This is a live risk for our G2 GS lane and it is a good example of the failure mode
          the captain already fears.** A GS register struct can be shaped plausibly enough to
          produce correct-looking frames while being wrong in fields no test has touched. The
          discipline that follows is the one DCDecomp itself uses everywhere else: mark what is
          unverified as unverified (`VU1_PROGRAM_UNKNOWN6` is the model), and never let a
          reconstructed layout become load-bearing without a test that would fail if it were wrong.
          Concretely for us: **every GS register field in our layer needs a test derived from GS
          documentation, not from a struct that happens to work.**
CONFIDENCE: medium on the specific bitfield question (needs a compiler probe to settle);
          high on the general epistemics, which follow from what verify.py does and does not check.
```

---

# 5. GAME-SPECIFIC REGISTER — the do-not-carry list

*The captain, 2026-09-30: "im scared gt4 works differently tho and we are learning bad practice."*
This section is the direct answer. Everything listed here is real and useful, and **none of it is a
rule for GT4.** If you find yourself about to type one of these into VULCAN 4, stop and go read the
MEANS FOR US line on the finding it came from — which will say something different.

| Thing | Where | Why it must NOT be copied to GT4 |
|---|---|---|
| The **nine IRX modules** (SIO2MAN, PADMAN, MCMAN, MCSERV, LIBSD, SDRDRV, MODMIDI, MODHSYN, EZMIDI) | §3.1 | These are *Dark Cloud's* dependencies. GT4's own `init_all` names its own set. Copy the **sequence shape**, not the list. |
| The boot **order** as written (CD init → IOP reboot → re-init) | §3.1 | That order is HARDWARE-UNIVERSAL and very likely transfers. But it must be re-derived from GT4's own boot, because GT4 may reboot the IOP differently or not at all. |
| `My_dma_start0` (20 words) and `My_DrawEnv` (44 words) | §3.3 | Dark Cloud's GS startup packet, byte for byte. The *mechanism* (packet in data, DMA'd, flush + sync between sends) transfers; the contents are meaningless to GT4. |
| `Vu_prog0[896]` and every other microprogram in `src/vutext.cpp` | §3.3, §3.4 | Dark Cloud's VU1 microcode. **Never** port these. §3.3's transferable half is that retail's linker *kept* the internal `//_$J1` jump-target labels — which is a question to ask about GT4's linker, not an answer. |
| The `VU1_PROGRAM_*` enum and which programs exist | §3.4 | Dark Cloud's renderer. The *call contract* (write into the packet, advance the pointer) is universal; the program set is not. |
| `pal_at1188__2`, `pal_at2857`, `pal_at2690`, and the `#pragma name_counter` values | §2.4 | Dark Cloud's literal names. The *practice* (transcribe retail's `@<n>` names, restore the counter to its whole-program value) transfers; these numbers do not. |
| The `expr`-node override JSON (1.3 MB) and the literal-pool table in `region.py` (`0x2A17B8`/`0x2A1868`) | §2.8, §1.7 | Dark Cloud's addresses. The *layout* of such a table transfers; the contents are Dark Cloud's. |
| `config/ntsc/main.yaml`'s 130 TU offsets and every `vram:` in it | §1.7 | Dark Cloud's link map. The *form* (per-TU file offset + type + VRAM) is what GT4 needs, and it must be built from GT4's own ELF. |
| `SCREEN_HALF_HEIGHT`, `GS_Y_OFFSET`, `640`, `SCREEN_*` | §1.7, §3.2 | Dark Cloud's screen geometry. PAL's PAL branch shifts `DY` by 88 — that is a Dark Cloud PAL fact. |
| `262.0f` as the field-line divisor | §3.5 | 262 is NTSC and correct **as a hardware fact**; but GT4 may have its own divisor, and PAL is 313. Use GT4's. |
| `chararead.cpp`, `dispctrl.cpp`, `scriptinterpreter.cpp`, `runscript_opcodes.cpp` and the rest of `src/` | §4 target list | Dark Cloud's game logic. Structurally interesting, semantically useless to us. |
| `@unknownret` on 310 functions | §4.5 | Not a fact about Dark Cloud's semantics to copy — but the *count* is the honest expectation to hold for GT4 too. |

**What IS safe to carry from every row above:** the *shape*, the *sequence*, the *discipline*, and
the *failure modes*. Shapes and disciplines are what transfer. Values do not.

---

# 6. HOW THIS DOCUMENT SHOULD BE USED

1. **§EXECUTIVE SUMMARY** is the brief for the captain. Ten items, each with the section to read.
2. **§5 GAME-SPECIFIC REGISTER** is the guard rail. Check it before acting on anything from §3.
3. **The FINDING blocks** are the substance. Each has SCOPE, EVIDENCE (file:line in
   `/mnt/ssd/dcdecomp`), MEANS FOR US, and CONFIDENCE with what would raise it.
4. **§4.8** is the honesty table — nine things this document did *not* establish, so nobody mistakes
   silence for a negative result.
5. **GT4's own evidence always wins.** If any borrowed lesson contradicts `docs/` in our repo or the
   boot logs in `/mnt/ssd/vulcan4-build/run/`, the lesson is wrong for us and the contradiction gets
   written down here.
