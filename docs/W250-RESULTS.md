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
