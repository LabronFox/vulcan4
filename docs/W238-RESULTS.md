# W238 — entry-slice over-run fixed (LOUD limitation); giants gone

## The fix (tool change, `tools/patches/ps2recomp-linux-w238-slice-cap.patch`)

`ps2xRecomp/src/lib/ps2_recompiler.cpp`: every entry/resume slice is now capped — if a slice would
span more than `PS2RECOMP_MAX_SLICE_INSNS` instructions (default 1024, `w238MaxSliceBytes()`), the
analyzer emits a **LOUD** `VULCAN 4 LIMITATION` naming the address, the span and the cap, and clamps
the slice. Applied in both slicing branches of `discoverAdditionalEntryPointsImpl` (containing-function
and no-containing/`sectionEnd`). This is a guard, not a truncation "fix": the address is reported so the
missing boundary can be found. Never hand-edited output; OFF-by-default behaviour unchanged.

## Result (engine, `PS2RECOMP_REACHABLE_ONLY=1 MAX_FUNCTIONS=400`, distinct symbols)

| | W236/W237 | W238 |
|---|---:|---:|
| TU bytes | 217,522,773 | **40,349,773** |
| largest emitted function | 1,349,426 lines (`entry_48efb0`) | **8,687 lines** (`entry_2fc870`) |
| LOUD limitations | 0 | **80** |

## Spot-check vs ground truth — 0x5A3140 (the crt0's `j` target)

```
// Function: sub_005A3140
// Address: 0x5a3140 - 0x5a31f0
```
Ground truth (image bytes): prologues at `0x5a3140, 0x5a31f0, 0x5a32f0`, first `jr $ra` at `0x5a32e8`.
Our emitted end `0x5a31f0` is exactly the **next real prologue** — the boundary is correct now. (Before
the fix this region was swallowed into a multi-MB slice.)

## Not reached (steps 4–5)

The TU is still 40.3 MB (> the ≤8 MB split target) so the **TU split**, the bounded-frontier **compile**,
the **second dispatch path** and the **boot** were not done this dish. Halt unchanged
`execps2_unmapped_entry`; capture unchanged (`/mnt/ssd/vulcan4-build/run/w231b-capture.png`, disclaimer);
suite green; loader's 723 functions untouched; java/Minecraft never touched; `free -g` checked (12 GB
available) before the emit.

## Next (mechanical now)

`PS2RECOMP_MAX_SLICE_INSNS` can be lowered to shrink the tail, then split `ps2_recompiled_functions.cpp`
into ≤8 MB TUs + an aggregate registration TU, compile the ~400-function frontier under a capped
`systemd-run` unit, and land the engine dispatch (harness entry resolution for base `0x00100000`/entry
`0x00100008`; ExecPS2 enters `g_ps2EngineFunctionTable`; verify engine↔loader jumps in both tables).
The 80 limitations name the still-missing boundaries to add.
