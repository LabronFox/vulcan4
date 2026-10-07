# W230 — the recompiler gap at 0x101F040 (tool fix) and the wall after it

Goal: with the oracle injected (proving the loader hash compare is the blocker), fix the NEXT wall —
a recompiler gap — in the TOOL, then iterate wall by wall. The oracle and the 12 KB decoder fix are
NOT part of this dish (the decoder fix is the next dish); the shipped path stays our own decoder.

## 1. The wall

With `VULCAN4_W229_ORACLE` on, the boot advanced past the hash compare and halted with:
```
[guest-branch:missing-target] kind=DirectJump op=J source=0x1028bb0 target=0x101f040 pc=0x101f040
VULCAN4 HARNESS detail=no generated function at this pc pc=0x0101f040
```
`0x101F040` is a table of 16-byte syscall-wrapper stubs (`addiu v1,N; syscall; jr ra; nop`, bytes
`04 00 03 24  0C 00 00 00  08 00 E0 03  00 00 00 00`, N=4,5,6,7…) that the recompiler never emitted
(0 hits in `register_functions.cpp` / `ps2_recompiled_functions.cpp`). It sits in a gap between
`sub_0101E6B8` and `sub_0101F070` — executable code no symbol covers.

## 2. Root cause — TWO silent drops in the recompiler

1. `control_flow_analyzer.cpp::queueExternalEntryTarget`: a direct `J`/`JAL` target that is not inside
   any known function is dropped (`if (!containingFn) return;`). It never reaches the entry-point
   discovery. **Fix:** if the target is an executable address, add it to `externalEntryPoints`.

2. `ps2_recompiler.cpp::discoverAdditionalEntryPoints` (the loop over `externalEntryPoints`): a target
   with no *owning decoded* function is dropped (`if (!owner) continue;`). Upstream only ever attached
   external targets as RESUME points inside an existing owner — it never emitted a standalone function
   for a target in an un-symbolised gap. **Fix:** collect those null-owner targets and run them through
   `discoverAdditionalEntryPointsImpl` with the real decoder, so each is synthesized as a standalone
   guest function bounded by the next known function.

Patch: `tools/patches/ps2recomp-linux-w230-gap-entrypoints.patch` (47 added lines, 2 files).
Upstream note: PS2Recomp silently ignored direct jumps into executable code regions that no ELF
symbol or analysis pass covers — a `missing-target` halt at runtime instead of a recompiled function.

## 3. Regeneration (measured)

```
[recompiler] synthesized 14 standalone configured guest entry point(s)
[recompiler] synthesized 2 standalone gap entry point(s)          <- new
Generated functions: 723   (was 721)
Additional entrypoints: 9058
Warnings: 122, errors: 0, decode failures: 0
```
`0x101F040` now appears in `register_functions.cpp` (count 1) **without any TOML `entry_points`** entry —
the tool discovers and emits it itself. (The TOML `entry_points` path also works and was used to confirm
the diagnosis, but it was reverted; the shipped fix is the tool change.)

## 4. Boot after the fix (oracle ON)

No more missing targets (`missing_functions=0`; no `missing-target` logs). The boot report changed from
`halt=missing_function pc=0x0101f040` to:
```
halt=guest_blocked
HARNESS detail=the main thread is parked ... no other thread is runnable ...
              Runnable frames: none pc=0x0101f048
```
`pc=0x0101f048` is inside the newly-emitted stub (`jr ra` of the `addiu v1,4; syscall` wrapper). The
park is the next wall: the runtime does not implement **EE syscall 0x07 = ExecPS2**
(`Kernel/Syscalls/System.cpp:371` names it; the dispatcher falls through to "Unknown syscallId=0x7"
and returns 0). GT4's loader calls it at `0x101f078` (from `0x1028b30`) with `a0=0x100008 a1=0x0 a2=0x2
a3=0x3` — the self-relaunch into the main ELF.

## 5. The picture

`/mnt/ssd/vulcan4-build/run/w230-wall-capture.png` — window `0x2e00007` (`VULCAN 4 - …`), 640x448,
**14 colours** (black + white text): still the 2005 Sony disclaimer. Menu NOT reached.

## 6. Next

- This dish's recompiler-gap work is complete: 2 missing targets fixed, tool patched, regenerated.
- The wall after it is a RUNTIME feature: implement `ExecPS2` (reset/re-enter the guest ELF at the
  given entry with argc/argv) — a different lane from the recompiler gap.
- The oracle stays a diagnostic; the shipped crack is still the decoder's deterministic 12 KB
  corruption (§W229 §21–§27).

## 7. W231 — ExecPS2 (EE syscall 0x07) implemented; the relaunch target is not recompilable

Ground truth (read first): `resources/db-syscalls.md:26` and `resources/09-ps2tek.md` (07h) —
`ExecPS2(entry, gp, argc, argv)`: clears all internal kernel state and starts a fresh priority-0
main thread at `entry`; must not return. PCSX2 does NOT HLE ExecPS2 (it runs the real BIOS —
`pcsx2/R5900OpcodeImpl.cpp:982` only sets a debug breakpoint), so the corpus + the stub table are the
truth: the loader's stub at `0x101F070` is `addiu v1,7; syscall 0; jr $ra` and the caller at
`0x1028AD0` sets `a0=s1(entry)  a1=s2(gp)  a2=s0(argc)  a3=*(0x1036678)+4(argv)`.

Implementation (runtime, `Kernel/Syscalls/System.cpp`): syscall 0x07 resolves entry/gp/argc/argv,
logs ONE line `[execps2] entry=… gp=… argc=… argv=…`, and calls `PS2Runtime::requestExecPS2(...)`,
which records the request and `requestStop()`s the guest. A syscall stub CANNOT reset the machine in
place (its emitted `jr $ra` would resume the old frame, and the driver holds a reference to the
current thread's context), so the DRIVER (`tools/harness/vulcan4_harness.cpp`) performs the relaunch
after the invocation returns: it rebuilds a fresh launch context (GPRs zeroed, `pc=entry`, `sp=top of
RDRAM`, `a0=argc`, `a1=argv`, `gp=gp`), clears kernel state (`EeScheduler::reset`), and restarts the
loop. **RDRAM is deliberately untouched** — the loaded game data must survive, which is the point.

Measured (oracle ON):
```
[execps2] entry=0x100008 gp=0x0 argc=2 argv=0x3
VULCAN 4 LIMITATION: ExecPS2 (EE syscall 0x07) entry 0x00100008 is outside the recompiled guest
  image [0x01000008,0x0102dbec) — GT4's loader re-executes into low RDRAM (the 0x100000 page),
  which is not part of the ELF's PT_LOAD .text; the recompiler emits ELF code only, so Stage 1
  cannot execute runtime-loaded code.
halt=execps2_unmapped_entry
```
The ELF has exactly two PT_LOAD segments — `.text` @0x01000000 and `.data` @0x0102DC80. There is **no**
segment at 0x100000, so `0x100008` is RDRAM the loader populates at runtime (a second-stage image or a
copied engine), which static recompilation cannot emit. This is a genuine Stage-1 LIMITATION, reported
loudly instead of silently returning 0 (the previous behaviour, which then parked at `guest_blocked`).

Suite: 497/497. (The W230 gap-entry fix initially called `isExecutableAddress()` in the emitter path
too, where `allFunctions == nullptr` and the section list is unusable — 5/6 suite crashes. Guarded to
the discovery path only; green again.)

The recompiler-gap fix (`tools/patches/ps2recomp-linux-w230-gap-entrypoints.patch`) and the ExecPS2
runtime change (`tools/patches/ps2recomp-linux-w231-execps2.patch`) are both committed.
