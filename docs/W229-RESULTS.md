# W229 — the filler is REACHED and CORRECT; `FUN_0100F8C8` (gzip inflate) ZEROES the struct's base pointer

**Goal:** GT4 past the 2005 Sony disclaimer → the menu.
**Gate:** `bash /home/or/vulcan4/.auto/verify-menu.sh`. **Result of this dish so far:** no picture change
yet, but the W228 wall is broken open one layer deeper and the corrupting call is **named**.

This dish added a runtime probe (`VULCAN4_W229_FILLER`, OFF by default) at the `FUN_01004308` entry
and body, and ran hardware ground truth in PCSX2. Nothing in the product path was changed.

---

## 1. THE DECISIVE MEASUREMENT — ours REACHES the filler, with the same entry state

`[w229:fill]` fires on our side (`PS2Runtime::dispatchGuestBranch`, `targetPc==0x1004308`,
`src=0x100451c` — confirms `FUN_01004308` is called only from `FUN_01004500` at `0x0100451c`):

```
a0=0x1ffffb0 a1=0x10d1a50 a2=0x3 a3=0x1047b0c s0=0x1ffffb0 s1=0x1051a10 s2=0x1ffffb4 ra=0x1004524 sp=0x1fffe80
struct@a0: f0=0x1051a10 f4=0 f8=0 fC=0 f10=0 f14=0 f18=0 f1c=0 f20=0
```

So the W228 "never fires / different `s2`" hypotheses are both **refuted**: we reach the filler, and
its entry struct is `{obj, 0, 0}` — identical to hardware (`FUN_01000558` writes `+0=iVar2, +4=0, +8=0`
at `0x010005c8/0x010005d0/0x010005d8`).

## 2. HARDWARE GROUND TRUTH (PCSX2, paused — two points)

PCSX2 was found still paused at the W228 store `0x01004404`; native disasm + regs read live.

At the store (`0x01004404`):
```
s0=0x012BF100 s1=0x01FFFD14 s2=0x01FFFD10 v0=0x012BF184 v1=0x012BF182
a2=0x00000100 a3=0x00000080 ra=0x010043AC sp=0x01FFCB70
```

At `FUN_01004308`'s return into `FUN_01004500` (`0x01004524`, breakpoint):
```
s3=0x01FFFD10 ; struct:
+0 =0x01051a10  +4 =0x012bf100  +8 =0x005d5ecc  +C =0x012bf102  +10=0x00000080
+14=0x012bf184  +18=0x00000080  +1c=0x012bf204  +20=0x005d5dc8
```

`+4` is the base buffer, `+8` the length (6,119,116); `+C/+10/+14/+18/+1c/+20` are derived from the
16-bit words at the base (see §4). This exactly matches W228 §1.

## 3. OURS MATCHES UNTIL THE INFLATE — trace of the filler body

`VULCAN4_W229_FILLER=1` traces the filler body's dispatches:

```
n=5  src=0x1004360 tgt=0x1000fd8  a0=0x1ffffb4 a1=0x5d5ecc        f4=0x0
n=6  src=0x1000ff4 tgt=0x1011090  alloc(0x40,0x5d5ecc)            f4=0x0
n=7  src=0x1001008 tgt=0x101e81c  memcpy(dst=0x12bf100,src=0,len=0) f4=0x0   <-- alloc returned 0x12bf100
n=8  src=0x1001010 tgt=0x1010fd0  free(0)                          f4=0x0
n=9  src=0x1001030 tgt=0x101e9d0  memset(0x12bf100, 0, 6119116)    f4=0x0
n=10 src=0x1004368 tgt=0x1010b10  (after FUN_01000FD8)             f4=0x12bf100   <-- +4 SET, CORRECT
n=11 src=0x100438c tgt=0x100ed78  FUN_0100ED78(sp, ...)            f4=0x12bf100
n=12 src=0x10043a4 tgt=0x100f8c8  FUN_0100F8C8(sp)  (gzip inflate) f4=0x12bf100
n=d  src=0x1004424 tgt=0x1010bd0  (after FUN_0100F8C8)             f4=0x0         <-- +4 ZEROED
```

`FUN_01000FD8` allocates the 6 MB buffer at `0x12bf100` (same address as hardware), zeroes it, and
writes `struct+4 = 0x12bf100`, `struct+8 = 0x5d5ecc`. **So the allocator and the buffer store are
correct in ours.** The ONLY step between the correct `+4` and the zeroed `+4` is `FUN_0100F8C8`.

`FUN_0100F8C8` (`0x100f8c8`–`0x1010b00`, ~74 KB of MIPS) is a **gzip inflate** (`0x8b1f` magic),
called as `FUN_0100F8C8(sp)` where `sp = 0x1ffce10` is `FUN_01004308`'s own frame; the stream struct
it consumes was set up by `FUN_01010B10(sp)` (`vtable=&DAT_01036ac0` at `+0x44`, out at `+0x28`) and
`FUN_0100ED78(sp, obj[0]+6, obj[1]-6)`. The output window is `sp+0x1c/+0x20 = 0x12bf100`,
`sp+0x24 = 0x12bf10 0 -1`.

## 4. WHY THE ZEROED `+4` IS THE WALL

`FUN_01004308` then derives the rest of the struct from `*(struct+4)`:

```
a3 = u16be(base)                      -> +10
a2 = u16be(base + a3 + 2)             -> +18 ; a2 += a3
+C = base + 2
+14 = base + a3 + 4
+20 = +(8) - a2 - 4
+1c = base + a2 + 4
```

With `base = 0` ours produces exactly the measured garbage: `+C=0x2 +10=0 +14=0x4 +18=0 +1c=0x4
+20=0x5d5ec8` (hardware: `0x12bf102 / 0x80 / 0x12bf184 / 0x80 / 0x12bf204 / 0x5d5dc8`). Every one of
the differences is explained by `base=0`. This stream feeds `FUN_01004500 → FUN_01006f90 → …`, which
is why the parser later builds the length-1 stream and the `sub_010088E8` merge spins.

## 5. NEXT (started, not finished)

The corrupting call is `FUN_0100F8C8`. It is a large recompiled function with an indirect call
through the vtable at `sp+0x44` (`&DAT_01036ac0`, method `+0x14 = 0x01010b00`). Prime suspects, in
order:

1. **The indirect dispatch / vtable inside `FUN_0100F8C8`** — if `sp+0x44` is clobbered or the
   method resolves wrong, the inflate's refill callback writes astray. `VULCAN4_W221_JALR` traced
   only 40 indirect dispatches before the run derailed, none in this region; widen it.
2. **A write inside the recompiled `FUN_0100F8C8`** landing on the caller's frame — bound its stores
   with a write-watch on `struct+4` in *our* runtime (the store observer slot), or `ulimit`-style
   store-range trap.
3. **Resume at an interior label** skipping the prologue (the pattern this campaign has hit before).

Hardware A/B is already standing (PCSX2 paused; DebugServer `127.0.0.1:21512`); re-arm a watch on
`0x01FFFD14` from a fresh boot to prove hardware never writes it after the filler.

**No fix landed yet. No picture change. Suite unchanged (probe is OFF by default).**

## 6. THE ROOT CAUSE — an IRQ handler runs on a stack that overlaps the main thread's stack

The W229d store observer (armed on the struct at the inflate call site `0x010043A4`) caught the exact
writer:

```
[w229:w] addr=0x1ffffb0 size=8 val=0x0 writerPc=0x10293a4 op=WRITE64   <-- covers struct +0 and +4
```

`0x010293A4` is `sd s5,0x50(sp)` — the **prologue of `FUN_01029388`**. Ghidra: `FUN_01029388` is a
**guest interrupt handler** — it is registered by `FUN_01028de8` with
`AddIntcHandler(0xb, 0x1029388, 0, 0)` (IRQ 0xb). Its frame base is `sp = 0x1ffff60`, so its save area
`sp+0x50 = 0x1ffffb0` lands **exactly on the main thread's parser struct** (`+0`, `+4`). The main
thread was interrupted inside `FUN_0100F8C8`.

Why the overlap: the runtime runs guest interrupts as **separate scheduled invocations** on a reserved
"async callback stack", not nested on the interrupted thread's stack:

- `EeScheduler::dispatchIrq` sets the invocation `sp = 0` (`EeScheduler.cpp:1809`); when the queued
  invocation runs, `invocationStackTop()` (`EeScheduler.cpp:1555`) assigns it
  `reserveAsyncCallbackStack(0x4000, 16)` (`ps2_runtime.cpp:5324`).
- `reserveAsyncCallbackStack` hands out stacks downward from `m_asyncCallbackStackTop = PS2_RAM_SIZE`
  (`0x02000000`, `ps2_runtime.cpp:5338-5358`) — **the same address the main thread's `$sp` starts at**
  (`m_cpuContext.r[29] = PS2_RAM_SIZE - 0x10 = 0x01FFFFF0`, `ps2_runtime.cpp:5861`).
- So the async callback stack `[0x1FFC000, 0x2000000]` **overlaps the main thread's live stack**, and
  the first IRQ handler frame clobbers whatever the main thread keeps near the top — here the parser
  struct at `0x01FFFD10`-equivalent / ours at `0x1ffffb0`.

On real hardware an EE interrupt **nests on the interrupted thread's stack** (the handler pushes below
the current `$sp`), so it cannot clobber live data above it. Ours cannot, because it runs the handler
on a fresh top-of-RAM stack. Hardware's parser struct sits `0x2F0` below the stack top; ours sits
`0x50` below, so ours is the one in the collision zone.

**FIX CANDIDATE (not yet landed):** run interrupt invocations on the interrupted thread's stack (the
saved `$sp` at dispatch time), i.e. make the IRQ invocation nest below the current `$sp` exactly as the
hardware does — or, minimally and safely, move the async-callback-stack region below the main thread's
stack floor so the two can never overlap. This is a runtime change in `EeScheduler.cpp` /
`ps2_runtime.cpp`; **no `.h`** (project law), no `runner/*.cpp`.

## 7. REPRODUCE

`VULCAN4_W229_FILLER=1` on the harness; `[w229:fill]` (entry), `[w229:trace]` (body, `f4` before/after
each call), `[w229:out]` (post-filler struct), `[w229:w]` (store observer, names the writer). Hardware
A/B: PCSX2 paused at `0x01004404` / BP `0x01004524`.

## 8. FIX LANDED — IRQ handlers nest on the interrupted stack (struct now matches hardware)

`EeScheduler::dispatchIrq` (`EeScheduler.cpp:1808`) now sets the handler invocation's `$sp` to the live
interrupted `$sp` (`m_runtime.cpu()`) instead of `0`; the old async-stack path still applies when there
is no live `$sp`. `VULCAN4_W229_NONEST=1` restores the old behaviour for A/B. Runtime `.cpp` only — no
`.h`, no `runner/*.cpp`.

**Verified (3/3 runs, `[w229:out]`):** the parser struct now matches hardware **exactly**:

```
ours / hardware: f0=0x1051a10 f4=0x12bf100 f8=0x5d5ecc fC=0x12bf102 f10=0x80 f14=0x12bf184 f18=0x80 f1c=0x12bf204 f20=0x5d5dc8
```

Before the fix `f0`/`f4`/`f10`/`f18` were `0`. A/B (fix on vs `NONEST=1`): both settings show both run
shapes (`pc_outside_generated_table` at ~2.2k and `livelocked_in_syscall` at ~7.2k), so **no regression**
— the change only removes the clobber.

**Still not the menu.** The run now derails at `pc_outside_generated_table` with PC `0x0`,
`last_good=0x01009004` (the tail of `sub_01008C50`), `ra=0x0`. Two candidates: (a) a **deferred**
invocation whose captured `$sp` is stale by the time it runs (the handler's frame then lands on the
interrupted frame), or (b) a genuinely deeper parser wall now that the struct is correct. Next:
`VULCAN4_W229_NONEST` A/B with a longer budget, and a watch on the handler invocation's run-time vs
dispatch-time `$sp`. **No picture change; gate not yet a real pass.**

## 9. THE PAYOFF — the parser/merge inputs now match hardware, and the spin is GONE

With the §8 fix, the merge (`sub_010088E8`, the W227 wall) now receives **populated 32-element
streams** — and they are **byte-identical to PCSX2**:

```
[w227:merge] tgt=0x10088e8 src=0x1008ffc a0=0x1895450 a1=0x18953f0 a2=0x1895420 a3=0x18957e0 ra=0x1009004
[w227:struct] i=1 p=0x18953f0 count=0x20 base=0x18956c0 w0=0x3bace481 w1=0x8259142d   <- hardware W227 §4
[w227:struct] i=2 p=0x1895420 count=0x20 base=0x1895750 w0=0xc4531b7f w1=0x7da6ebd2   <- hardware W227 §4
```

The old wall — a 63-million-iteration spin at `0x01005890`/`0x010089c8`/`0x010089d4` — is **gone**.
The XFER census in a clean run is now flat (`top 0x01007a04=6.67%`, no dominant site), i.e. the guest
is doing real work, not spinning. The run lasts **~7 s / 360 frames** (was 1.4 s before the fix; the
`VULCAN4_W227_MERGE` harness probe segfaults on an out-of-bounds read, use the runtime probe).

**But the SCREEN IS UNCHANGED.** Our window is still the **16-colour disclaimer** (perceptual diff 1.5
vs the reference, 640×448, mean 16.6). So the CPU now gets past the parser/merge, but the picture does
not move — the next wall is **not** the merge. The new derail is a different one: the run ends with
`tid2` at `pc=ra=0x08438ee5` (KSEG, outside the table), `sp=0x010459e0` — the W122/W148/W197
corrupted-`ra` on the second scheduler thread, now exposed because the merge no longer spins. That,
and/or the screen still waiting, is the next dish.


