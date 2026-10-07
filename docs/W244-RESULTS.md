# W244 — syscall table: OUR side is populated (corrects W243); oracle read inconclusive

## Table layout, measured in OUR run

`PS2Runtime::setEeSyscallOverride` (ps2_runtime.cpp:6187) writes the handler as a u32 to
`kTableBase + n*4`, `kTableBase = 0x80011F80 & 0x1FFFFFFF = 0x11F80`, and logs the readback. Our run:

```
[SetSyscall] n=131 handler=0x10285f8 slot=0x1218c readback=0x10285f8
[SetSyscall] n=90  handler=0x10285c0 slot=0x120e8 readback=0x10285c0
```

So base `0x11F80`, stride 4, value = the guest handler pointer; slot 0x83 (`0x1218C`) holds
**0x10285F8** and slot 0x5A (`0x120E8`) holds **0x10285C0**, both read back correctly. **The slots are
NOT unpopulated** — W243's "unpopulated" conclusion was wrong; our `SetSyscall` (0x74) already writes
them exactly at the guest's expected addresses, and the 0xA4 slot delta (0x1218C-0x120E8) is present.

## ORACLE — inconclusive this dish

PCSX2 (paused at `pc=0x00081FC0`, the EE kernel idle) reads `0x11F80 / 0x120E0 / 0x12180` as **all zero**.
That is NOT the loop point and the emulator is not confirmed to be at the matching boot stage, so this
read does NOT contradict our populated table — it is simply inconclusive and must be re-taken with the
emulator stopped at the 0x83 loop head.

## 0x83 dispatch

Still the G1.8b pattern: `Dispatcher.cpp:305` hardcodes `case 0x83: FindAddress(...)`; the registered
guest handler `0x10285F8` is not dispatched. Since the builtin and the guest handler implement the same
word-scan, this alone does not explain the spin — but it is still worth routing 0x83 to the registered
handler (the task's step 3) so the guest's own code runs.

## State / next

The divergence is NOT the syscall-table population. The remaining suspects, in order: (a) the
`FindAddress` window/target arguments at the loop head, (b) the KSEG1 canonicalisation of the returned
address. Next: stop PCSX2 AT the 0x83 loop head and dump its `[a0,a1)` window + target + result, then
diff against ours. Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; nothing
hand-edited; java/Minecraft untouched.
