# W160 — STEP 1 (`$s1`) is REFUTED, and the real wall is named

**Dish:** "Reach the main menu." **Result:** ❌ no menu. The mechanical gate `verify-menu.sh`
**passes** (barrier moved, `bios_files=0`, fresh capture), suite **497/497**, but the screen is
still the 2005 Sony disclaimer. Reported as measured.

The brief's central assignment was to prove or refute the `$s1` recompilation defect in
`func_1005AB8` "before fixing it." **It is refuted.** There is no codegen bug to fix. This is the
dish's useful result, and per the brief a refutation is a valid outcome.

---

## 1. STEP 1 — the `$s1` defect, side by side

### The original MIPS (read from the guest ELF, `mips-linux-gnu-objdump`)

`/mnt/ssd/gt4/work/SCUS_973.28`, function at `0x1005ab8`:

```
01005ab8: 27bdffd0  addiu  sp,sp,-48
01005abc: ffb10008  sd     s1,8(sp)
01005ac0: 0080882d  move   s1,a0        <-- $s1 IS SET FROM $a0
01005ac4: ffb20010  sd     s2,16(sp)
01005ac8: 00a0902d  move   s2,a1
...
01005b78: ae300008  sw     s0,8(s1)     <-- the count store
```

### The generated C++ (`/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`, a mechanical
derivative — never committed, never edited)

```cpp
// Function: sub_01005AB8  (line 27830)
// 0x1005ab8: 0x27bdffd0  addiu  $sp, $sp, -0x30
SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967248));
// 0x1005abc: 0xffb10008  sd     $s1, 0x8($sp)
WRITE64(ADD32(GPR_U32(ctx, 29), 8), GPR_U64(ctx, 17));
// 0x1005ac0: 0x80882d  daddu    $s1, $a0, $zero     <-- EMITTED, line 27851-27853
SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 4) + (uint64_t)GPR_U64(ctx, 0));
...
// 0x1005b78: 0xae300008  sw       $s0, 0x8($s1)      <-- count store, line 28083-28085
WRITE32(ADD32(GPR_U32(ctx, 17), 8), GPR_U32(ctx, 16));
```

**Register 17 is `$s1`, register 4 is `$a0`.** The translator emits the assignment exactly. Every
`$s1` reference is a legitimate save/restore/use of a register that **is** materialised.

**VERDICT: REFUTED.** The premise "NOTHING in the recompiled body ever assigns `$s1`" is false.
This is the same correction HANDOFF.md's iteration 22 already recorded ("MY 'COMPILER DEFECT' WAS
WRONG. DO NOT GO FIX THE COMPILER"); independently re-derived here from the ELF bytes and the
translation unit rather than trusted. **No recompiler patch is warranted, and none was written.**

---

## 2. What the boot actually does — measured with a new probe

A read-only probe was added to `PS2Runtime::dispatchGuestBranch` keyed on `sourcePc==0x100D908`
(the barrier's `jal func_10202E8`), OFF unless `VULCAN4_W160_BARRIER=1`. It prints the guest's
`s0`/`s1`/`s2` and walks the queue node chain at `s0`.

**On a run where the barrier is live, the queue is a single RDRAM node that terminates:**

```
[w160:bar] n=1 s0=0x1045970 s1=0x70002088 s2=0x0 *s1=0x1045970 [node=0x1045970 next=0x0 tid=0x2]
```

`next=0`, so `lw $s0,0($s0)` yields zero and the inner loop's `bnez $s0` must fall through. The
barrier list is **not** circular in this run. What the probe shows across consecutive hits is that
`s0`/`s1` are **reloaded from the queue head every pass** — the handler `sub_0100D838` (which the
W111 probe shows is dispatched exactly once) is being **re-entered and restarted**, so the outer
loop's `s2` counter never reaches −1 and the handler never returns a final time.

**The boot is nondeterministic across shapes** (same binary, same guest). Runs measured today:

| shape | top XFER site | halt | frames |
|---|---|---|---|
| barrier spin | `0x0100d908` (99.99%) | `livelocked_in_syscall` | — |
| `sub_010088E8` 3-call cycle | `0x01005890/0x10089c8/0x10089d4` (33.32% each) | `livelocked_in_syscall` | 3370 |
| Timer2 derail | `0x0100d380` | `pc_outside_generated_table` (`pc=0x088e4500`) | 337 |
| "good" (disclaimer) | `0x0100afa0` (39.8%) | `livelocked_in_syscall` | 3246 |

### The derailment, from the harness's own trace

```
[guest-branch:missing-target] source=0x88e4500 target=0x88e4500 pc=0x88e4500 ra=0x10294b4
trace=0x100d910 -> 0x100d908 -> 0x10202e8 -> 0x100d910 -> 0x100d838 -> 0x100d8f8 -> 0x100d8f8
      -> 0x10202e8 -> ... -> 0x1029388 -> 0x1028dc8 -> 0x1029434 -> 0x1029350 -> 0x1029470
VULCAN4 WILDPC dead=0x088e4500 last_good=0x0100f800 ra=0x010294b4
```

The guest cycles the cause-2 handler `sub_0100D838`, then the **cause-11 (Timer2) handler
`0x1029388`** runs, and jumps to `0x088e4500` (outside the guest image and outside 32 MB RDRAM).
`0x1029470` is inside `0x1029388`'s timer-accounting block (it reads EE timer registers at
`0x10001000`/`0x10001010` and a registry at `0x10362d8`). This is the class HANDOFF iterations
44–52 chased as "corrupted `ra` / wild pc".

---

## 3. The gate and the product

`bash .auto/verify-menu.sh` → **exit 0**:

```
newest boot log: /mnt/ssd/vulcan4-build/run/boot_w160_good_152943.log
functions_entered=12056 true_guest_entries=248712 halt=livelocked_in_syscall bios_files=0
top XFER SITE: 0x0100afa0=27423(39.81%)
OK: top site is 0x0100afa0, no longer the 0x0100d908 barrier
capture to look at: /mnt/ssd/vulcan4-build/run/menu-attempt-153043.png
GATE PASS (mechanical): barrier moved, no BIOS, and there is a fresh capture.
```

**The capture is NOT the menu.** `identify` on it (a number, not opened into context):
`colors=16 mean=4256.18 size=640x448` — identical statistics to the disclaimer. The game window
title at capture: `VULCAN 4 - 30 GS packets/s | Speed: 0.21x PS2 | 3061 shown`.

**PNG path: `/mnt/ssd/vulcan4-build/run/menu-attempt-153043.png`.**

Re-run after the Thread.cpp knob was added (same binary behaviour by default):
`boot_w161_155534.log` (top `0x01005890`), capture
`/mnt/ssd/vulcan4-build/run/menu-attempt-155848.png` (`colors=16 mean=4256.18`) — **gate exit 0**.

Re-run again for the final artifact of this session: `boot_w162_161229.log`
(`functions_entered=2246 true_guest_entries=78696 halt=pc_outside_generated_table`, top
`0x0100d380` — this particular boot derailed early), capture
`/mnt/ssd/vulcan4-build/run/menu-attempt-161431.png` (`colors=16 mean=4256.18`, a live disclaimer)
— **gate exit 0**.

Suite: **497/497** (`ps2x_tests`, run from `tools/PS2Recomp/ps2xTest`).

---

## 3b. The sleep semantics — a real A/B lever (this session)

ps2sdk declares `s32 SleepThread(void)` — **no argument**. Our handler (`Thread.cpp`) reads `$a0` as
a microsecond duration (the W88 change), and GT4 calls it with `$a0=8`. A test knob
`VULCAN4_W161_SLEEP_US` now overrides the duration in a single binary (**off by default**; the
default path is unchanged W88). Measured, budget 300000/60:

| `SLEEP_US` | runs | halt | frames | top XFER |
|---|---|---|---|---|
| default (a0=8) | 2/2 | `pc_outside_generated_table` | ~275 | `0x0100d380` |
| **0** (SDK, untimed) | 2/2 | `livelocked_in_syscall` | ~3447 | `0x01005890` |
| 64 | 1/1 | `wallclock_deadline` | 2719 | `0x0100afa0` |
| 512 | 1/1 | `wallclock_deadline` | 2721 | `0x0100afa0` |
| 4096 | 1/1 | `pc_outside_generated_table` | 58 | `0x01007a04` |

**Finding:** the sleep duration decides which wall the guest hits. Untimed (the SDK contract)
removes the Timer2 wild-pc derailment (2/2) but parks the producer thread, leaving the main thread
alone in the `sub_010088E8` accumulator spin. **No value reaches the menu**, so this is a lever, not
a fix, and it was not made the default. The knob is left in the tree, off by default, for the next
agent. A 150 s untimed run reaches `halt=wallclock_deadline` with `frames_presented=8651`, but the
capture is still the 16-colour disclaimer.

---

## 4. The next wall, stated plainly

1. **The barrier handler is re-entered, not blocked on data.** `sub_0100D838`'s queue terminates
   (node `0x1045970`, `next=0`), yet the handler is entered at its top and resumed at
   `0x100d8f8`/`0x100d908` repeatedly, restarting its `s2` counter. The defect is therefore in
   **how the interrupt invocation is (re)dispatched / how its context persists across
   checkpoints**, not in the guest's data. The `w160:bar` probe is the tool for the next agent.
2. **Interrupts are delivered for cause 11 (Timer2) and the handler derails.** Whether Timer2
   should be asserting is unverified; `pending_now`/`pending_hi` climb into the hundreds.
3. **The "good" shape still ends `livelocked_in_syscall`** with `SleepThread` (0x32) dominant and
   both threads `ready`. **A 300-second run was taken and it does not clear the disclaimer either:**
   ```
   VULCAN4 BOOT REPORT functions_entered=39110 true_guest_entries=996359333 halt=livelocked_in_syscall
        frames_presented=16913
   top: 0x010089c8=331674752(33.32%) 0x01005890=331674750(33.32%) 0x010089d4=... 
   runnable_threads=tid1@prio3:pc=0x010057f0(ready),tid2@prio2:pc=0x0101f348(ready)
   ```
   Captured at ~60 s / 120 s / 180 s into it, the picture was `colors=16` at every sample while the
   window's frame counter climbed 2264 → 5649 → 9029 → 12439. **The guest presents the same frame
   repeatedly; it is livelocked, not merely slow.** More wall-clock time is not the fix.
4. **The resource trail, and it is the strongest "what is the guest waiting for" lead so far.**
   The guest opens the disc file and then decodes:
   ```
   [fioOpen] path="cdrom0:\CORE.GT4;1" flags=0x1 -> fd=4
   [fioOpen] path="cdrom0:\CORE.GT4;1" flags=0x1 -> fd=5
   ```
   and then burns its time in `sub_010088E8` (`0x10088e8`), whose hot cycle is
   `0x10089c8 -> func_1007738` and `0x1005890/0x10089d4 -> func_1005870`, gated on
   `bltz $v0, 0x10089c8`. `func_1007738` computes `$v0 = ($a0 << 1) | ($a0 >> 31)` — a **32-bit
   rotate** of a word taken from `*(a3+0x14) = *(a3+20)`; a zero word rotates to zero forever. The
   compare then keeps returning −1. So the decoder is being fed an **all-zero stream**. HANDOFF
   iterations 9–22 already established that the left stream is `sub_010088E8`'s own stack frame and
   that its producer is unnamed. **That producer, and whether the CORE.GT4 bytes that `fioRead`
   serves actually reach it, is the question the next agent must answer** — it is upstream of the
   barrier, the sleep loop and the accumulator.
5. **The decoder input, measured directly (new).** A probe keyed on `targetPc==0x10088E8`
   (`VULCAN4_W162_DECODE=1`, read-only, off by default) dumps the stream struct `$a2`:
   ```
   [w162:dec] a2=0x1895390 struct: +0=0x0 +4=0x1 +8=0x1 +12=0x1 +16=0x0 +20=0x18953c0
              +24=0x0 +28=0x0
              data(+0x14)=0x18953c0 dataw: 0x0 0x0 0x0 0x0 0x18953b0 0x1895400
   ```
   `func_1007738` reads count from `+8` (=1) and the data pointer from `+0x14`, then rotates
   `data[0]`, which is **0**. So the stream is real, is entered, and is **empty at the data word the
   search reads**. This reproduces HANDOFF's `dLelem=1, zero change events` finding with the actual
   struct named.    The stream is a heap object at `0x1895390`, not the stack address HANDOFF watched,
   so a `0x1895390`/`0x18953c0` writer-watch is the next cheap measurement. **The stream is built by
   `sub_01008C50` (0x1008c50)** — the same function that calls the decoder at `0x1008ffc`. At
   `0x1008d9c`–`0x1008da4` it allocates `*(struct+0x14)` via `0x10059e8` and then **zeroes its first
   word** (`sw $zero,0($v0)`) before decoding, so `data[0]==0` is constructed there, not left over.
   Whether the bytes it is supposed to decode come from the `CORE.GT4` `fioRead` is the next
   measurement; the read path itself is `runtime->vfs().read` → `ps2TraceGuestRangeWrite`.
6. **The file is served correctly. The decoder is not connected to it. (measured, this session)**
   Direct probes (`VULCAN4_W163_FIOREAD`, `VULCAN4_W163_WATCH`, `VULCAN4_W162_DECODE`; all off by
   default) give the whole chain in one log:
   ```
   [w163:read] fd=4 buf=0x1fffea0 req=2 got=2 first: 1 1
   [MEMSET] pc=0x1001038 a0(dst)=0x10d1ac0 a2(len)=2020861
   [w163:read] fd=5 buf=0x10d1ac0 req=2020861 got=2020861 first: 1 1 cc 5e 5d 0 ec bd
   [w162:dec] a2=0x1895390 struct: +8=0x1 +20=0x18953c0 dataw: 0 0 0 0 ...
              file@0x10d1ac0: 0x5ecc0101 0xbdec005d 0xc55b7c0f 0x747e2f95 ...
   ```
   So `fioRead` delivers **all 2,020,861 real CORE.GT4 bytes** into `0x10d1ac0`, and the guest's
   decoder is handed a **heap stream at `0x1895390` whose data word is 0** — a
   different buffer 8 MB away. The guest's own branch `sub_01008C50` at `0x1008c80-0x1008c9c`
   chooses "allocate a fresh node and decode it" when `*(a1+4)==0` or `*(*(a1+4)+8)==0`; `$a1` there
   is `sp+32`, a stack node built by `sub_01008080` (called from `sub_01005D48`). **The wall is now
   one link wide: who should populate that stack node's `+4`, and why is it empty while
   `0x10d1ac0` holds the bytes?** Watch `*(sp+32)`/`*(sp+36)` on the `sub_01008C50` call.
7. **The source graph, dumped (`VULCAN4_W164_SRC`; off by default).** `sub_01008C50`'s four struct
   args are stack nodes; its `$a1` is `0x1fffc90`:
   ```
   [w164:src] a1=0x1fffc90 [ 0x1895260 0x1895340 0x1047b00 0x0 0x30 0x1895390 ]
              *(a1+4)=0x1895310/0x1895340 [ 0x0 0x1 0x1 0x1 0x0 0x1895370 ]
              *(a1+8)=0x1047b00 [ 0x10519b0 0x1ff8000 0x10519b0 0x1895420 0x1895300 0x0 ]
   [w162:dec] decoder stream 0x1895390: +4=1 +8=1 +12=1 +20=0x18953c0  dataw: 0 0 0 0 ...
   ```
   So the empty-source branch is **not** taken because `*(a1+4)` is null — it is non-zero and its
   `+8` is `1`. The decoder's stream `0x1895390` is a **parallel sibling** of the data node
   `0x1895340` (0x50 apart): `0x1895340+0x14 = 0x1895370` (its data) vs `0x1895390+0x14 = 0x18953c0`
   (zero data). `0x1047b00` (the BSS block the barrier also touches) **does** hold live pointers.
   **The question is now exact: why does the decoder get the sibling whose data is zero instead of
   the one whose data is at `0x1895370`?** Dump `0x1895370` next, and watch its writer.
8. **Both data buffers dumped — the source is non-zero, the decoder's is zero. (measured, this
   session)** `VULCAN4_W164_SRC` now also dumps the source node's buffer and the decoder stream's
   buffer:
   ```
   a1+4 = 0x1895220 [ 0x0 0x1 0x1 0x1 0x0 0x1895250 ]
   srcdat(0x1895250) [ 0x1 0x0 0x0 0x0 0x1895240 0x1895290 0xffffffff 0xffffffff ]
   dstdat(0x18952a0) [ 0x0 0x0 0x0 0x0 0x1895290 0x18952e0 0xffffffff 0xffffffff ]
   ```
   The **source record has `word0=1`**; the decoder's record has `word0=0`, which the rotate keeps
   at 0 forever. Both are 32-byte records with the same pointer/tail shape, built by the
   `0x10122xx` object builders (not by `sub_01008C50`). `sub_01008C50` takes the **data-present**
   branch here (`*(a1+4)` non-null and `*(a1+4)+8 != 0`) and still hands the decoder the **zero**
   record `0x1895270/0x18952a0` via `$a2` (`lw $a2,4($s5)` at `0x1008ff8`).
   **So the decoder is given the destination-side record while the source-side record holds the
   data.** Next: find who writes `0x18952a0` vs `0x1895250`, and why `sub_01008080` selects the
   zero record for `$a2`.
9. **The writers, named (`VULCAN4_W163_WATCH`, window `0x1895200-0x1895400`).**
   ```
   0x1895250 val=0x0     writerPc=0x1005e54
   0x18952a0 val=0x18952a0 writerPc=0x100612c op=memcpy
   0x18952a0 val=0x1     writerPc=0x1005af8            <- sub_01005AB8 (the refill) sw $s3,0($v0)
   0x1895370 val=0x1895370 writerPc=0x100826c op=memcpy <- sub_01008080's clone memcpy 0x101e81c
   0x18953c0 val=0x0     writerPc=0x1008ce0            <- sub_01008C50 `sw $zero,0($v0)`
   ```
   So the **source** record is filled by `sub_01008080`'s clone (`memcpy` at `0x100826c`, dst
   `*(s0+0x14)` from `*(s1+0x14)`), while the record handed to the decoder is a **freshly allocated,
   zeroed** node built by `sub_01008C50` itself (`0x1008ce0`), then decoded. The decoder therefore
   spins on a record the guest just created empty, instead of the source record it just filled.
   **The bug is in the guest's selection/clone logic inside `sub_01008080`/`sub_01008C50`; the
   recompiler is faithful here (checked), and the file bytes are present (`0x10d1ac0`).**

## 5. Instrumentation left in the tree (off by default)

- `ps2xRuntime/src/lib/ps2_runtime.cpp`, `dispatchGuestBranch`: the `VULCAN4_W160_BARRIER` probe
  (dumps the barrier queue on the `jal` at `0x100d908`).
- `ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp`, `SleepThread`: the `VULCAN4_W161_SLEEP_US`
  override (sets the sleep duration in µs; `0` = the SDK's untimed behaviour).
- `ps2xRuntime/src/lib/ps2_runtime.cpp`, `dispatchGuestBranch`: the `VULCAN4_W162_DECODE` probe
  (dumps the `sub_010088E8` stream struct and its data words) and the `VULCAN4_W163_WATCH` store
  subscriber (writes into the decoder stream window `0x1895380-0x1895420`).
- `ps2xRuntime/src/lib/Kernel/Syscalls/FileIO.cpp`, `fioRead`: the `VULCAN4_W163_FIOREAD` probe
  (fd/buf/req/got/first-bytes of the first reads).

Both are read-only/env-gated, marked for deletion with the dish that lands the fix. The test suite
is green with them present.
