# W245 — oracle could not be placed at the loop head; dispatch order re-checked

## Oracle — BLOCKED

Set a breakpoint at `0x005B7408` (the engine's `addiu v1,0,0x83; syscall` loop head) and resumed
PCSX2. It never hit: six 20-s polls showed the instance cycling at the EE kernel idle `pc=0x81FC0` with
the cycle counter RESETTING between polls (0x80004540 → 0x00568454 → 0x005682BC → 0x81FC0 …), i.e. this
PCSX2 instance is not reliably booting GT4 to the engine. So hardware's `a0/a1/a2/v0` at the loop head
were NOT captured, and candidates (a) args differ / (b) scan disagrees / (c) return not canonicalised
CANNOT be decided by the oracle this dish. (Do not touch the emulator process beyond BP/resume.)

## Dispatch order — re-checked in the code (corrects W243/W244 wording)

`Kernel/Syscalls/Dispatcher.cpp::dispatchNumericSyscall` calls
`dispatchSyscallOverride(syscallNumber, …)` **FIRST** (line 9) and only falls through to the `switch`
(where `case 0x83: FindAddress(...)` lives, line 305) when there is no override. So when the loader's
`SetSyscall(0x74, 0x83, 0x10285F8)` registration is present, 0x83 dispatches to the GUEST handler
`0x010285F8`, not the builtin — the "builtin shadows the guest handler" claim is NOT correct as stated;
the builtin is a fallback. (Whether `findEeSyscallOverride(0x83)` actually returns the handler in our
run is the thing to confirm with a one-line probe — see NEXT.)

## Not landed

No fix this dish: the oracle is blocked and the measured candidate cannot be chosen without it. The
table is populated (W244), the guest handler should be reached (this dish), so the remaining suspect is
(b)/(c) — the scan result or its KSEG1 canonicalisation — pending a hardware capture at the loop head.
Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; nothing hand-edited;
java/Minecraft untouched.

## NEXT

1. A one-line probe in `dispatchSyscallOverride` printing `syscallNumber`, `handler`, and whether it
   dispatched, for 0x83 (OFF by default) — proves who answers 0x83 in our run.
2. Repair the PCSX2 instance (its own unit/restart, never touching Minecraft) and re-break at 0x5B7408
   to capture `a0/a1/a2/v0` + the window bytes.
