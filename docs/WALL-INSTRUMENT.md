# THE ZERO WAS VACUOUS — the instrument never printed the string (2026-10-01, Caine, second correction)

**Everything in `WALL-CDROM-GAMEDATA.md` and `STEER-2026-10-01.md` that says "fioOpen appears 0
times, therefore the guest never opens core.gt4" is WITHDRAWN.** The zero is an artefact of the
measurement, not a fact about the game. I gave you a confident number built on a string the program
never prints. That is the same failure this project has already paid for three times.

## What is actually true, and what is not

**The `fioOpen` count of 0 proves nothing.** `ps2_syscalls::fioOpen`
(`Kernel/Syscalls/FileIO.cpp:21-42`) contains **no trace call**. The only literal `fioOpen` in the
runtime is the *error* string at `FileIO.cpp:29` (`"fioOpen error: Invalid path address"`), which
fires only when the path pointer is invalid. **A successful open prints nothing at all.** Grepping
the log for `fioOpen` is like grepping for a string the program never emits.

**The syscall tally's silence is also not proof.** `PS2Runtime::syscallCounts()`
(`ps2_runtime.h:505,813`, incremented at `ps2_runtime.cpp:1632` before dispatch, printed by the
harness at `vulcan4_harness.cpp:2269-2286`) is a real instrument — 22 distinct syscall numbers,
summing to exactly 677,736. But `sceOpen` **reaches `fioOpen` outside that tally**: `gt4.toml:131`
lists `sceOpen@0x01024570`, `register_functions.cpp:8369` binds it into the function table, and in
the linked object it is a 21-byte forwarder that calls `fioOpen` **directly**, never through
`handleSyscall`. This is the fast-write bypass repeating one layer up, at the syscall layer.

**And the log is stale.** `boot_span.log` is 20:24:37; the linked `vulcan4_harness` is 20:26:54;
`libps2_runtime.a` is **21:52:44**. The log predates the runtime archive it describes by 1.5 hours.
`boot_span.log` also appears in **no run script at all** — only in two docs — and `FIRST-BOOT.md:262`
documents a different filename and a different optimisation level than `build_harness.sh:62` uses.

So: the mount retraction still stands (that came from reading code, not from the log), but the
"guest never asks" conclusion **does not**. We do not know yet whether the guest opens the file.

## Four ways a real open can be silent — all verified

1. `AGRESSIVE_LOGS` / `PS2_RUNTIME_LOGS` (`ps2_log.h:119-138`): if both are 0, `RUNTIME_LOG` compiles
   to `do {} while(0)`. CMake sets them via `PS2X_ENABLE_AGRESSIVE_LOGS`
   (`ps2Runtime/CMakeLists.txt:399-408`, cache default `ON`) — **verify the actual build's value.**
2. `ps2_log.txt` (`ps2_log.h:149,155`) is a **separate buffer**, opened `std::ios::out` — lines land
   there, not in the redirected stdout we have been grepping.
3. Harness env gates, all off by default: `VULCAN4_TRACE_WRITES` (`:286`), `VULCAN4_SHADOW` (`:294`),
   `VULCAN4_BRANCH_WATCH` (`:198`), `VULCAN4_WATCH_LO/HI/MAX` (`:117-130`),
   `VULCAN4_TRACE` / `VULCAN4_TRACE_ALL` (`:1031-1034`).
4. `[sceCdSearchFile]` has a **hard-coded rate cap** — `traceCount < 128 || %512`
   (`CD.cpp:551-556`) — so it would hide a late open even when tracing is fully enabled.

Unobserved writers elsewhere: `Stubs/FileIO.cpp:41,95,132` (`fstat`/`sceIoctl`/`stat` `memset`
straight into guest memory with no observer), and `Ps2FastWrite32` appears 20× in the log itself.

## The red test, specified well enough to implement without more research

**Do not use the `mkdir GAMEDATA && ln -s` experiment for this.** That tests the filesystem, not the
instrument, and cannot distinguish "no open happened" from "no open was reported".

**Step A — prove the instrument is live.** In `ps2xTest/src/ps2_runtime_io_tests.cpp`, reusing the
existing `TestContext` fixture and the `writeGuestString` / `setRegU32` idiom at `:377-393`:

1. `setRegU32(ctx,4,fileAddr); setRegU32(ctx,5,PS2_FIO_O_RDONLY); fioOpen(rdram,&ctx,&runtime);`
   against a known-good path — `rom0:ROMVER` already succeeds per `:185`.
2. Assert `fd >= 0`.
3. Redirect `std::cerr` and `std::cout` to a temp file around the call and assert the file contains
   a `fioOpen` line. **This fails today**, because no such line is printed. That is the point: it is
   the red test.
4. Separately assert `runtime.syscallCounts()` did **not** gain an open entry — this documents the
   bypass rather than pretending it is not there.

**Step B — prove the trace can be silenced.** Run the same binary twice:

- `cd /home/or/vulcan4/tools/PS2Recomp/ps2xTest && nice -n 10 /mnt/ssd/vulcan4-build/ps2xTest/ps2x_tests`
- Then rebuild with `-DAGRESSIVE_LOGS=0 -DPS2_RUNTIME_LOGS=0` (`build_harness.sh:17` /
  `FIRST-BOOT.md:249`) and re-run. The `RUNTIME_LOG` lines must vanish while the unconditional
  `sys::cerr` `[Syscall TODO]` marker (`System.cpp:335`) still appears. That pair is the control
  proving the macro is what silenced it.

**Accept when:** (a) with logging on, a known-good open emits ≥1 `fioOpen` line; (b) with
`AGRESSIVE_LOGS=0`, that line is absent **and** the unconditional marker is still present.

## Then, and only then, re-measure the boot

1. Fix the instrument (an unconditional `fioOpen` trace line, not one hidden behind a macro).
2. Rebuild. Confirm `PS2X_ENABLE_AGRESSIVE_LOGS` is actually `ON` in the build you are running.
3. Re-run the boot and produce a **fresh** log, named by the run script so it cannot be mistaken for
   the stale one.
4. Only then ask whether the guest opens `core.gt4`, and with what return value.

The `mkdir GAMEDATA` experiment remains worth doing — but as a **second** experiment, after the
instrument is known to work, and its result must be read as "the open still did not happen" or "the
open happened and returned −1", not as evidence about mounts.

## Could not verify

- Whether GT4's `sceOpen` executes at all during boot. That needs the fresh run above.
- Whether the current generated `ps2_recompiled_functions.cpp` (22:03) still emits `sceOpen` as a
  forwarder — it is **newer** than the linked object (20:07) and no longer contains that symbol, so
  today's build may differ from the one audited.
- The runtime path for `ps2_stubs::sceCdSearchFile`: it is LTO-eliminated from the binary and its
  caller is not in the numeric dispatcher.
- Two research lanes timed out before reporting: the guest-side ELF/string analysis, and the
  microVU-on-Linux build feasibility. Both are still open questions.
