# W239 — TU split + engine compile (dispatch/boot not reached)

## 1. Bounded TU splitting — LANDED

`ps2xRecomp/src/lib/ps2_recompiler.cpp` (`tools/patches/ps2recomp-linux-w239-tu-split.patch`): the
single-worker output path now writes deterministic part files
`ps2_recompiled_functions_NN.cpp` of at most `PS2RECOMP_TU_BYTES` (default 8 MB), each with the include
preamble; the aggregate registration TU (`register_functions.cpp`) is unchanged. OFF-by-default size only
affects this stage; the loader (single TU) is unaffected.

Emitted engine (400-function frontier, distinct symbols):

| part | bytes |
|---|---:|
| `ps2_recompiled_functions_00.cpp` | 8,376,576 |
| `ps2_recompiled_functions_01.cpp` | 8,088,419 |
| `ps2_recompiled_functions_02.cpp` | 8,280,413 |
| `ps2_recompiled_functions_03.cpp` | 8,375,508 |
| `ps2_recompiled_functions_04.cpp` | 7,229,829 |
| `register_functions.cpp` | 577,278 |

## 2. Compile — LANDED, fits the box

Sequential, one TU at a time, in a capped user unit (`v4-engcc2`, `MemoryHigh=10G MemoryMax=14G`,
Nice=10, IOSchedulingClass=idle, CPUQuota=300%), `-O0` (correctness first), `free -g` = 12 GB before
starting:

| TU | wall | peak RSS |
|---|---:|---:|
| part 00 | ~19 s | 1.61 GB |
| part 01 | 18.32 s | 1.59 GB |
| part 02 | 19.83 s | 1.63 GB |
| part 03 | 20.07 s | 1.65 GB |
| part 04 | 16.26 s | 1.44 GB |
| register | 1.19 s | 0.31 GB |

Total ≈ 96 s, max 1.65 GB — well inside the cap. **The engine now compiles here.** Objects:
`ps2_recompiled_functions_0{0..4}.o` (7.0–8.9 MB) + `register_functions.o`.

## 3. Second dispatch path + boot — NOT reached

The harness entry resolution is still hardcoded to the loader base `0x01000008`, the ExecPS2 relaunch
still raises `execps2_unmapped_entry`, and the engine objects are not yet linked into the harness. So
step 3 (engine/loader dual-table dispatch) and step 4 (boot + capture) were not done. Halt unchanged;
capture unchanged (`/mnt/ssd/vulcan4-build/run/w231b-capture.png`, disclaimer). Suite green; loader's
723 functions untouched; no hand-edited output; java/Minecraft never touched.

## 4. Next (wiring, now unblocked)

Link `ps2_recompiled_functions_0*.o` + the engine `register_functions.o` into the harness; make the
driver select `g_ps2EngineFunctionTable` (base `0x00100000`, entry `0x00100008`) after the ExecPS2
relaunch and keep the loader table before it; verify engine↔loader jumps in both tables; unresolved
paths stay LOUD `VULCAN 4 LIMITATION`.
