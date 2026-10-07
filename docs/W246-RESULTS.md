# W246 — the FindAddress contract (static) and where the spin must come from

## The handler's contract (static, from the game's own code)

`System.cpp:1074-1092` already records the disassembly of the guest's 0x83 handler at `0x010285F8`
(an inlined copy of the kernel algorithm), and it is unambiguous:

```
10285f8  lw   v0,0(a0)          ; word at cursor
10285fc  beq  v0,a2, 0x102862c  ; match -> return the ADDRESS (a0)
1028600  sltu v0,a0,a1
1028604  beqzl v0, 0x1028630    ; a0 >= a1 -> done
102860c  addiu a0,a0,4          ; next word
1028630  jr   ra
1028634  move v0,a0             ; returns $a0  (end cursor on a miss)
```

So the contract is: **a0 = window start, a1 = window end, a2 = target word; returns the address of the
matching word on a match, or the cursor (`a0` == end) on a miss.** Our builtin
(`computeBuiltinFindAddressResult` + `setReturnU32(ctx, foundMatch ? resultAddr : end)`) is exactly
this, so **the scan/return algorithm is NOT the divergence.**

## What the caller requires (0x5B74E0-0x5B7518)

The loop repeats while `(s3-0x20C) != (s2-0x168)`, i.e. it exits when the two results are exactly
`0xA4` apart. `0xA4 = 0x83*4 - 0x5A*4` is the delta of the two syscall-table slots
(`0x8001218C`,`0x800120E8`), so the two searches must both FIND their slots and both return the address
in the SAME mirror family. Our `setEeSyscallOverride` puts the handlers at exactly those slots
(0x1218C=0x10285F8, 0x120E8=0x10285C0, W244), and our builtin canonicalises every result to KSEG1
(`0x80000000|addr`), which would make both results `0x8001218C`/`0x800120E8` -> delta `0xA4` -> EXIT.

## Therefore the divergence is (a) the ARGUMENTS, or (c) canonicalisation actually applied per call.

It is NOT (b): the scan algorithm matches the guest handler by construction. To decide between (a) and
(c) we need OUR `a0/a1/a2/v0` at the two `0x5B7408` calls — not captured this dish, because
`logFindAddressDiagnostics` is compiled only under `AGRESSIVE_LOGS` (`System.cpp:761` `#if
!AGRESSIVE_LOGS return;`); capturing needs a `VULCAN4_LOGS=1` build or a small OFF-by-default probe.

## Oracle

`set_breakpoint 0x005B7408` + resume: PCSX2 never hit it — it cycles at EE kernel idle `pc=0x81FC0`
with the cycle counter wrapping. **Not repaired this dish**; the instance needs a clean stop + relaunch
of `pcsx2-qt -debugger` (never touching java/Minecraft). Whether hardware even reaches `0x5B7408` at
this boot point is itself unresolved and is the first thing the repaired oracle must answer.

## State

No fix landed. Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; nothing
hand-edited; java/Minecraft untouched.

## NEXT

(1) OFF-by-default probe at `dispatchSyscallOverride`/`FindAddress` printing `a0/a1/a2/v0` + whether the
guest override dispatched — gives our args with no LOGS build. (2) Repair + re-break PCSX2 at `0x5B7408`
for hardware's args. Then pick (a) or (c) with numbers.
