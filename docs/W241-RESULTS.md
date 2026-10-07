# W241 — unified function resolution (both images = one space); the engine runs much further

## Change (harness)

`tools/harness/vulcan4_harness.cpp`: replaced the "switch the active table" design with ONE resolver,
`resolveFunction(pc)`, that consults BOTH images as a single address space — loader
`[0x01000008,0x0102DBEC)` and engine `[0x00100008,0x00616D94)` — plus `inAnyImage(pc)`. Entering the
engine no longer removes the loader's entries and vice versa; the ExecPS2 relaunch no longer mutates
the tables. Every unmapped address that lies INSIDE an emitted image is collected in
`missingBoundaryAddresses` and printed: `VULCAN4 MISSING-BOUNDARIES n=… : 0x…` (LOUD, addressed).

## Measured (oracle ON)

```
VULCAN4 EXECPS2 -> unified resolve entry=0x00100008 (engine 0x00100008,0x00616d94)
VULCAN4 MISSING-BOUNDARIES n=0 :
VULCAN4 BOOT REPORT functions_entered=7490 ... halt=guest_cycle_no_progress
```

- **Both directions resolve**: the previous halt at loader `0x01028b30` is gone — MISSING-BOUNDARIES
  is 0 and no WILDPC. The engine→loader return and the loader→engine ExecPS2 relaunch both resolve.
- `functions_entered` **3371 → 7490** (the engine's crt0 + its callees ran).
- New halt: `guest_cycle_no_progress` — a BEHAVIOURAL cycle (the engine spins reaching no new code or
  hardware), not a boundary bug.
- New screen: NO — capture is still the disclaimer (perceptual diff 1.13 vs the W231b disclaimer
  capture). Capture: `/mnt/ssd/vulcan4-build/run/w241-capture.png`.

## State / next

Unified lookup landed; missing-boundary list empty (0 addresses). The wall is now the engine's
`guest_cycle_no_progress` spin — the next dish locates that cycle (use the existing CYCLELOOP decode and
the `VULCAN4 CYCLE` output). Suite green; loader's 723 functions untouched; engine 5 TUs linked beside
it; nothing hand-edited; java/Minecraft never touched.
