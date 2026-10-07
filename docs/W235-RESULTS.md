# W235 — distinct engine symbols landed; dispatch/compile blocked by box memory

## 1. DISTINCT SYMBOLS — LANDED (tool change, OFF by default)

`ps2xRecomp/src/lib/function_table_emitter.cpp`: reads `PS2RECOMP_TABLE_SYMBOL` (default
`g_ps2RecompiledFunctionTable`, so the loader is byte-for-byte unchanged) and emits that name for the
`…Base/…End/…SlotCount` globals and the table array. Patch:
`tools/patches/ps2recomp-linux-w235-distinct-symbols.patch` (50 lines).

Regenerated the engine output with `PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable`:
```
extern const uint32_t g_ps2EngineFunctionTableBase = 0x100008u;
extern const uint32_t g_ps2EngineFunctionTableEnd = 0x616ec8u;
extern const uint32_t g_ps2EngineFunctionTableSlotCount = 1334192u;
PS2Runtime::RecompiledFunction g_ps2EngineFunctionTable[1334192u] = {};
```
The engine's generated function names were already distinct (`sub_00100008_0x100008` vs the loader's
`sub_01000008_0x1000008` — different addresses), so only the table globals collided. `ps2_recompiled_functions.cpp`
(105,635,875 bytes) is unchanged byte-count — the big TU need not be regenerated.

## 2. SECOND DISPATCH — NOT LANDED

The harness resolves every guest entry itself: `slot = (ctx.pc - g_ps2RecompiledFunctionTableBase) >> 2;
g_ps2RecompiledFunctionTable[slot]` — hardcoded to the loader base `0x01000008`. Entering the engine at
`0x00100008` needs (a) the driver to select the engine table `[0x00100000,0x00617A14)` after the ExecPS2
relaunch, and (b) verification that an engine→loader jump (and the runtime's inline `dispatchGuestBranch`
call path) still resolves. Not implemented this dish; the loud `VULCAN 4 LIMITATION` stays and
`halt=execps2_unmapped_entry` is unchanged.

## 3. COMPILE — capped by the box, not finished

Memory discipline was followed: `free -g` showed 31 total / 28 used / 0 free / 3 available with **swap
100% full** before anything was started, so NO second compiler was launched. The earlier detached engine
TU compile (`v4-cc`, `g++ -O1`, unit `MemoryHigh=10G MemoryMax=14G`) has been running >4 min at
**~10.8 GB RSS and ~30 % CPU** (thrashing against the full swap) and has not produced `engine_tu.o`.
Per the rules this is reported, not forced: **no build time available**, and the engine's
`register_functions.cpp` (11 MB) has not been compiled either. The live Minecraft server was never
touched.

## 4. STATE

- Step 1 (distinct symbols): done, committed `8cdc8d2`.
- Step 3 (compile): blocked by memory, reported.
- Step 2 (second dispatch) + step 4 (boot/capture): not reached; screenshot unchanged (last:
  `/mnt/ssd/vulcan4-build/run/w231b-capture.png`, disclaimer).
- Suite green; loader's 723 functions untouched; no generated output hand-edited.

## 5. NEXT (bounded, memory-gated)

Wait for `free -g` available ≥ 6 GB (or stop `v4-cc` and retry the engine TU alone at `-O1` under the
same 14 GB cap), then: compile the engine TU + its `register_functions.cpp`, link beside the loader,
and make the driver switch to `g_ps2EngineFunctionTable` for `[0x00100000,0x00617A14)` on the ExecPS2
relaunch — verifying both directions of engine↔loader dispatch.
