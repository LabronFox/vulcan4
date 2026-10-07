# W243 — 0x83: who answers it, and why the loop still spins

## Is 0x83 a kernel syscall or a guest handler? — BOTH, and BOTH produce the same algorithm

- ps2SDK `__NR_FindAddress = 0x83` (kernel syscall number).
- GT4 ALSO installs its own handler: the loader called `SetSyscall(0x74)` and the runtime logged
  `SYSTABLE n=0x83 slot=0x1218c handler=0x10285f8` — a guest-installed 0x83 handler at `0x010285F8`.
  That handler is an inlined copy of the same word-scan (documented in `System.cpp:1074-1092`) and
  returns the matching word's address on a match, or the cursor `$a0` (end of window) on a miss.

## Who answers it in OUR run? — the RUNTIME BUILTIN, not the guest handler

`Kernel/Syscalls/Dispatcher.cpp:305` hardcodes `case 0x83: FindAddress(...)`, so 0x83 is served by our
builtin and does NOT dispatch to the registered guest handler `0x10285F8`. That IS the G1.8b pattern
("every later 0x83 fell through to our builtin instead of the guest's own code"). **However**, the
builtin and the guest handler implement the same scan, so this alone does not change the outcome.

## The earlier "return 0" bug is ALREADY FIXED — and the spin persists

`System.cpp:1097-1099` records the exact prior bug (`returning 0 on a miss makes `prevResult-0x20C !=
curResult-0x168` unreachable`) and the fix: `setReturnU32(ctx, foundMatch ? resultAddr : end);` — on a
miss it returns the window END, not 0. That fix is present. So the spin now is NOT a 0 return: it is
that the two scans find **no match**, so `s3`/`s2` are the two window ENDS, whose difference is not the
hunted `0xA4`, and the loop never converges.

The `0xA4` is the syscall-table slot delta (`0x83*4 - 0x5A*4 = 0xA4`, table base `0x80011F80`, per the
`System.cpp` comment). So hardware's scans FIND the syscall-table slots `0x8001218C` (slot 0x83) and
`0x800120E8` (slot 0x5A); ours find nothing in those windows → the slots are not holding the expected
words → **the guest's syscall-table population (what `SetSyscall`/the loader wrote) is the first
divergence**, upstream of both the builtin and the loop.

## Not landed

The batch (a) route 0x83 to the guest handler `0x10285F8` when registered, and (b) dump our RDRAM at the
two FindAddress windows and diff against PCSX2 at the loop head to find the missing writer. Neither was
completed this dish. Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; nothing
hand-edited; java/Minecraft untouched.

## Next

Dump `[a0,0x8008xxxx)` for both calls at the loop head in ours vs PCSX2; the first differing word names
the writer that must run before the loop.
