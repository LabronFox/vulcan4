# W242 — the engine's spin named: a sce_FindAddress (0x83) retry loop

## What the loop IS

The cycle detector's `VULCAN4 CYCLE` reports a tight 2-PC cycle:
`0x5B7408` (`insn=0x24030083` = `addiu v1,zero,0x83`) and `0x5B74EC` (`insn=0x40982d` = `daddu s3,v0,zero`).
Decoding the engine image around it:

```
0x5B7408  addiu v1,zero,0x83 ; syscall ; jr ra      <- syscall-0x83 (sce_FindAddress) stub
...
0x5B74E0  lui  a1,0x8008
0x5B74E4  jal  0x5B7408            (FindAddress #1)
0x5B74E8  addiu a2,s5,0x73C8       (delay)
0x5B74EC  daddu s3,v0,zero         (s3 = result #1)
0x5B74F0  b    0x5B7510
0x5B74F4  addiu s1,s3,-0x20C       (delay)
0x5B74F8  addiu a0,s2,0x4
0x5B74FC  lui  a1,0x8008
0x5B7500  jal  0x5B7408            (FindAddress #2)
0x5B7504  addiu a2,s4,0x7390       (delay)
0x5B7508  daddu s2,v0,zero         (s2 = result #2)
0x5B750C  addiu s0,s2,-0x168       (delay)
0x5B7510  bne  s1,s0, 0x5B74D0     -> LOOPS while (s3-0x20C) != (s2-0x168)
```

So it is a **sce_FindAddress retry loop**: it looks up two tables (windows `[a0,0x8008xxxx)` for targets
`s5+0x73C8` / `s4+0x7390`) and repeats until the two returned addresses differ by exactly `0xA4`
(`s3 - s2 == 0x20C - 0x168 = 0xA4`). One sentence: *it retries two `sce_FindAddress` lookups until the
addresses they return sit a fixed delta apart.*

## First divergence

Our `FindAddress` (`Kernel/Syscalls/System.cpp::computeBuiltinFindAddressResult`) scans
`[start,end)` words for a word equal to `target` and returns its address, else **0**. Both lookups
return 0, so `s3 = s2 = 0` and the loop test is `-0x20C != -0x168` — permanently true → the spin. On
hardware the searched pointers exist, so `FindAddress` returns real addresses and the loop exits.

Therefore the divergence is **the state `FindAddress` searches, not the loop**: the window
`[a0,0x8008xxxx)` in our RDRAM does not contain the target word that hardware's does — i.e. something
upstream was supposed to write the looked-up pointer (or the window base/target is off). The scan
returning 0 is the *symptom*; the *first divergence* is that written data.

## Not landed

The fix (writing the missing looked-up pointer / correcting the window+target) and the PCSX2 oracle run
to this exact point were NOT completed this dish. The loop is named and the divergence class is fixed
(address looked-up data absent), per the "first divergence wins" law. Halt unchanged
`guest_cycle_no_progress`; capture unchanged; suite green; nothing hand-edited; java/Minecraft untouched.

## Next

Locate the writer of the looked-up pointer: dump our RDRAM at the two windows at the loop head and
compare against PCSX2's RDRAM at the same moment (the oracle already stands at the ExecPS2 point).
