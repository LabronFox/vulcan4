# W226 — the spin's entry is a RESUME (emitted switch), and the halt syscall IS 0x32/SleepThread

**Dish:** `25-who-entered-the-spin`. **Result:** ❌ no picture change (fresh capture, disclaimer); gate
fails. **Outcome (P2):** both questions answered with evidence.

## (a) WHERE tid1's `pc=0x010057F0` came from — a RESUME, not a dispatch

The emitted body of `sub_010088E8`
(`/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp:43943`) begins with the resume machinery:

```
void sub_010088E8_0x10088e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    switch (ctx->pc) {
        case 0x100894cu: goto label_100894c;
        case 0x1008960u: goto label_1008960;
        ...
        case 0x10089c8u: goto label_10089c8;
        case 0x10089d0u: goto label_10089d0;
        case 0x10089dcu: goto label_10089dc;
        ...
        case 0x1008a78u: goto label_1008a78;
        default: break;
    }
    ctx->pc = 0x10088e8u;
```
`grep -n "sub_010088E8_0x10088e8" ps2_recompiled_functions.cpp` → line 43943.

**This is option (i): the frame is RE-ENTERED by resume.** When the guest yields (its SleepThread
syscall) or is serviced, the harness/scheduler re-enters the generated function via the dense function
table at whatever `ctx->pc` holds — here one of the resume labels (`0x100894C..0x1008A78`) inside
`sub_010088E8` — which is why **no `dispatchGuestBranch` into `0x10088xx` is ever recorded** (W225b's
zero is real) and yet the function runs. The spin's own internal `jal`s (`0x10089C8`/`0x10089D4` →
`func_1007738`/`func_10057F0`) *are* dispatched and *do* appear (W221 log), pairing with tid1's
`pc=0x010057F0` (inside `func_10057F0`, the memcmp the spin calls).

So: **the whole parser chain never dispatches because the spin's frame is resumed, not called.** This
resolves the W225b contradiction — "runs but never dispatched" is exactly resume semantics.

## (b) The halt syscall number — disassembled from the guest bytes

`mips-linux-gnu-objdump -d --start-address=0x101F340 --stop-address=0x101F360 SCUS_973.28`:
```
101f340:	24030032 	li	v1,50        ; v1 = 0x32
101f344:	0000000c 	syscall
101f348:	03e00008 	jr	ra           ; <- tid1's pc
101f34c:	00000000 	nop
```
**The syscall number is `50` = `0x32`.**

`resources/db-syscalls.md` (the authoritative table) says:
```
| 0x32 | SleepThread | — | $v0=status | impl | Cooperative yield — decrements wakeup_count or goes WAIT |
| 0x10 | AddIntcHandler | a0=cause, ... |
```
**So the harness's label `SCE syscall 0x32 (SleepThread)` is CORRECT, and HANDOFF.md:2559's retraction
("SleepThread is 0x10, NOT 0x32") is WRONG** — `0x10` is `AddIntcHandler`, not SleepThread. The
retraction misread the table (it conflated the *Interrupts* section `0x10–0x17` with the thread block,
where `0x10` in a *different* listing is `SleepThread`). **This is a retraction of a retraction:** the
halt IS a SleepThread (`0x32`), and `0x32`/`0x33`/`0x35` should NOT be avoided in this project.

## Fresh capture (the gate was judging a stale image)

No `disclaimer-break-*.png` writer exists in the repo; W174's PNG was an external `import -window` of the
harness's raylib window on `:0`. This run produced a **fresh** capture on Xvfb `:99`:
```
DISPLAY=:99 import -window 0x200007 menu-attempt-062216.png
  -> /mnt/ssd/vulcan4-build/run/menu-attempt-062216.png  (640x448, 16 colours, mean 4256.18)
```
`verify-menu.sh` now judges this run:
```
GATE FAIL: the capture is PERCEPTUALLY the same as the disclaimer (diff=1.539 < 6) - the screen did not
meaningfully change
```
So the picture genuinely has not changed — and the gate is no longer blind.

## Positive control

The W221 emitter probe fires 87×/30 s armed vs 0 unarmed (W221) and records real dispatches; the
`sub_010088E8` resume switch is quoted from the emitted source. The disassembly is the guest's own bytes.
The fresh PNG is `identify`-verified (640x448, 16 colours), not opened.

## Gate (authoritative, honest)
`GATE FAIL` (v3, fresh `menu-attempt-062216.png`, perceptual diff 1.539). Suite **497/497**.

## NEXT
The resume semantics are confirmed: the spin's frame is re-entered while the parser chain (which would
feed it) is not re-run. The next measurement is the scheduler's **frame refresh**: why the parked frame's
pc is left inside the spin rather than the parser after the SleepThread yield — i.e. whether the
SleepThread-resume should rewind to the parser call, not resume the callee. This is the
SleepThread-yield-loop question, now correctly labelled (0x32).
