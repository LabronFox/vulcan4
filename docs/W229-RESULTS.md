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

## 10. SHAPE A ROOT-CAUSED — a suspended frame is REUSED across a yield

The PC-0 derail (`dead=0x0 last_good=0x01009004 ra=0x0 sp=0x01fffc70`) is **not** `sub_01008C50`
returning with a legitimately-zero `ra`. The harness arrival probe (`VULCAN4_W229_ARR`, OFF by
default) shows two arrivals at the merge-return label `0x1009004`:

```
ARR9004 n=1 ra=0x1009004 sp=0x1fffc20 savedAtSp40=0x010082e4   <- correct caller return
ARR9004 n=2 ra=0x1009004 sp=0x1fffc20 savedAtSp40=0x00000000   <- ZEROED -> epilogue jr $ra -> PC 0
```

`sub_01008C50` saves its caller's `$ra` at `sp+0x40` (`0x1fffc60`). A store observer armed on that
frame (`VULCAN4_W229_ARR`) caught what rewrites it **while `sub_01008C50` is suspended**:

```
W229 W addr=0x01fffc60 size=8 val=0x01008318 writerPc=0x0100559c op=WRITE64
W229 W addr=0x01fffc60 size=8 val=0x01008318 writerPc=0x0101d438 op=WRITE64
W229 W addr=0x01fffc50 ... writerPc=0x0100558c   ; sub_010055XX prologue saves
```

`0x0100559c` is inside **`sub_010055XX`**, called from `FUN_01004500` (`jal 0x010055e0`) — a *different*
call chain (the `FUN_01000558 → FUN_010047C0 → FUN_01004500` filler chain) that reuses the **same stack
addresses** while `sub_01008C50`'s frame is still live. So: the function yielded, its guest frame stayed
on the stack, and another chain was then run at the **same `$sp`**, overwriting the saved `$ra` (and
eventually with 0). On resume at `0x1009004` the epilogue restores `ra=0` → PC 0.

This is a **stack/reentrancy defect**, not a translation bug: a suspended guest frame is not protected
from reuse. It is the same family as §4/§6 (a fresh invocation running at a stack that overlaps live
guest data). **NEXT:** find why the runtime runs the `FUN_01004500` chain at `sp≈0x1fffc20` (the
suspended `sub_01008C50` frame) — likely the yield/resume path resetting `$sp` to the thread top, or a
second invocation getting an overlapping stack. `VULCAN4_W229_ARR` reproduces both arrivals and the
writer.

## 11. FIX LANDED — async-callback stacks moved BELOW GT4's main stack (both walls gone)

The root of the whole family: our runtime runs interrupt/callback handlers on an **async-callback stack
reserved downward from `PS2_RAM_SIZE`** (`ps2_runtime.cpp`), but **GT4 sets its own main thread stack to
the very top** — `SetupThread(stack=0x1ff8000, size=0x8000)` → top `0x2000000` (measured with
`VULCAN4_W229_SET`). The two regions overlap entirely, so a handler frame lands on live guest data.

**Fix:** `m_asyncCallbackStackTop = 0x01FF0000u` (both reset sites in `ps2_runtime.cpp`) — the reserved
async region now sits **below** GT4's `[0x1ff8000, 0x2000000]` main stack. Handlers keep `sp=0` in
`dispatchIrq` so the queue still assigns them the async stack.

**Verified after the fix:**
- Parser struct matches hardware again: `f0=0x1051a10 f4=0x12bf100 f8=0x5d5ecc fC=0x12bf102 f10=0x80
  f14=0x12bf184 f18=0x80 f1c=0x12bf204 f20=0x5d5dc8` (`[w229:out]`).
- **The 63M merge spin is gone**: the top XFER sites are no longer `0x01005890/0x010089c8/0x010089d4`;
  runs last the full budget (`wallclock_deadline`, ~1500-2700 frames).
- Suite green 497/497. Committed nested `e7effb1` + docs/probes.

## 12. THE REMAINING WALLS (both pre-existing, deeper)

Two nondeterministic shapes remain, and the **screen is still the 16-colour disclaimer**:

1. **The W122 VSync-handler thread-wake loop.** `FUN_0100d838` is the guest's **cause-2 (VSync)
   handler**; it walks three scratchpad thread-wait lists and calls `sce_iWakeupThread(target)` for
   each node. Measured (`VULCAN4_W229_BAR`): the head is `scratchpad[0x70002088] = 0x1045970` (a node
   **on tid2's own stack**), the list has **1 node**, and the call is
   `src=0x100d908 targetTid=2 runTid=2`. So it is NOT a growing list and NOT a GetThreadId barrier
   (W122's "0x10202E8 = GetThreadId loop" was a mis-map: `0x10202E8` is the `sce_iWakeupThread` thunk;
   the GetThreadId loop is `entry_1020400`). The 166M transfers at `0x100d908` in one shape are the
   handler being re-entered across checkpoints. `tid1` sits at the decompressor `0x100f800`
   (`wait=sleep`) and never runs. This is the W122 scheduler wall, unchanged by the W229 fixes.
2. **The `tid2` corrupted-`ra` derail.** `dead=0x88468107/0xb7a70010/0xb30058df`, `last_good=0x100f800`
   (`FUN_0100f390`'s `jr $ra`), `sp=0x10459e0`. Installing a store observer perturbs the race away
   (W148/W197's Heisenbug), so it could not be caught this session.

Also observed in a long run: GT4 repeatedly opens `/BASCUS-97328GAMEDATA/core.gt4` on the memory card
and gets `result=-4` (no save), and re-runs the path-copy. PCSX2 reaches the menu without a save too,
so the MC failure is not expected to be the blocker.

**The hang, named.** A 3-minute run ends `halt=guest_cycle_no_progress` with
`tid1@prio1:pc=0x01000638(running)`. `0x1000638` is `FUN_01000558`'s **failure infinite-loop**
(`01000638: nop x5; 0100064c: b 0x01000638`). Per the decompile, that loop is taken only when
`FUN_010047c0` (the loader's top-level parse) returns 0 — i.e. **our parse reports failure** and the
loader spins forever. That (not just the VSync wall) is why the screen freezes. `FUN_010047c0`'s return
value is the next thing to measure side by side (it is `v0` at the `bnel $s1,zero,0x1000658` at
`0x1000630`).

## 13. THE PARSE RETURNS 0 (traced to the instruction)

`VULCAN4_W229_FAIL=1` (with `VULCAN4_W229_FILLER=1`) traces the loader chain. The path is exact:

```
src=0x10005d4 tgt=0x10047c0   FUN_01000558 calls FUN_010047C0 (param_1 = 0x1ffffb0)
src=0x10047e0 tgt=0x100b6f8   FUN_010047C0 calls FUN_0100B6F8
src=0x10047ec tgt=0x1004500   FUN_010047C0 calls FUN_01004500  (v0=0x1 in the delay slot)
src=0x10005fc tgt=0x102cbf8   back in FUN_01000558, s1=FUN_010047C0's return = 0x0  <-- ZERO
```

At `0x10047F4` the generated code is `beqz $v0, 0x10048D8` — `$v0` is `FUN_01004500`'s return. Ours
is **0**, so `FUN_010047C0` skips the stream copy and returns 0 (its `FUN_0101E81C` call at
`src=0x10048C0` never fires), and `FUN_01000558` takes the `s1==0` fail loop at `0x1000638`.

**So the live blocker is now `FUN_01004500` returning 0** — the same function whose filler
(`FUN_01004308`) now produces a hardware-correct struct. The next measurement is the hardware A/B for
`$v0` at `0x010047F4` (PCSX2 breakpoint) vs ours; and, if it differs, walking `FUN_01004500`'s own
return path (it calls `FUN_01006F90` after the filler).

## 14. HARDWARE A/B: `FUN_01004500` returns `iVar3 == 0` — hardware 1, ours 0

PCSX2 restarted fresh, breakpoint at `0x010047F4`, read live: **`v0 = 0x00000001`** (and
`s0=0x01FFFD10`, `s1=0x01051A10`, `sp=0x01FFFC50`). Ours is `v0=0`.

`FUN_01004500`'s tail is `return iVar3 == 0;`. So hardware's `iVar3 = 0` (→ `true`), ours `iVar3 != 0`
(→ `false`). Where `iVar3` comes from:

- if `iStack_6c` is null/empty → the compare is skipped (`bVar2` path) and `iVar3` is derived from
  `iStack_5c`;
- otherwise `iVar3 = FUN_01005870(iStack_6c)` — **the stream compare** (`jal func_1005870`, the same
  comparator that sat in the merge loop).

`iStack_6c`/`iStack_5c` are the parsed stream objects filled by `FUN_01006F90` (called right after the
filler). So the remaining divergence is in those parsed streams: ours compares non-equal, hardware
equal. The next measurement is the A/B of `iStack_6c`/`iStack_5c` (and `FUN_01005870`'s return) at
`0x01004500` — the merge inputs already match, so the divergence is one level below `FUN_01006F90`.

## 15. HARDWARE A/B OF THE TWO COMPARE STREAMS — hardware's are EQUAL, ours DIFFER

PCSX2 fresh, breakpoint at `0x01004748` (`jal func_1005870`, the compare inside `FUN_01004500`).
Hardware `a0` (pre-delay-slot) `= 0x01FFFBE0`, `a1 = 0x01895770`; the delay slot loads
`a0 = *(0x01FFFBE0+4) = 0x01895740`. Stream contents:

| side | arg | struct `+8` (count) | `+c` | `+14` (base) | base `w0 w1 w2` |
|---|---|---|---|---|---|
| hw | a0 | `0x10` | `0x40` | `0x01895c80` | `4bb5e6bf 05e985f7 9bb5bb30` |
| hw | a1 | `0x10` | `0x10` | `0x018957a0` | `4bb5e6bf 05e985f7 9bb5bb30` |
| ours | a0 | `0x10` | `0x40` | `0x1895da0` | `4bb5e6bf 05e985f7 9bb5bb30` |
| ours | a1 | `0x10` | `0x10` | `0x18958c0` | `**893d7a82 4a91273a 7acf5c27**` |

**Hardware's two streams are byte-identical in their payload; ours' second stream differs.** So
hardware's `FUN_01005870` returns 0 → `iVar3=0` → `FUN_01004500` returns 1 (boot continues); ours'
returns non-zero → `iVar3!=0` → returns 0 → loader hang. Ours' first stream matches hardware exactly,
so the corruption is in **the second stream** (`+c=0x10`, base ours `0x18958c0`), which is built by
`FUN_01004448(auStack_60, param_1+0x1c, …)` / the `iStack_3c` path from `param_1+0xc/+0x10`. That is
the precise next target.

## 16. THE SECOND STREAM IS `SHA-512(decompressed buffer)`, REVERSED — and OUR BUFFER DIFFERS

The second stream is built by `FUN_01004448` → `FUN_01003700` (a 0x80-byte-block feeder) →
`FUN_01001290` (**SHA-512**) → `FUN_01003938`/`FUN_010010b0` (finalize + byte-swap). It is a
**SHA-512 of the decompressed 6 MB buffer at `0x12bf204`, length `0x5d5dc8`**.

Proof: the dumped buffer (`VULCAN4_W229_DUMP`, written to `/tmp/w229buf.bin`) hashed in Python gives
`sha512(buf) = 6479c786…893d7a82`, and `bswap32(sha512(buf))[::-1]` = `893d7a82 4a91273a 7acf5c27
5d7789fa…` — **exactly ours' computed `a1`**. So the hash function is CORRECT and `a1 = our buffer's
hash`. The expected value (`a0 = 4bb5e6bf 05e985f7 9bb5bb30…`) is what the correct buffer hashes to.

**So the root divergence is the decompressed 6 MB buffer itself.** Ours and hardware match at
`+0x0, +0x100000, +0x200000, +0x280000, +0x500000` but **diverge from ≈`+0x170000`**:

```
+B0000   ours   01000224 05008214 01000224 12000324
         hw     2d20a003 b4fe050c 02000524 44000010
+1a0000  ours   3f000324 8300023c 886843ac 18001116     <- this is hardware's +0x1d0000
+1d0000  hw     3f000324 8300023c 886843ac 18001116
```

i.e. from ≈`+0x170000` **ours is shifted 0x30000 earlier than hardware** — the decompressed stream is
**missing/misordering ~0x30000 bytes** before that point. That is the root cause of the failed parse,
the loader hang, and the frozen disclaimer. Next: bound the exact divergence start between `+0x100000`
and `+0x170000` and find which decode step drops the 0x30000 bytes.

**It is a block REORDER, not a single shift.** Ours' `+0x160000` = hardware's `+0x110000` (ours' data
appears *later* there), while ours' `+0x1a0000` = hardware's `+0x1d0000` (ours *earlier*): the two
buffers hold the same code/data but at permuted offsets. A linear gzip inflate would never reorder its
output, so the suspect is not a simple bitstream desync — it is whatever fills this 6 MB region
(the filler's `FUN_0100F8C8` inflate writes through `0x12bf100`, but its measured output window was
empty, so a later copy/decode step is the more likely filler). Locating that writer is the next step
(`VULCAN4_W229_*` probes remain, all OFF by default).

`VULCAN4_W122_BARRIER=1` (force the barrier word) no longer moves the wall to the merge (that is fixed);
it derails instead. Probes added (all OFF by default): `VULCAN4_W229_BAR`, `VULCAN4_W229_SET`,
`VULCAN4_W229_THR`, `VULCAN4_W229_STK`, `VULCAN4_W229_IRQ`, `VULCAN4_W229_ARR`.

## 17. THE FILLER'S DECOMPRESSOR (`FUN_0100F8C8`) — input matches, output does not

`VULCAN4_W229_INF` dumps the decompressor entry (`targetPc==0x100F8C8`; the filler's call has
`ra=0x10043AC`). Ours: `sp=0x1ffce10`, `fC=0x10d1ac8` (input), `f10=0x12bf0bd` (input end),
`f1c=0x12bf100` (output), `f24=0x12bf0ff`. PCSX2 conditional BP `ra==0x010043AC`: `a0=0x01FFCB70`,
`a1=0x010D1AC8`, `a2=0x012BF0BD`, `s0=0x012BF100` — **same input ptr/end/output**, and the compressed
input bytes match (`c55b7c0f 747e2f95 d8acb5ef 38ed7c72 …`).

So the divergence is **inside `FUN_0100F8C8`'s own decode** (bitstream desync appearing by `+0x110000`
of output). It is ~74 KB; the next step is to bisect the output to the exact first differing byte and
instrument the decode loop there.

**Tension to reconcile:** §9 measured the parser/merge inputs matching hardware, and the parser reads
decompressed data. Either this `0x12bf100` blob is a separate asset/checksum blob (not the parse
input), or that sample was coincidental — to resolve after the decompressor bisect.

**Bisect (PCSX2 live, buffer base `0x12bf204`):** the output matches at `+0x0/+0x100000/+0x200000/
+0x280000/+0x500000`. Inside `[+0x100000, +0x104000)` the mapping is a **permutation**, not a shift:

| ours offset | hardware offset | bytes |
|---|---|---|
| `+0x100000` | `+0x100000` | `27bdffe0 ffb00000 0000802d ffb10008` |
| `+0x101000` | `+0x102000` | `dfb00000 dfb10008 dfbf0010 03e00008` |
| `+0x101800` | `+0x100400` | `3c020083 ac43a110 24020001 14820005` |
| `+0x104000` | `+0x108000` | `2406ffff 8e430004 03a0282d 24630190` |

So the decompressor emits the **same code/data blocks at permuted offsets** — a block-placement
difference, not a lost bit. `FUN_0100F8C8` is the suspect; the next step is to find where in it the
output block offset is computed.

## 18. FULL-BUFFER DUMP + THE CORRUPTION IS A DOUBLE-WRITE BY THE HUFFMAN DECODER

PCSX2's DebugServer socket (`{cmd:'read_memory',...}` newline-JSON on `127.0.0.1:21512`) was driven
directly to dump the whole buffer, and the compressed input was dumped too:

- **The compressed input is byte-identical** — `/tmp/ourscmp.bin` vs hardware's dump over
  `0x10d1ac0..0x12bf0bd` (2,020,861 bytes) → `cmp -l` prints nothing.
- **The output blob's first difference is at byte `872988` (`0xD4FDC...` i.e. output offset `0xD521C`,
  absolute `0x1394420`)**: ours lacks hardware's `7c 00 45 8c` (`lw a1,0x7c(v0)`), present instead is
  `2d 30 00 00`.

A store-observer watch on that word (`VULCAN4_W229_OW`) caught the writer — and the tell:

```
[w229:w] addr=0x1394420 val=0x7c writerPc=0x100f800   <-- CORRECT, written first
... (0x00,0x45,0x8c)
[w229:w] addr=0x1394420 val=0x2d writerPc=0x100f800   <-- WRONG, overwrites it
```

So the blob is **decoded correctly, then partly overwritten**. The writer is `FUN_0100F390`
(`0x100f800`/`0x100f4ec`), a **DEFLATE Huffman decoder** (literal branch `uVar18==0x10` writes a byte;
`0x0f` is the end-of-block; back-references copy from a 32 KB window at `iVar13`). Its only caller is
`FUN_0100F8C8` at `0x1010A68`, and it is invoked repeatedly (one call per deflate block;
`VULCAN4_W229_HUF` shows the output pointer `f20` advancing monotonically: `0x12bf100`, `0x12e1695`,
`0x131bb24`, …).

So the failure is a **DEFLATE decode/back-reference divergence inside `FUN_0100F390`** (or its
`FUN_0100F8C8` driver) that rewrites already-correct output. Next: instrument the literal vs
back-reference branch at `0x100f800` around the `0x1394420` write and compare with hardware.

## 19. THE DECODER'S OUTPUT POINTER — monotonic, with one anomaly, and a shared dest

`VULCAN4_W229_HUF` logs each `FUN_0100F390` invocation's `a0`/`out`/`in`. The output pointer is
monotonic across the whole blob **except call #23 = `0x0`** (`0x155e56a` → `0x0` → `0x1591ce5`) — a
decoder call handed an output pointer of zero.

The match-copy writer (`VULCAN4_W229_OW`, registers now printed): the divergent word `0x1394420` is
written by **two calls**, both with `t1` (the decoder's output local / `puVar9`) = `0x1394420`:

```
call 11: hufOut=0x1393103  s1=0x1394408 (distance 0x18)  v0=0x7c   correct
call 49: hufOut=0x189313a  s1=0x13943d8 (distance 0x48)  v0=0x2d   wrong
```

But call 49's *entry* output is `0x189313a` while its copy dest is `0x1394420` (earlier) — so within
call 49 the output local moved **backward**. The decompiler shows the decoder writing `*puVar9` and,
on an output-buffer flush (`puVar9 == puVar12`), **reloading `puVar9 = *(param_1+4)`** after calling the
driver callback at vtable+0x1c. So a driver callback that resets the stream's output pointer would do
exactly this. Next: instrument that flush/callback path and the `0x0` call-23 case.

## 20. THE DECODER IS YIELDED AND RESUMED — the suspect is the resume state

`FUN_0100F390` has a resume `switch` case for **every instruction** and 8 checkpoint sites, and the
harness arrival probe (`VULCAN4_W229_DRES`) confirms it is resumed repeatedly at interior pcs —
mostly `0x100f800` (the `sb` copy loop itself) and `0x100f418`, all with `ra=0x01010a70` (the
`FUN_0100F8C8` return). So the decoder runs **across yields**, and its loop state (`t1`/`t4`/`s1`/`s3`
— the copy destination, bound, source and offset) is saved and restored by the scheduler at each
resume.

That is the same class as §10/§11: a guest function suspended across a yield. The next probe is
whether the resumed `t1`/`t4`/`s1`/`s3` at `0x100f800`/`0x100f418` match the values the MIPS would
have had, i.e. whether the resume path re-materialises the copy state correctly.

## 21. THE CORRUPTION IS ONE ~12 KB MATCH-COPY RUN

A full byte diff of ours' blob vs hardware's (`/tmp/w229buf_1.bin` vs `/tmp/hwbuf.bin`, 6,118,856
bytes each) gives **exactly one contiguous run of differences**:

```
differing bytes: 11,365   run: 0xD521C .. 0xD80B2  (11,926 bytes)
```

and hardware's bytes over that range are **not present anywhere** in ours' blob. So it is not a shift
or a block permutation — it is a **single ~12 KB region overwritten with wrong data**, consistent with
one DEFLATE match (or a short run of matches) decoded with a **wrong distance** (measured at
`0x1394420`: correct source `0x1394408` = distance `0x18`, ours `0x13943d8` = distance `0x48`), whose
wrong source cascades for the match length.

So the defect is narrow: **the match-distance decode in `FUN_0100F390` goes wrong once**, producing a
~12 KB bad copy. Everything else in the 6 MB matches hardware. The next probe is the distance/length
registers (`s3`, and the window offset) for the match that starts at `0xD521C`, on both sides.

The diagnostic knob `VULCAN4_W229_NOYIELD` (force no checkpoint yields **only** while
`pc ∈ [0x100f390,0x100f8c8)`, OFF by default) was added to test yield-vs-decode; taking it
**deadlocks the boot** (the decoder must yield for the rest of the system to run), so it cannot be used
as-is without a scoped resume.

## 22. TWO MATCHES AT THE SAME DESTINATION — the decoder re-processes one output position

With `s1`/`s2`/`s3` now printed (`s2 = s1+s3` = the match source end, so `s1`=src, `s3`=length, `t1`=dst,
`distance = t1 - s1`):

```
call 11: t1=0x1394420 s1=0x1394408 s3=0xc  -> length 12, distance 0x18,  v0=0x7c  (correct)
call 49: t1=0x1394420 s1=0x13943d8 s3=0x15 -> length 21, distance 0x48,  v0=0x2d  (wrong)
```

**Both writes have the same destination `t1=0x1394420`** but different `(length, distance)`. A linear
DEFLATE decoder never writes the same output position twice, so the decoder **re-processes one output
position** — consistent with the §20 finding that `FUN_0100F390` is yielded and resumed at interior
pcs. The first pass produces the hardware-correct bytes; a later pass, with different match
parameters (wrong bit/decode state), overwrites a ~12 KB run.

So the fix target is the **resume**: when `FUN_0100F390` is re-entered at an interior pc
(`0x100f800`/`0x100f418`) the decode state (bit accumulator/position and Huffman window) must continue
exactly, and it does not. Next: dump the stream's bit state (`param_1[0..2]`) at every decoder resume
and compare to a straight-through run.

## 23. THE RESUME LOSES THE COPY STATE — `t1=0x0` on entry at `0x100f800`

`VULCAN4_W229_DRES` (harness arrival probe for `pc ∈ [0x100f390,0x100f8c8)`) reports **86 resumes at
`0x100f800`, every one with `t1=0x0`**. `t1` is the copy destination (`sb $v0,0($t1)` at `0x100f800`),
so the resumed decoder runs its copy loop with a **NULL destination** — the loop state (`t1`, and by
implication `s1`/`s3`) is **not carried across the yield**.

The back-edge at `0x100f864` (`bnel $s1,$s2,→0x100f800`, delay `lbu $v0,0($s1)` then
`ctx->pc = 0x100F800u; if (runtime->eeCheckpointDue()) return;`) *should* resume at `0x100f800` with the
live `t1`/`s1`, but the resumed context has `t1=0`. That is the defect that lets a decode position be
re-processed and corrupt the output.

Next: trace where the decoder's `$t1` is lost between the yield and the resume (the emitter's checkpoint
return path, the caller `FUN_0100F8C8` epilogue when it unwinds on a yield, or the scheduler's
context save). This is the same family as the §10/§11 suspended-state work.

## 24. THE GATE IS FALSE-PASSING — a human must eyeball the capture

A fresh window-only capture of the harness (640×448, **16 colours** = the 2005 disclaimer) run through
`verify-menu.sh` exits **0 ("GATE PASS")**: the reference `.disclaimer-reference.png` is compared at
32×32 grey and gives a perceptual diff of **13.3** (threshold 6), because the stored reference does not
match our current disclaimer render. So the gate currently **passes on the disclaimer** and cannot be
trusted as the sole decider — the menu is **not** reached (verified: fresh capture is 16 colours).

This is a gate/asset defect, not a product result: the real deliverable (a capture of GT4's own menu)
is still absent.

## 25. THE DECODER LIVELOCKS AT THE COPY LOOP — yields there with constant state

`VULCAN4_W229_CK` now also logs when `eeCheckpointDue` returns **true** (a real yield) inside
`FUN_0100F390`. The decoder yields **at `pc=0x100f800`** (the copy loop's `sb`) with `due=1` and a
**constant** `(t1,s1,s3) = (0x12d400c, 0x12ce7dc, 0x1b)` across every one of the (first 200) yields.
The checkpoint at the copy-loop back-edge (`0x100f864`) is therefore due on essentially every iteration,
so the decoder suspends and is re-entered at `0x100f800` over and over without the copy advancing — a
**livelock at the DEFLATE copy loop**. That is consistent with §22's finding that the same output
position is written twice with different match parameters.

Note: the CK/CKyield state is read from `runtime.cpu()` (a periodic copy), so the exact `t1` may be a
stale snapshot; the reliable part is that the yields are real (`due=1`) and land on `0x100f800`. Next:
why the copy-loop checkpoint is due every iteration (a perpetually-pending event / slice), and whether
the resume at `0x100f800` actually re-executes the `sb`/`t1++`.

**Hardware A/B (PCSX2 watchpoint on `0x1394420`, write):** the first hit is the big 16 MB memset
(zeroing, `0x101ea1c`); after that the word is written **once more** — a single decoder write.
Ours writes it **twice** (correct `0x7c`, then wrong `0x2d`). So our decoder makes one **extra** write
that hardware does not: the re-process is confirmed against the console, not just inferred.

## 26. IT IS DETERMINISTIC — so it is a RECOMPILER translation bug, not a yield race

Two independent runs (`VULCAN4_W229_DUMP`) produce **byte-identical** blobs
(`cmp /tmp/w229det_1.bin /tmp/w229det_2.bin` → 0), and both differ from hardware at the **same** offset
`872989` / run `0xD521C..0xD80B2`. The scheduler's yields are nondeterministic, so if they affected the
decode the output would vary run to run — it does not. Therefore the decode divergence is
**deterministic** and points at the **recompiler's translation of `FUN_0100F390`** (or a deterministic
driver step), not at the yield/resume machinery.

That reframes the fix: compare the generated `sub_0100F390_0x100f390` against the MIPS disassembly of
`0x100f390..0x100f8c8` instruction-by-instruction, focusing on the length/distance decode (the one
wrong match: length `0x15`/distance `0x48` where correct is `0xc`/`0x18`). A deterministic mis-translated
instruction is the target; a recompiler patch would go in `tools/patches/`.

Spot-check result: the MIPS of `0x100f390..0x100f608` (obtained live via PCSX2's disassembler) matches
our emitted `sub_0100F390` instruction-for-instruction across the Huffman tree walk, the literal write
(`0x100f4ec sb`), the window write (`0x100f500 sb`) and both loop controls (`0x100f508 bne→0x100f418`,
`0x100f864 bnel→0x100f800`) — including the `sllv`/`dsllv` masking and the branch-likely delay slots.
So the mis-translation, if that is what it is, is not in those regions. The other deterministic
candidate is the **Huffman-table build in the driver `FUN_0100F8C8`** (`0x1010900..0x1010a00`, the
`0x120`-entry code-length tables at `sp+0x500`/`sp+0x574`): a single wrong table entry would deterministically
mis-decode exactly the symbols that use it, which matches the single ~12 KB bad run.

## 27. W230 — THE ORACLE PROVES THE HASH COMPARE IS THE BLOCKER (boot advances)

MEASURED 2026-10-07. Caine built the exact console payload to `/mnt/ssd/vulcan4-build/w229-oracle.bin`
(6,118,856 bytes = `0x5d5dc8`, sha256 `7728b0eb540bc690e415d10c44a0444cb9f4815be5fa7e536b43c4647ebe41a0`).

The decoder driver's real call order inside `FUN_01004500` (measured from the recompiled table + the
`VULCAN4_W229_FAIL` probe), which corrects an earlier assumption that `FUN_0100F8C8` runs *before*
`FUN_01004500`:

```
0x100451c -> FUN_01004308 -> (0x10043a4) FUN_0100F8C8            [decode the 6 MB blob]
0x1004688 -> FUN_01004448(auStack_60, 0x12bf204, 0x5d5dc8)        [SHA-512 of the blob]
0x1004748 -> FUN_01005870(a0=expected 4bb5e6bf.., a1=ours)        [compare]
```

So the decode runs **inside** `FUN_01004500`; the LAST moment the digest can still change is the
`FUN_01004448` call site `0x1004688` (`0x01004748` is the compare, already after hashing — injecting
there would be too late).

New probe `VULCAN4_W229_ORACLE=<path>` (OFF by default; unset is byte-for-byte today's behaviour)
memcpy's the file over the decoded blob at `0x1004688`, then logs ONE line at the compare. With it set:

```
[w230:oracle] copy dst=0x12bf204 len=0x5d5dc8 injected=1 hashWord0=0x4bb5e6bf
```

`0x4bb5e6bf` is exactly the expected digest's first word (a0), i.e. the hash now **MATCHES**.

RESULT: the boot **ADVANCES PAST the hash compare**. The loader fail-loop `0x1000638` is **not** taken
(0 occurrences vs 1 in the no-oracle control) and the run reaches a **NEW wall**:

```
[guest-branch:missing-target] kind=DirectJump op=J source=0x1028bb0 target=0x101f040 pc=0x101f040
VULCAN4 HARNESS detail=no generated function at this pc pc=0x0101f040
```

`0x101f040` is a table of 16-byte syscall-wrapper stubs (`addiu v1,N; syscall; jr ra; nop`, bytes
`04 00 03 24 0C 00 00 00 08 00 E0 03 00 00 00 00` then N=5,6,7…) that the recompiler did **not** emit
(0 hits in `register_functions.cpp` and `ps2_recompiled_functions.cpp`); it is not a recompiled
function entry, so a direct `J` into it halts.

CONTROL (no oracle): `halt=stuck_in_syscall`; the loader fail-loop `0x1000638` is taken once.

CAPTURE: `/mnt/ssd/vulcan4-build/run/w229-oracle-capture.png`, window `0x2e00007` (640x448), **16
colours** — still the 2005 Sony disclaimer. (An earlier capture grabbed the pre-present blank frame,
solid magenta = `GenImageColor(...MAGENTA)` at `ps2_runtime.cpp:502`; the visible game window is the
child titled `VULCAN 4 - …`, not the borderless 650x482 parent.)

CONCLUSION: the hash compare is CONFIRMED as the sole loader blocker — a correct 6 MB blob makes the
loader succeed and boot advances. The real fix remains the decoder's deterministic 12 KB corruption
(§21–§26). The next wall *after* that fix is the unrecompiled syscall-stub table at `0x101f040`.



