# microVU ON LINUX — researched by hand, and the answer is better than expected (2026-10-01, Caine)

**Verdict: microVU is GPL-3.0-or-later and the source is already portable to GCC/Clang. PCSX2
itself builds on Linux, macOS and Windows with CI for all three.** The "MSVC x64 only" line in my
earlier brief came from Killzone's README describing *their* build, not a property of microVU.

I fetched the sources rather than reading a summary, so every claim below is from the code.

## What was fetched

`PCSX2/pcsx2` @ master, via authenticated `gh api`, no clone. 236 files, 3.1 MB, into
`/home/or/.microvu_src` (scratch, outside the project). Covered: `common/`, `3rdparty/include/`,
`pcsx2/x86/`. Fetch result: **184 fetched, 0 failed** (the remainder were already present from an
earlier pass).

The microVU core is all present and is **committed, not generated**:

```
pcsx2/x86/microVU.cpp           microVU_Analyze.inl   microVU_Compile.inl
pcsx2/x86/microVU.h             microVU_Execute.inl   microVU_Branch.inl
pcsx2/x86/microVU_IR.h          microVU_Macro.inl     microVU_Misc.inl
pcsx2/x86/microVU_Tables.inl    microVU_Clamp.inl     microVU_Upper.inl
                                microVU_Lower.inl     microVU_Alloc.inl
                                microVU_Flags.inl     microVU_Log.inl
```

**No codegen step.** There is no MSVC tool in the loop to produce them, which removes the single
biggest portability risk I expected to find.

## 1. Licence — the decisive question, answered from source

`pcsx2/x86/microVU.cpp` lines 1-2, verbatim:

```cpp
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
```

**GPL-3.0-or-later.** Linking it into VULCAN 4, which is already **GPL-3.0**, is compatible and
requires nothing new from us: we are already the same licence, we would simply have to carry the
attribution and the GPL notice.

**This does not change our distribution posture.** "Code only, assets from the user's own disc" is
unaffected — it is a statement about *shipping game data*, not about which licence our code carries.
Killzone ships GPL-3.0 for exactly this reason.

## 2. MSVC-only constructs — measured across the whole fetched tree

| construct | hits in tree | in microVU's path? |
|---|---|---|
| `__declspec` | 17 | **no** — all in `3rdparty/include/{pcap,xxhash}` and two macros in `common/Pcsx2Defs.h` |
| `_MSC_VER` | 57 | **no** — 0 hits in any `microVU*` file |
| `__forceinline` | 17 | **no** — defined in `Pcsx2Defs.h`, with a GCC branch |
| `#pragma intrinsic` | 0 | — |
| `__asm` / `_asm` | 1 each | **no** — the single real one is in `iR3000A.cpp:1275`, and it is **already `#ifdef`'d** with a GCC `__asm__` fallback beside it |
| `__readcr` / `__cpuid` / `__umul128` | 0 | — |

**microVU's own files contain zero MSVC-only constructs.** And `common/Pcsx2Defs.h` is written with
both sides of the fence — for example:

```cpp
#ifdef _MSC_VER
#define __forceinline __forceinline
#define __noinline   __declspec(noinline)
#define RESTRICT     __restrict
#else
#define __forceinline __attribute__((always_inline, unused))
#define __noinline   __attribute__((noinline))
#define RESTRICT     __restrict__
#endif
```

That file is a maintained MSVC/GCC compatibility shim, not an MSVC-only header. microVU does **not**
reference `iR3000A` at all, so the one real inline-asm site in the tree is outside our path.

## 3. SIMD requirements are SSE, not AVX2

`microVU_Clamp.inl` and `microVU_Upper.inl` reference **SSE4** constants (`sse4_minvals`,
`sse4_maxvals`, `sse4_compvals`) and there is a comment about a "non-sse4 version only" fallback in
`Clamp.inl:47`. There is **no `/arch:AVX2` requirement, no `__AVX2__` gate and no CPUID feature
probe** anywhere in the microVU sources. `/arch:AVX2` in Killzone's README is their build setting,
not a microVU requirement.

GCC equivalents, if wanted: `-msse4.1` / `-msse4.2`.

## 4. PCSX2 builds on Linux — with CI

The repository carries a **Linux build matrix workflow** and a `linux_build_flatpak.yml`, alongside
Windows and macOS workflows, and the README states plainly:

> PCSX2 supports Windows, Linux, and Mac platforms.

So this is not a fork-and-hope situation. The microVU sources sit inside a project that is built
and tested on Linux every day.

## The honest caveats

1. **I have not compiled it.** Everything above is read from source. The claim "microVU compiles on
   GCC/Clang" is *strongly supported* — portable constructs, no codegen, a maintained GCC branch in
   the one shared header, and a Linux CI in the parent project — but it is not *measured* by me.
   A compile is the next step, and it is cheap: the sources are already on disk at
   `/home/or/.microvu_src`.
2. **`microVU` is x86-64 only.** It emits x86 machine code, so it is irrelevant to the Odin 2 / ARM64
   target. The interpreter path in `ps2_vu1_core.cpp` stays needed regardless; microVU would be the
   x86 path only.
3. **The shim that made this messy for Killzone is theirs, not ours.** `ext/kzvu/kzvu_rename.h`
   force-includes a rename header over every PCSX2 global because they link the whole of PCSX2's
   EE/VU machinery. We would link a much smaller slice — microVU plus the VU register state — so
   that renaming work is likely ours to do, and it is the real integration cost.

## What this changes for VULCAN 4

**The VU1 decision is no longer blocked on a licence question or a Windows-only toolchain.** It is
now a scoped engineering question:

1. Compile microVU standalone on this box with GCC. (cheap, ours to do)
2. Give it `PS2Memory::m_vu1Code` / `m_vu1Data` — which **must be static arrays inside the executable
   image**, because microVU encodes VU addresses as 32-bit displacements from its own text pointer.
3. Invalidate on change via the existing `getVU1CodeGeneration()` counter in the MSCAL callback.
4. Differential-test against our existing interpreter, which already exists and is already wrong in
   known ways — that is the oracle, not a new one.

The captain's call is now much smaller: **GPL-3.0-or-later, which we already are.**
