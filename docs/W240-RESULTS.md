# W240 — engine dispatch lands: ExecPS2 enters the recompiled engine

## What landed

1. **LINK** — `tools/harness/build_harness.sh` now links the engine image's objects
   (`recomp_engine_small/ps2_recompiled_functions_0{0..4}.o` + `register_functions.o`) beside the
   loader. No duplicate symbols (the engine table uses `g_ps2EngineFunctionTable*`), build exit 0, the
   loader's 723 functions untouched.

2. **SECOND DISPATCH PATH** (`tools/harness/vulcan4_harness.cpp`) —
   - the loader table vars are now mutable and the dispatch uses `activeTable`;
   - on the ExecPS2 relaunch the driver resolves the entry against BOTH tables and, when it is in the
     engine image, switches `activeTable` to `g_ps2EngineFunctionTable`;
   - the per-entry dispatch also falls back to the OTHER table (engine↔loader) before declaring a
     missing function.

## Measured (oracle ON)

```
[execps2] entry=0x100008 gp=0x0 argc=2 argv=0x3
VULCAN4 EXECPS2 -> engine table [0x00100008,0x00616d94) entry=0x00100008
VULCAN4 EXECPS2 relaunch#1 entry=0x00100008 gp=0x00000000 argc=2 argv=0x00000003
halt=pc_outside_generated_table   WILDPC dead=0x01028b30
```

So the ExecPS2 → engine dispatch **fired**: the driver switched to the engine table and entered the
recompiled engine at its real entry `0x00100008`. It then hit `pc=0x01028b30` — a LOADER address (the
return site of the ExecPS2 call) that the loader table has no entry for, which is the **engine → loader
return direction** the task asked to verify: it does NOT resolve (0x01028b30 is not a generated entry),
so it stays a LOUD halt. New screen: NO — capture is still the disclaimer (perceptual diff 1.13 vs the
W231b disclaimer capture).

## State

- Engine dispatch: LANDED and firing.
- Halt: `pc_outside_generated_table` at 0x01028b30 (engine→loader return unresolved).
- Capture: `/mnt/ssd/vulcan4-build/run/w240b-capture.png` (640x448, 16 colours, disclaimer).
- Suite green; no hand-edited output; java/Minecraft never touched.

## Next

The engine's crt0 ran; before going further the 80 entry-slice limitations plus this return path need
boundaries. The engine→loader return (0x01028b30) is the immediate wall: either the loader function
containing it must be emitted/covered, or the engine stub must not return into the loader.
