# W250 — the silent-garbage path is gone; override handlers are resolved across BOTH images

## Root cause found (the override invocation plumbing)

`Kernel/Syscalls/System.cpp::dispatchSyscallOverride` gates the deferred invocation on
`runtime->hasFunction(handler)`. The RUNTIME's `hasFunction` only knew the LOADER table
(`g_ps2RecompiledFunctionTable`, base `0x01000008`) — so an ENGINE handler (`0x005B73C8`, range
`0x00100000+`) was "no function" → it took the silent path `setReturnS32(ctx, KE_ERROR)` =
**`v0 = -1 = 0xFFFFFFFF`**, exactly the signature W247 saw (`0` → `0xFFFFFFFF` → `a0=3`). This was the
"silent garbage" that hid the wall for several dishes.

## Fix (runtime, `tools/patches/`)

`ps2_runtime.cpp` + `System.cpp`:
1. **Unified lookup** — `PS2Runtime::hasFunction` now consults BOTH tables (loader + engine); the engine
   table is referenced via WEAK externs so the runtime still links without an engine.
2. **KSEG normalisation** — the override table stores the guest's mirror-form handler (e.g.
   `0x80075000` for `0x00075000`), so the lookup masks `& 0x1FFFFFFF` before indexing the tables.
3. **LOUD, never silent** — `dispatchSyscallOverride` now prints
   `VULCAN 4 LIMITATION: syscall 0x.. override handler 0x.. has no generated function in either image`
   instead of a silent `KE_ERROR`.

## Measured (oracle ON)

```
VULCAN 4 LIMITATION: syscall 0x5b override handler 0x80075000 has no generated function in either image
VULCAN 4 LIMITATION: syscall 0x56 override handler 0xffffffff  has no generated function in either image
VULCAN4 BOOT REPORT functions_entered=3377 ... halt=guest_blocked
```

The named gaps are real: `0x80075000`→`0x00075000` is BELOW the engine image (`0x00100000`), i.e. low
RDRAM the engine populates at runtime (not statically emitted), and `0xffffffff` is a bogus
registration. Halt moved from `guest_cycle_no_progress` to `guest_blocked`. New screen: NO
(`/mnt/ssd/vulcan4-build/run/w250b-capture.png`, diff 1.13 vs the W231b disclaimer).

## State / next

The silent-garbage path is closed and the missing handlers are now named addresses. Whether `0x5B73C8`
itself executes was not directly logged this dish (the 0x83 override no longer hits the missing gate),
but the named limitations (`0x80075000`) are the next roots to emit (they are runtime-low-RAM handlers)
or to remodel. Suite green; probe OFF by default; nothing hand-edited; java/Minecraft untouched.

---

# STOP STATE — captain's halt, 2026-10-07 (resume-from-here)

## What changed this session (the engine-image arc, W231-W250)

- **W231-W235**: the SCUS ELF is only a LOADER; GT4's real engine is a runtime-loaded image inside
  CORE.GT4. Container mapped (3 members; engine = `blob[0x30 : 0x30+0x517A14]` -> `0x00100000`,
  entry `0x00100008`) and **verified byte-for-byte 100.0000% against hardware RDRAM**. Recompiler
  taught to accept it (synthesized ELF `w231-engine.elf`) and given distinct table symbols
  (`PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable`). Tool patches in `tools/patches/ps2recomp-linux-w23*.patch`.
- **W233-W239**: reachability scoping (`PS2RECOMP_REACHABLE_ONLY`, `PS2RECOMP_MAX_FUNCTIONS`,
  `PS2RECOMP_NO_FALLBACKS`), entry-slice cap (`PS2RECOMP_MAX_SLICE_INSNS`, LOUD limitation, engine TU
  217 MB -> 40 MB, largest fn 1.35 M -> 8.7 k lines), and bounded TU splitting
  (`PS2RECOMP_TU_BYTES`, 5 parts <= 8 MB + aggregate `register_functions.cpp`). Engine compiles `-O0`
  in ~96 s, peak RSS <= 1.65 GB/TU.
- **W240-W241**: engine objects linked beside the loader; unified function resolution across BOTH
  images (one address space); `MISSING-BOUNDARIES n=0`; `functions_entered` 3371 -> 7490.
- **W247-W250**: the engine spin is a `sce_FindAddress` (0x83) retry loop; the 0x83 override dispatches
  to the ENGINE handler `0x005B73C8`; the runtime's `hasFunction` was LOADER-ONLY, so an engine handler
  was "no function" -> **silent `KE_ERROR` (-1)** (`v0=0xFFFFFFFF`, the `0`->`0xFFFFFFFF`->`a0=3`
  signature). **Fixed**: `hasFunction` now resolves both images (weak engine externs) + KSEG-normalises
  (`& 0x1FFFFFFF`), and `dispatchSyscallOverride` raises a **LOUD** `VULCAN 4 LIMITATION` naming the
  handler instead of silent garbage (`tools/patches/ps2recomp-linux-w250-unified-handler-lookup.patch`).

## Current halt + what is known

- Halt: `guest_blocked` (engine TUs linked, oracle ON). Two LOUD limitations now name real gaps:
  `syscall 0x5b override handler 0x80075000 has no generated function` (physical `0x00075000`, BELOW the
  engine image -> low RDRAM the engine populates at runtime) and `syscall 0x56 ... handler 0xffffffff`
  (bogus registration).
- Screen: **still the disclaimer** (structural match) — menu NOT reached. Newest capture
  `/mnt/ssd/vulcan4-build/run/w250b-capture.png`.
- Oracle: PCSX2's DebugServer at `:21512` answers raw socket reads but the instance cycles at EE kernel
  idle `pc=0x81FC0` and will NOT reach `0x5B7408`; it is wedged (needs a clean stop + relaunch of
  `pcsx2-qt -debugger`, never touching java/Minecraft).

## Mechanical gate output (`.auto/verify-dish.sh`, 2026-10-07 13:50, exit 0)

```
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w250b-capture.png
      GATE FAIL: STRUCTURAL MATCH to the disclaimer (non-black 0.1173 vs ref 0.1150)
INFO  missing-function hits in that log: 0
=== RESULT: MECHANICAL CLAIMS HOLD ===
```

## Exact next step for a stranger

1. Emit the runtime-low-RAM override handlers the LOUD limitations now name: add `0x00075000` (and any
   further `MISSING-BOUNDARIES`/limitation addresses) to the engine TOML `entry_points` OR determine
   what populates `0x00075000` at runtime (it is below the engine image), re-emit
   (`PS2RECOMP_REACHABLE_ONLY=1 PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable`), recompile the parts
   (`-O0`, one TU at a time, capped unit), relink, boot, and repeat until no NEW limitation appears.
2. Repair the PCSX2 instance (ours, `/mnt/ssd/tools/pcsx2-src`, `:21512`) and break at `0x5B7408` to get
   hardware's `a0/a1/a2/v0` for the two `FindAddress` calls (the args are already correct in ours; the
   result is what differs).
3. Keep the loader's 723 functions and the `[0x01000008,0x0102DBEC)` behaviour byte-for-byte; every new
   probe stays OFF by default.
