# W249 — seeded the invisible handler roots; the handler now emits, but the loop still spins

## Discovery-loop iteration 1

Seeded the engine reachability with the syscall-override handler addresses the guest registers via
`SetSyscall` (added to the engine TOML `entry_points`, a tool INPUT change):
`handler_5B73C8@0x005B73C8`, `handler_5B7390@0x005B7390`, `handler_5B7408@0x005B7408`,
`handler_5B74E0@0x005B74E0`.

Re-emit (reachable-only, cap 400):

| | before | after |
|---|---|---|
| functions emitted | 758 | 758 |
| `5b73c8` in `register_functions.cpp` | 0 | **2** (emitted) |
| parts | 5 | 5 |

So the previously-invisible 0x83 handler `0x005B73C8` IS now in the emitted set (the exact W248 gap is
closed). Recompiled the 5 parts + register (`-O0`, one TU at a time, capped `systemd-run --user` unit
`v4-engcc3`, `free -g`=12 GB before start; ~80 s total), relinked, suite green.

## Boot (oracle ON) — the spin is UNCHANGED

```
VULCAN4 BOOT REPORT functions_entered=7490 ... halt=guest_cycle_no_progress
VULCAN4 MISSING-BOUNDARIES n=0
```

- `MISSING-BOUNDARIES n=0`: the override now resolves to an emitted handler (no longer a missing
  function), so step 2's "loud when unregistered" is satisfied here by construction — but
- the loop STILL does not exit: emitting `0x5B73C8` did **not** change `functions_entered` (7490) or the
  halt. So the divergence W248 named (un-emitted handler) was necessary but **not sufficient** — the
  handler now runs and still yields the wrong result (or the dispatched handler is not the one the
  caller's `v0` reflects).

## State

The specific W248 gap is closed; the wall remains `guest_cycle_no_progress`. New screen: NO (capture
`/mnt/ssd/vulcan4-build/run/w249-capture.png`, diff 1.13 vs the W231b disclaimer). Probe OFF by default;
nothing hand-edited; loader's 723 functions untouched; java/Minecraft untouched.

## NEXT

Re-run the `VULCAN4_FINDADDR` probe with the emitted handler to see whether `0x5B73C8` actually
executes (log its entry) — if it does and still returns garbage, trace its scan; if it does NOT, the
override invocation path is bypassing the handler entirely (the "silent garbage" path step 2 targets).
