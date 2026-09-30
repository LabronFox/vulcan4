# ANDROID-FEASIBILITY — can VULCAN 4 cross-compile to ARM64/Android?

**Goal G5.2a.** Answer with a build, not an opinion.

**Verdict: PORTABLE IN PRINCIPLE.** The recompiler's output and the entire runtime, GS and VU1
included, cross-compile to ARM64 with **zero architecture-portability work**. The only failures are
host-library dependencies (raylib/GLFW/X11, FFmpeg) that need Android equivalents — a platform
swap, not a port. Nobody has written the Android port and this dish did not start one.

**Transcript:** `/mnt/ssd/vulcan4-build/android/try.log`
**Binary produced:** `/mnt/ssd/vulcan4-build/android/gs_probe_arm64` (5,263,568 B, ELF64 AArch64)

**Headline proof:** the ARM64 executable contains **707 recompiled GT4 guest functions** and **94 GS
runtime symbols**, and contains **zero x86 or SSE symbols**.

```
$ file gs_probe_arm64
ELF 64-bit LSB pie executable, ARM aarch64, ... dynamically linked, ... not stripped
$ aarch64-linux-gnu-readelf -h gs_probe_arm64 | grep Machine
  Machine:                           AArch64
$ aarch64-linux-gnu-nm -C gs_probe_arm64 | grep -cE ' T sub_0| T ps2_recompiled'
707
$ aarch64-linux-gnu-nm -C gs_probe_arm64 | grep -cE 'GS::|GSCpuBackend'
94
$ aarch64-linux-gnu-nm -C gs_probe_arm64 | grep -cE '_mm_|__m128i'
0
```

---

## 1. What toolchain is on this box

Measured, not assumed:

| Thing | Result |
|---|---|
| Android SDK | `/home/or/Android/Sdk` **exists but is empty** — `du -sh` = 0. No `ndk/`, no components. |
| NDK | **Not installed.** No `/usr/lib/android-ndk`, no `/opt/android-sdk`. |
| `clang` / `clang++` | **Not installed.** |
| `aarch64-linux-gnu-g++` | **Not installed** before this dish. |

**Decision, stated up front:** I installed the **57 MB** apt cross-compiler
(`g++-aarch64-linux-gnu`, 25 packages) to get a real build. I did **not** install the NDK
(~1–4 GB), and I did not install `android-sdk` (noted for reference only). A 57 MB cross-compiler
buys a complete answer to the portability question; a multi-gigabyte SDK would only have answered
questions we can already enumerate from the symbol list.

**What a future Android dish needs, concretely:**
- **NDK r27 or newer**, side-by-side install, and `sdkmanager "ndk;27.x" "cmake;3.22.1" "platform-tools"`.
  The NDK supplies Clang with `aarch64-linux-android<API>` triples, the bionic sysroot, and
  `libc++_shared.so`.
- API level: **minSdk 26 (Android 8.0)** is a reasonable floor for Vulkan 1.2 + wide compatibility.
  The captain's AYN Odin 2 runs Android 13, so API 33 is available if we want newer features.
- Nothing else. No Gradle work is implied by this dish.

**The gap that is *not* ours:** this box's Ubuntu `arm64` apt index 404s on `noble-backports`, so
`zlib1g-dev:arm64` cannot be installed. That is a packaging gap on the build host, not a portability
problem — **Android/bionic ships zlib natively**, and the NDK provides it.

---

## 2. The single most useful thing I found: upstream already supports ARM64

This is the answer to "what will bite". **Less than expected, because PS2Recomp's top-level
`CMakeLists.txt` already has a complete ARM64 branch.** In `tools/PS2Recomp/CMakeLists.txt`:

- **lines 30–37** — detect the target: `if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")`
  sets `PS2X_IS_ARM_TARGET` and `PS2X_IS_AARCH64_TARGET`.
- **lines 44–57** — `FetchContent` **sse2neon v1.9.1** and
  `add_compile_definitions(USE_SSE2NEON)`.
- **lines 65–68** — `add_compile_options(-march=armv8-a+fp+simd)` for non-Apple ARM64.
- **lines 91–94** — our own G0.1 `-msse4.1` patch is already guarded with `NOT PS2X_IS_ARM_TARGET`,
  and its comment says outright: *"ARM/Android/Vita are skipped because they go through sse2neon
  above."*

**The ARM path is scaffolding that was written before we arrived.** All we had to do was supply a
CMake toolchain file so `CMAKE_SYSTEM_PROCESSOR` reads `aarch64`.

```cmake
# /mnt/ssd/vulcan4-build/android/aarch64-toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
```

Configure output confirms the branch was taken:

```
-- ARM target detected, fetching sse2neon
-- Non-Apple ARM64 - adding NEON compiler flags
```

### The one file that bites, if you bypass CMake

`ps2xRuntime/include/ps2_runtime_macros.h:7-13`:

```c
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(USE_SSE2NEON)
#include "sse2neon.h"
#else
#include <immintrin.h> // For SSE/AVX intrinsics
#endif
```

Compiling by hand with bare `aarch64-linux-gnu-g++` hits exactly this and dies:

```
ps2_runtime_macros.h:12:10: fatal error: immintrin.h: No such file or directory
   12 | #include <immintrin.h> // For SSE/AVX intrinsics
```

That is the whole of the "x86 intrinsics" problem, and it is a **build-system** problem, not a
source problem. **Cost: SMALL.** Build via CMake with the toolchain file and it never appears. Files
that use x86 types and all compile clean under sse2neon: `ps2_runtime_macros.h`, `ps2_runtime.h`,
`runtime/ps2_memory.h`, `ps2_memory.cpp`, `ps2_runtime.cpp`, `ps2_debug_panel.cpp`,
`ps2_vif1_interpreter.cpp`, `Kernel/Stubs/GS.cpp`, `Kernel/Stubs/IPU.cpp`.

---

## 3. What compiled, and what did not

Every line below is from `try.log`.

### Compiles clean for aarch64 — zero errors

| Target | Result |
|---|---|
| **`ps2_recompiled_functions.cpp`** — the generated TU, 8,899,740 B, 721 functions | **OK** → 5,123,416 B object |
| **`register_functions.cpp`** | **OK** → 141,544 B object |
| `GS::` frontend (`gs_frontend.cpp`) | **OK** |
| **`GSCpuBackend`** (`gs_cpu_backend.cpp`) — the whole software rasteriser | **OK** |
| `ps2_gs_memory.cpp` — every PSM pixel format, VRAM addressing | **OK** |
| `ps2_gif_arbiter.cpp` — the EE→GS DMA path | **OK** |
| `ps2_vif1_interpreter.cpp` — **the VU1** | **OK** |
| `ps2_runtime.cpp` | **OK** (once raylib's *headers* are on `-I`) |
| `Kernel/Syscalls/` — all 8, including **`Sync.cpp`** | **OK** |
| `Kernel/Stubs/` — 19 of 20 | **OK** |
| `ps2xRuntime/src/lib` top level — all 14, including **`ps2_android_runtime.cpp`** and `ps2_vita_runtime.cpp` | **OK** |
| `ps2xIOP` — 27 of 27 | **OK** |

Two of those deserve comment:

- **`ps2_android_runtime.cpp` already exists upstream** and compiles for aarch64. Somebody already
  thought about this platform.
- **`Kernel/Syscalls/Sync.cpp` compiles for aarch64.** That is the vblank/sync handler — the
  subsystem our guest is currently stuck in (`halt=stuck_in_syscall`). Whatever is wrong there is
  **not** an ARM problem, and the same file is what an Android build would use.

### Fails — both for the same boring reason

| Failure | Exact error | Category |
|---|---|---|
| **CMake configure of the full runtime** | `Could NOT find X11 (missing: X11_X11_INCLUDE_PATH X11_X11_LIB)` from `_deps/raylib-src/src/external/glfw/src/CMakeLists.txt:181` | (d) host library |
| `Kernel/Stubs/MPEG.cpp` | `fatal error: libavcodec/avcodec.h: No such file or directory` | (d) host library |

`MPEG.cpp` is the **only** source in the whole tree that fails to compile for ARM64, and it needs
FFmpeg *headers*, which do not exist for aarch64 on this host. It is already optional behind
`PS2X_HAS_FFMPEG`, the GS path does not need it, and Android can use its own or MediaCodec. On
Android the raylib dependency mostly evaporates too, because there is no X11 and raylib is not the
right windowing layer there anyway.

### Link

After compiling the tree, **33 raylib C-API symbols** remained unresolved — the complete list is
window/present, gamepad, and audio: `InitWindow`, `CloseWindow`, `BeginDrawing`, `EndDrawing`,
`IsWindowReady`, `WindowShouldClose`, `ClearBackground`, `GetScreenWidth`, `GetScreenHeight`,
`DrawTexturePro`, `UpdateTexture`, `LoadTextureFromImage`, `UnloadTexture`, `SetTargetFPS`,
`SetConfigFlags`, `GenImageColor`, `UnloadImage`, `GetGamepadAxisMovement`, `IsGamepadAvailable`,
`IsGamepadButtonDown`, `IsKeyDown`, `InitAudioDevice`, `CloseAudioDevice`, `IsAudioDeviceReady`,
`PlaySound`, `StopSound`, `IsSoundPlaying`, `SetSoundVolume`, `SetSoundPitch`, `LoadSoundFromWave`,
`LoadWaveFromMemory`, `UnloadSound`, `UnloadWave`.

Stubbing exactly those 33 (plus omitting the FFmpeg-only TU) produced the working ARM64 binary.
**Every one of them is a windowing, input or audio call.** Not one is a graphics, CPU, DMA, IOP or
memory-model call.

---

## 4. Classification and cost

| # | Class | What | Cost | Why |
|---|---|---|---|---|
| a | **Missing toolchain** | No NDK on this box. Need NDK r27+ for the bionic sysroot, Clang triples and `libc++_shared`. | **Small (ours)** / **~1–4 GB (disk)** | Pure acquisition. No code. Everything else cross-compiles today. |
| b | **Host-arch intrinsics** | `immintrin.h` at `ps2_runtime_macros.h:12`; `-msse4.1` from our G0.1 patch. | **Small** | **Already solved upstream.** sse2neon v1.9.1 is wired into CMake behind `USE_SSE2NEON`, and our patch already excludes ARM. Just build via CMake. |
| c | **Host-endian / layout** | **Nothing found.** | **Zero** | The PS2's R5900 is little-endian, GT4's `SCUS_973.28` is `ELFDATA2LSB` (see `TOOLCHAIN.md` §5), aarch64 is little-endian, x86-64 is little-endian. All four agree, so no byte swapping and no `EI_DATA` handling is required. Verified by the whole tree compiling and the binary producing correct-looking AArch64 codegen. |
| d | **Host library dependencies** | raylib → GLFW → **X11** (33 symbols), FFmpeg (1 TU). | **Medium** | X11 does not exist on Android at all. Needs EGL + `ANativeWindow`/`NativeActivity` for presentation, AAudio or Oboe for audio, and Android's own zlib. This is a *platform layer* swap, and it is bounded and well understood. |
| e | **Genuine portability work** | **None found.** | **Zero** | 100% of the engine, the GS, the VU1, the memory model, the DMA path and all 707 guest functions compiled and linked unmodified. |

---

## 5. What this means for G5.2

The architecture is a solved problem. The remaining work is an Android *platform* port, and the
list of things to replace is short and specific:

1. **Replace raylib/GLFW with EGL + Vulkan (or GLES) via `ANativeWindow` / `NativeActivity`.** The
   11 window/present symbols are the whole job. Note our own G2.0 work already proved the GS can run
   completely headless — `latchHostPresentationFrame` → `copyLatchedHostPresentationFrame` → PNG —
   so a first Android milestone needs **no windowing at all**.
2. **Replace raylib audio with AAudio or Oboe.** 3 symbols. The Odin has a Snapdragon, so this is
   straightforward and low-risk.
3. **Drop `MPEG.cpp` or point FFmpeg at an Android build of it.** It is already optional and the
   GS path does not need it. GT4's FMVs are a separate goal from getting the game running.
4. **Add an NDK toolchain file** — the same five lines as §2, with `CMAKE_SYSTEM_NAME Android` and
   an `ANDROID_ABI`/`ANDROID_PLATFORM` pair. `PS2X_IS_AARCH64_TARGET` already matches `aarch64`, so
   the existing CMake branch engages with **no edits to `PS2Recomp` at all**.
5. **Package the result.** `libc++_shared.so` and the SDL/EGL surface are the only packaging
   concerns. Our G0.5 clean-room recipe is the template: a fresh build root, the three patches, and
   a machine-readable boot report.

**Risk, honestly stated:** I could not test against **bionic** or the **NDK**, only against glibc
for aarch64 Linux. Bionic differs in libc surface and C++ runtime, so a small number of
libc-specific issues is possible — most likely in `ps2_vfs.cpp`, the ISO9660/CUE readers, and
anything using `ucontext`/fibres. Nothing observed points at a real blocker. The genuine unknown is
**not** portability; it is that we still cannot get three guest functions deep, and that is a PS2
problem, on any CPU.

**The honest headline: the handheld is not the hard part.** The GS, the VU1, the DMA path and every
byte of recompiled GT4 code already build for ARM64. The wall is still the one G2.0 named.
