# W234 — the emitter "scaling" blocker is a DEADLOCK, not a quadratic

Goal: make the engine emission finish, then split/compile/wire ExecPS2.

## 1. The blocker, measured — 11-worker DEADLOCK

The engine recompile was run **detached** (`systemd-run --user --unit=v4-emit`, MemoryMax 14G,
Nice=10, IOSchedulingClass=idle, CPUQuota=400%), out of the opencode-server 3 GB cgroup. It produced
~60 MB of `ps2_recompiled_functions.cpp` in the first ~1 s and then **stalled**. The process showed
`%CPU 1.3`, state `S`, and **all 12 threads in `futex_wait_queue`** — a classic deadlock in the
multi-worker emission (`generating function output with 11 worker(s)`), NOT a slow quadratic.

Fix applied (input change): `output_worker_threads = 1` in
`/mnt/ssd/gt4/work/w231-engine_recomp.toml`. With one worker the whole engine emission **completes in
~1.5 s**:

```
[w234] DISCOVERY      2ms  functions=18903
[w234] DECODE       543ms  functions=6484
[w234] RESUME-COLLECT 303ms
[w234] EMIT          662ms  functions=6486
Recompilation completed successfully   EXIT=0
```

Phase timings are instrumented behind `PS2RECOMP_PHASE_TIMING` (OFF by default; tool change in
`ps2xRecomp/src/lib/ps2_recompiler.cpp`, patch `tools/patches/ps2recomp-linux-w234-phase-timing.patch`).
They confirm **no quadratic**: discovery, decode, resume-collection and emission are all sub-second at
6,486 functions. The worker pool must be repaired (or kept single-threaded) — that is the real fix.

## 2. Engine output (complete)

```
Functions discovered: 18903   recompiled: 18903   Generated functions: 6486   decode failures: 0
Additional entrypoints: 129624 | Indirect fallback promotions: 1357 (219243 fallback entries)
```
`/mnt/ssd/vulcan4-build/recomp_engine/`:
- `ps2_recompiled_functions.cpp` — **105,635,875 bytes** (single TU; no split needed to *generate*)
- `register_functions.cpp` — 11,133,007 bytes; table base `0x100008`, end `0x616EC8`,
  `g_ps2RecompiledFunctionTable[1334192]`.

## 3. Compile (detached, measuring)

`g++ -O1 -msse4.1` of the 105 MB TU, detached (`v4-cc`, Nice=10, CPUQuota=200%). See
`/mnt/ssd/vulcan4-build/engine_cc.log`.

## 4. Wiring — the next bounded step (not done)

`recomp_engine/register_functions.cpp` defines the **same global symbols** as the loader's
(`g_ps2RecompiledFunctionTableBase/End/SlotCount` and `g_ps2RecompiledFunctionTable`), so the two
recompilations cannot be linked as-is. And the harness dispatch is hardcoded to the loader table
(`slot = (pc - base) >> 2`, base `0x01000008`; the engine base is `0x00100008`). Wiring ExecPS2 needs:
(a) a tool option to give the engine's table distinct symbols (never hand-edit generated output); and
(b) a second lookup path in the driver so, after the ExecPS2 relaunch, dispatch uses the engine table
for `[0x00100000, 0x00617A14)`. Until then the loud `VULCAN 4 LIMITATION` stays and
`halt=execps2_unmapped_entry` is unchanged. Suite green; loader's 723 functions untouched.

## 5. Recorded

The engine entry `0x00100008` is real MMI crt0 code (Caine confirmed `padduw rd=1,0,0`); container
mapping and 100% hardware verification are in `docs/W231-RESULTS.md`.
