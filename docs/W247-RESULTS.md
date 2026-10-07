# W247 — OUR args captured; the slots hold the ENGINE's handlers; the first scan is already wrong

## 1. Our arguments (probe `VULCAN4_FINDADDR`, OFF by default)

```
[w247] pc=0x005b7408 a0=0x80000000 a1=0x80080000 a2=0x005b73c8 a3=0x006d6380 v0=0x00000000 s2=0 s3=0
                         [1218C]=0x5b73c8 [120E8]=0x5b7390 [8001218C]=0x5b73c8 [800120E8]=0x5b7390
[w247] pc=0x005b7408 a0=0x80000000 a1=0x80080000 a2=0x005b7390 ...        v0=0xffffffff s2=0 s3=0xffffffff
[w247] pc=0x005b7408 a0=0x00000003 a1=0x80080000 a2=0x005b73c8 ...        v0=0x00000001 s2=s3=0xffffffff
[w247] pc=0x005b74ec a0=0x00000003 ...                                     v0=0xffffffff s2=s3=0xffffffff   (loops)
```

- **Our builtin is NOT answering 0x83**: the `findaddr` probe in `ps2_syscalls::FindAddress` printed
  nothing → `dispatchNumericSyscall` routed 0x83 through `dispatchSyscallOverride` to the GUEST handler
  (`0x10285F8`), exactly as W245's code re-check said. The "builtin shadows the guest handler" story is
  dead.
- First call: window `[0x80000000, 0x80080000)`, target `0x005B73C8`, result **0** (should be the match
  `0x8001218C` or, on a miss, the end `0x80080000`). The target and the window are RIGHT.

## 2. The two slot measurements reconciled — BOTH earlier ones were read wrong

`[0x1218C] = 0x005B73C8` and `[0x120E8] = 0x005B7390` in our run, via BOTH the low and KSEG0 addresses.

- W243's "unpopulated" came from PCSX2, not us — and it was not our memory at all.
- W244's `slot=0x1218c readback=0x10285f8` is the LOADER's registration; the ENGINE later registers its
  OWN handlers `0x005B73C8` / `0x005B7390` at the same slots (the values the loop now hunts). So the
  loop's targets ARE the slot contents — the write address and the scan address agree (`0x1218C`).

## 3. Divergence

Not (a) (args are right: target == slot value, window covers the slot) and not the table population.
It is the **scan/handler's result**: the first call returns 0 instead of the matching slot address,
then the caller feeds `s2+4` back as the next `a0` (→ 3), and `s2=s3=0xffffffff` makes the loop's
`s3-0x20C != s2-0x168` permanently true. So the failure is in how the 0x83 call's result is produced
(guest handler execution or our syscall plumbing around it), not the arguments.

## 4. Oracle

NOT repaired this dish (no clean stop/relaunch performed). The instance still cycles at `pc=0x81FC0`.

## State

No fix landed. Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; the probe is
OFF by default; nothing hand-edited; java/Minecraft untouched.

## NEXT

Two concrete probes, in order: (a) trace the GUEST handler `0x10285F8` — its `a0` on entry, the word it
first reads at `0x8001218C`, and the branch taken — to see why the match isn't seen; (b) repair PCSX2
and break at `0x5B7408` to compare the first call's `v0`. The first (no oracle) will likely name it.
