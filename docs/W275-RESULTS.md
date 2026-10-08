# W275 — service the DMAC interrupt invocation, so `ExecPS2` fires

**Date 2026-10-08.** One page to let the next seat resume. Full predecessor evidence:
[`docs/W274-RESULTS.md`](W274-RESULTS.md) (no copy here — read that for the derivation).
Mission text: `.auto/crew/W275-MISSION.md`; live task list: `.auto/crew/tasks.md`.

## GOAL (one line)

The DMAC interrupt invocation is serviced, `tid2` clears its DMA-busy wait and signals semaphore 7, and
`ExecPS2` fires (count > 0 in the boot log) so GT4's own engine code begins to run.

## VERIFIED W274 STATE (each line with its evidence)

- **Engine fully linked** — `nm -C vulcan4_harness` → **20,127** (`sub_` 20,111 = 19,404 engine + 707
  loader; `entry_` 16, all loader). Binary 347,718,056 B. Link is necessary, NOT sufficient: boot is
  unchanged. (`nm` without `-C` returns 0 — C++-mangled names; the working proof is `nm -C`.)
- **`ExecPS2` fires 0 times** — `halt=guest_blocked`, `functions_entered=723`, `bios_files=0`.
  Raw: W274 boot logs under `/mnt/ssd/vulcan4-build/run/`.
- **The DMAC hypothesis (UNPROVEN — this dish proves or kills it):** the wakeup chain, measured 5 ways
  in W274: `tid2` (`FUN_01000BA0`) is the only `SignalSema(7)` waker; it parks polling DMA completion
  at `0x0100AF50` (bit 8 of `0x10008000` + busy byte `0x7000206D`). The VIF0 ch0 transfer **completes**
  (`CHCR=0x145`, chain, 3 tags → 2704 B; `[w274:dmac] queued/drain cause 0 ×1`, STR cleared). The DMAC
  handler `FUN_0100DAE0` (`0x100dae0`) is **registered** (`AddDmacHandler`) and **matches** `dispatchIrq`
  (cause=0 enabled=1 hasFn=1) — **yet is never entered** (0 × `target_pc=0x100dae0`).
  `inv_by_kind=[intr=94, dmac=0, override=0, other=0]`. **Hypothesis:** the DMAC
  `GuestInvocationKind::Interrupt` invocation queued by `dispatchIrq` is never *serviced* by the
  scheduler drain, while INTC invocations drain fine. Fix would live in `ps2xRuntime/src/lib/`
  (`EeScheduler` servicing) — **no fix until two independent angles agree on the drop point.**
- **Picture still the 2005 disclaimer.** `bash .auto/verify-menu.sh` → not passed; newest capture is
  BYTE-IDENTICAL to the disclaimer reference. Say plainly: the picture is still the disclaimer.

## RECORD-FIRST (law 8) — VERIFIED, RAW

T0 was executed by the lead; T5 verifies and records it. The W274 diagnostic probes (6 files:
`VULCAN4_W274_DMA` `[w274:*]` logs in `ps2_memory.cpp` ×3, `ps2_runtime.cpp` ×1, `EeScheduler.cpp` ×1)
were captured whole as `tools/patches/ps2recomp-linux-w275-precapture.patch` (78,733 B, 2026-10-08
09:04) and reverted. The kept 0x5b fix is carried by its own committed patch.

```text
$ cd /home/or/vulcan4 && git status --porcelain
(empty)
$ git log -1 --format='%H %an <%ae>'
d3fc63baaeb2bdba45ffece0a0e1c172b6496578 Or Golan <or024662@gmail.com>
$ git log -1 --format='%s'
W275: record-first (law 8) — capture the full W274 probe tree as ps2recomp-linux-w275-precapture.patch, revert the 6 diagnostic files (W253/W252/W274-DMA probes) to a clean tree, keep the 0x5b GetEntryAddress fix (its patch now committed)

$ cd /home/or/vulcan4/tools/PS2Recomp && git diff --numstat
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
$ git status --porcelain
 M ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
```

**Per-file capture check (the not-optional one).** The one live nested diff is `System.cpp +98`. The
patch that carries it carries at least that many added lines:

```text
$ git apply --numstat /home/or/vulcan4/tools/patches/ps2recomp-linux-w274-0x5b-getentryaddress.patch
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
```

98 added in the live diff == 98 added in the patch → **captured**. The nested working-tree diff is the
kept 0x5b fix, and it lives in a committed outer patch. No uncaptured source remains.

## MACHINE

### Disk (law 6 — root must stay < 90%)

```text
$ df -h / /mnt/ssd
Filesystem                         Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-ubuntu--lv  178G  145G   26G  85% /
/dev/sdb                           440G  335G   83G  81% /mnt/ssd
```

Root `/` at **85%** (< 90% PASS — but only 26G free, watch it). SSD `/mnt/ssd` at **81%** (83G free).
Builds go to `$VULCAN4_BUILD` / `$TMPDIR=/mnt/ssd/tmp`, `-j4` `nice -n 10 ionice -c3`.

### Baseline dish gate

Raw output captured to **`/mnt/ssd/tmp/w275-scribe-dishgate.txt`**. Exit code **0**.

```text
=== GATE RUN 2026-10-08T09:07:00 ===
CMD: bash /home/or/vulcan4/.auto/verify-dish.sh

=== VULCAN 4 dish gate — 2026-10-08 09:07 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w274irq-capture.png
      GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference - the screen did not change.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w274irq-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w274irq.log
      halt=guest_blocked
INFO  missing-function hits in that log: 0
0
=== RESULT: MECHANICAL CLAIMS HOLD ===

=== GATE EXIT CODE: 0 ===
```

**Known gate confound (do not misquote).** `verify-dish.sh` §5 reads the newest `$BUILD/run/*.log`
(here `boot_w274irq.log`) — that is a **W274-era capture, not this baseline's own boot**. Its
`halt=guest_blocked` and the byte-identical `w274irq-capture.png` are the *existing* W274 product,
quoted by the gate as-is. So `RESULT: MECHANICAL CLAIMS HOLD` means: suite green, captain authorship,
tree clean — **not** "W275 booted to this halt". A W275 banner must be measured by a fresh W275 run,
not this line.

## SEAT FINDINGS (appended as they land — 2026-10-08)

### T1 — builder — Angle A (runtime) — **ANSWER: NO DROP. THE DMAC `Interrupt` INVOCATION IS ENQUEUED 35× AND RUN 35× — the W274 hypothesis is FALSIFIED**

**There is no function that drains INTC but not DMAC, because there is no drain path split at all.**
Both INTC and DMAC interrupt invocations enter the SAME queue (`m_pendingInvocations`) through the SAME
`EeScheduler::queueInvocation` and are serviced by the SAME `EeScheduler::serviceInvocations` loop.

Probe: env-gated `VULCAN4_W275_INV=1`, 6 print sites in `EeScheduler.cpp` (+135 lines) and 1 in
`ps2_runtime.cpp` (+10 lines). Unset = byte-identical behaviour (all sites behind `w275InvTrace()`).
Boot log: **`/mnt/ssd/vulcan4-build/run/boot_w275inv.log`** (17,802,502 B, 219,482 lines).

**Deliverable 1 — enqueue vs service counts (raw, from the log).**

| count | value |
|---|---|
| `[w275:enq]` (all) | **93** — every one `kind=Interrupt` |
| `[w275:svc-pop]` (all) | **93** — every one `kind=Interrupt` |
| `[w275:svc-run] kind=Interrupt` | **94** (93 fresh + 1 resume, see note) |

**Deliverable 2 — the histograms MATCH on the handler address.** `enq` pc histogram vs
`svc-run kind=Interrupt` pc histogram:

| handler pc | enqueued | run |
|---|---|---|
| `0x100d838` | 50 | 50 |
| **`0x100dae0` (FUN_0100DAE0 — the DMAC handler)** | **35** | **35** |
| `0x1029388` | 8 | 8 |

`0x100dae0` is entered **35 times**, at `depth=1`, by the same loop that enters the INTC handlers.
`svc-run`/`svc-pop` n-sequences and pc sequences line up with `enq` from n=1 (`0x100dae0`) through n=81
(`0x100d838`). Nothing is left behind.

**Deliverable 3 — the named drain path (this is the whole chain, and it is the SAME for INTC and DMAC):**

1. `ps2_memory.cpp:1992` `queueCompletedDmacCause()` →
2. `ps2_runtime.cpp:5160` `drainCompletedDmacHandlers()` — measured 36 drains (25×cause1, 9×cause2, 1×cause0, 1 blank) →
3. `Interrupt.cpp:68` `dispatchDmacHandlersForCause()` →
4. `EeScheduler.cpp:1697` `dispatchIrq(dmac=1, cause)` — measured 36 `dmac=1` dispatches (vs 108 `dmac=0` INTC) →
5. `EeScheduler.cpp:1222` `queueInvocation()` — **prints `[w275:enq]`** →
6. `EeScheduler.cpp:1369` `serviceInvocations` `pop_front` — **prints `[w275:svc-pop]`** →
7. `EeScheduler.cpp:1565` invocation run site, immediately before `function(m_rdram, &context, &m_runtime);` — **prints `[w275:svc-run]`**.

**Corroboration at halt:** both threads report `invocations=0`, `pending_now=0`,
`blocked_on_servicing=0` — there is no undrained invocation parked anywhere.

**Why W274 mis-read it — two artifacts, both now corrected:**

- `inv_by_kind=[intr=94,dmac=0,...]`: the harness label `dmac=` reads
  `invocationsRunByKind[1]`, and slot 1 of `enum class GuestInvocationKind`
  (`ee_scheduler.h:93`) is **`Alarm`, not DMAC** — there is no DMAC slot; DMAC invocations are
  `Interrupt` (slot 0). `dmac=0` is a **vacuous zero** that has never counted anything DMAC.
  The real DMAC number is inside `intr=94`.
- `target_pc=0x100dae0` absent: that counter is only incremented in `dispatchGuestBranch`
  (`ps2_runtime.cpp:4166`). Interrupt entries go through the scheduler's `function()` call, not a guest
  branch, so its absence never meant "not entered".

**The lone anomaly, stated honestly (not a drop):** one extra Interrupt run, `n=22309
kind=Interrupt pc=0x100d8f8 tid=1 depth=1 step=292`, immediately after BaseFrame steps on tid2 —
a re-entry/resume of an in-progress invocation (`0x100d838 + 0xC0`), so 94 runs from 93 pops.
It leaves `pending=0` behind it. `svc-stuck` fired twice (n=1,2), both a transient nesting wait that
resolved in the same `serviceInvocations` call.

**Also measured (out of scope for the drop question, lead for a future fix):** our runtime builds the
handler context as `a0=cause`, `a1=handler.argument` (=0x4), `gp=handler.gp`, `sp=0`, `ra=0`, and
**never sets `a2` or `v1`**. Hardware (T2) enters at `0x100dae0` with `a0=0`, `a1=0x1FFFBC0`,
`a2=0x81FC0`, `sp=0x81FC0`, `ra=0x81FEC`, `v1=0x100DAE0`. So the handler *is* entered but with a
different argument/register context than the kernel trampoline supplies. Not fixed here.

**Law-8 capture check (raw):**
```
135	0	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp      <- pre-existing 0x5b fix, patch already committed (d3fc63b)
10	0	ps2xRuntime/src/lib/ps2_runtime.cpp
-rw-rw-r-- 1 or or 10763 Oct  8 09:13 ../patches/ps2recomp-linux-w275-invocation-queue-probes.patch
EeScheduler.cpp numstat_added=135 patch_added=135
ps2_runtime.cpp numstat_added=10  patch_added=10
```
Patch written + per-file verified. **NOT committed** (task said "Do NOT commit"); the two probe files are
captured so the work does not live only in the working tree.

**Wall unchanged by this finding:** `halt=guest_blocked`, tid1 parked on `sema#7` `pc=0x0101f468`,
tid2 sleeping `pc=0x0101f348`. The named cause (unserviced DMAC invocation) is wrong even though the
wall is real. Next divergence hunt must move off the invocation queue.

### T2 — measurer — Angle B (oracle) — **ANSWER: YES, hardware ENTERS `FUN_0100DAE0`**

Oracle: PCSX2 DebugServer, GT4 SCUS-97328 (USA) v2.00, fresh `pcsx2-qt -debugger -fastboot`, breakpoints
armed **before** the loader ran (`[UI] StartPaused=true`). Reproduced **twice**, identical cycle counts.

**Deliverable 1 — does the breakpoint fire? YES.**

| BP | meaning | FIRED | Cycles | Key regs at the hit |
|---|---|---|---|---|
| `0x0100ACA8` | registration fn entry | ✅ | 115,933,619 | `ra=0x0100A4A8` |
| `0x0100DAE0` | **handler entry** | ✅ | **116,434,189** | `PC=0x0100DAE0`, `ra=0x00081FEC`, `sp=0x00081FC0`, **`a0=0x00000000`** (channel 0), `a1=0x01FFFBC0`, `a2=0x00081FC0`, `gp=0x01049770`, `v1=0x0100DAE0` |
| `0x0100DCF0` | busy-byte clear site | ✅ | 116,434,220 | `a0=0x70002060` → `sb zero,0xD(a0)` writes **`0x7000206D`** |

The clear site hits **+31 cycles** after handler entry. Busy byte at `0x7000206D` reads **`0x01` before**
the single step and **`0x00` after** it — the handler *is* what clears it. The chain `0x100dae0` →
`0x100dcf0` → clear is real and complete on hardware.

**State at the handler entry (`0x100dae0`):** busy byte `0x7000206D = 0x01` (still set); DMAC ch0
`0x10008000..0x1000803C` (CHCR/MADR/QWC/TADR) = **all zero** (transfer already done, channel off);
`D_STAT 0x1000E010 = 0`, `I_STAT 0x1000F000 = 0`, `I_MASK 0x1000F010 = 0` — the kernel has already
**ACKed** the interrupt before the handler runs (so "cause" is 0 by the time we observe it). Handler-id
table `0x700020F0..FC` = `06 03 04 05`. `at = 0xB000E010` (kernel had just read D_STAT).

**Deliverable 2 — the delivery path. The caller is KERNEL code, not game code.**

`ra = 0x00081FEC` is the return address of a `jalr` at `0x00081FE4`. Disassembly of the caller:

```text
0x00081fe0:  0x3c1d0008    lui   sp, 0x0008
0x00081fe4:  0x0060f809    jalr  ->v1          ; v1 = 0x0100DAE0 (the registered handler)
0x00081fe8:  0x27bd1fc0    addiu sp, 0x1FC0    ; delay slot -> sp = 0x00081FC0
0x00081fec:  0x2403fffb    li    v1, -0x5      ; <-- here is ra
0x00081ff0:  0x0000000c    syscall             ; ExitHandler / ReturnFromException
```

with a twin trampoline at `0x00082000` (`li v1,-8; syscall`). This is the classic **EE-kernel exception
trampoline**: set the kernel stack, `jalr` the registered handler, then `syscall` back to the kernel.
So on hardware the DMAC handler is entered by the **EE kernel's own interrupt dispatch** — a `jalr` from
kernel-installed code at `0x00081FE0` (inside the `0x80000` kernel region) — **not** by any game-code
call, and **not** by a guest-visible `dispatchIrq` call. Path, end to end, measured:
`AddDmacHandler(0,0x0100DAE0,0)` (called from `0x0100A4A8`→`0x0100ACA8`, via `0x0101F130`) → VIF0 ch0
DMA completes → kernel ACKs D_STAT/INTC → **kernel trampoline `0x00081FE0` JALRs the handler** →
`0x0100DAE0` → clears `0x7000206D` at `0x0100DCF0` → returns → `li v1,-5; syscall`.

**Deliverable 3 — is `0x0100DAE0` entered by hardware for this transfer? YES** (cycles 116,434,189,
`a0=0` = channel 0, same transfer W274 measured). Two independent fresh boots, same cycle.

**Implication for our recomp (state plainly).** `0x00081FE0` is **below the recompiled image**
(`0x00100000+`), so **nothing we recompile ever calls `0x0100DAE0`**. The JALR that enters it lives in
BIOS/kernel-installed trampoline code. Therefore our `dispatchIrq` "matching" the handler
(`cause=0 enabled=1 hasFn=1`) is **necessary but not sufficient**: the runtime must itself reproduce the
kernel trampoline — on DMAC completion, *call* the registered handler the way the kernel does (with the
kernel stack frame), then apply its effect. If our servicing only enqueues a `GuestInvocationKind::Interrupt`
that the scheduler never turns into the call, `target_pc` stays 0 — exactly the W274 symptom.

**Instrument gotchas (both verified this session):**

1. **`pcsx2_get_backtrace` SIGSEGVs PCSX2** — `Unhandled SIGSEGV … DebugInterface.cpp:682 …
   handleCommand DebugServer.cpp:787 … Aborting application`. Do **not** call it; it kills the emulator.
2. **Post-`ExecPS2` instances are useless** — `0x01000000..` and `0x0100DAE0` read all zeros once
   `ExecPS2` has run, and no save state exists. A **fresh boot with the BPs armed before the loader runs**
   is mandatory. Guaranteed by `[UI] StartPaused=true` (`QtHost.cpp:238`); restored to `false` after.
3. `PC=0x01000008` mid-`ExecPS2`-load is a transient pause, not a breakpoint hit; a halt is confirmed by
   a frozen cycle counter (cycles unchanged across ≥4 s) with the PC pinned at the BP address.

### T3 — architect — Angle C (static, competes with T1) — OPEN
Read `EeScheduler::dispatchIrq` (`EeScheduler.cpp:1697`) → enqueue → the drain/service loop; name the
code path that services INTC but not DMAC, independently of T1. Owner: architect.

### T4 — reviewer — falsify — DONE (W274 claim FALSIFIED)

**Verdict: the W274 claim "DMAC interrupt invocation is queued but never serviced" is FALSIFIED.**
The handler `FUN_0100DAE0` was **entered**. Six of the claim's own cited evidences are wrong or vacuous:

1. **`dmac=0` is a mislabel.** `inv_by_kind` slot `dmac` = `invocationsRunByKind[1]` =
   `GuestInvocationKind::Alarm` (`ee_scheduler.h:93-103`; harness `vulcan4_harness.cpp:3659`). DMAC
   invocations are `kind=Interrupt` = slot 0 (`intr`), so a serviced DMAC handler increments `intr`,
   never `dmac`. The counter cannot see DMAC at all — it is a vacuous zero, not a DMAC-specific wall.
2. **`0 target_pc=0x100dae0` is a proxy artifact.** `target_pc=` prints only in `[Yield]`
   (`ps2_runtime.cpp:4112`) and `[Dispatch]` (`ps2_runtime.cpp:4166`), each capped at 400 lines
   (`kMaxYieldTrace`/`kMaxCallTrace`), on the guest *branch/call* path. The interrupt-invocation service
   path (`EeScheduler.cpp:1466` `function(m_rdram,&context,&m_runtime)`) prints neither. So 0 occurrences
   does **not** mean "handler never entered" — it means the handler was never reached by a *guest branch*.
3. **The invocation WAS queued.** `[w274:irq] dmac cause=0 handlers=3` (NOT `MASKED`) proves the mask gate
   (`EeScheduler.cpp:1747`) passed for cause 0; the handler `{cause=0, enabled=1, h=0x100dae0, hasFn=1}`
   matches the loop at `EeScheduler.cpp:1781`, so `queueInvocation` (`:1870`) was reached.
4. **The invocation WAS serviced.** `pending_now=0` (queue empty at halt) + `pending_hi=1` (queue depth
   never exceeded 1) + exactly **one** cause-0 dispatch (board 08:44 "queued/drain cause 0 ×1"). The only
   drains of `m_pendingInvocations` are `serviceInvocations:1417`, `run():247/322`, and `reset():151`
   (boot-init only; the `ExecPS2`-relaunch reset at `vulcan4_harness.cpp:2751` never fires, `ExecPS2=0`).
   A queued-but-never-serviced invocation would leave `pending_now≥1` at halt → **contradiction** → it
   was popped = serviced.
5. **`missing_functions=0`** (boot log 66209) → once entered, the handler did not die on a missing function.
6. **`irq_attach=0`, `irq_runsite=0`, `irq_done=0` are run()-only counters** (`EeScheduler.cpp:296/321/369`);
   the harness drives via `serviceInvocations`, which never touches them → vacuous zeros, not evidence.

**The real wall is downstream.** The handler runs, but its effect (clear busy byte `0x7000206D` / bit 8 of
`0x10008000`, unblock `tid2`, `SignalSema(7)`) never materializes. Angle A's `[w275:svc-run]` trace should
show `pc=0x100dae0` — if it does, that **confirms** (not contradicts) this finding, and the fix target
moves from "invocation dropped" to "handler runs but effect missing / handler body diverges".

*Falsified ≥2 findings:* (a) "dmac=0 is the wall", (b) "queued but never serviced", (c) "never entered
(0 target_pc)", (d) "irq_attach/irq_done=0 as evidence".

**Angle A (T1, builder) falsified — AGREED.** Their runtime trace (`VULCAN4_W275_INV=1`,
`boot_w275inv.log`) landed and **independently confirms** my kill: `0x100dae0` enqueued 35× and RUN 35×,
no drop. Reconciliation with my counters: 36 DMAC drains → 35 enqueues + 1, and that 1 is the first
drain, which had `handlers=0` (`boot_w274irq.log:504` "[w274:irq] dmac cause=2 handlers=0", before
`AddDmacHandler`). Their corrected artifacts are verbatim mine (`dmac=`=Alarm slot 1 of
`ee_scheduler.h:93`; `target_pc` = branch-path counter `ps2_runtime.cpp:4166`). Their register-divergence
lead is *accurate from source* — `dispatchIrq` sets `a0=cause, a1=handler.argument, gp, sp=0, ra=0`
(`EeScheduler.cpp:1797-1871`) and never sets `a2`(r6)/`v1`(r3), while hardware enters via the COP0/KSEG1
exception frame (`sp=0x81FC0, ra=0x81FEC, a1=0x1FFFBC0`) — but it stays a **lead** until someone shows
`FUN_0100DAE0` actually reads `a1`/`sp` and dereferences it. Both required angles now agree: **serviced,
not dropped**; the wall is downstream in the handler's effect. T3 (architect) still to falsify.

## RULE THAT GATES A FIX

No fix until **≥2 independent angles agree on the same drop point.** Fix lives in
`ps2xRuntime/src/lib/` (EeScheduler servicing), never in `runner/*.cpp`, never in a PS2Recomp `.h`.
Every probe OFF by default (`0`/unset == today's behaviour, byte for byte). Recompiler/translator
changes → `tools/patches/`.

## NEXT ACTION

T1/T3 must independently name the exact function where the DMAC `Interrupt` invocation dies (enqueue
serviced, or drain that skips it); T2 confirms against hardware that the handler is entered. When two
angles agree, write the fix in `EeScheduler` servicing, rebuild (`-j4`, `nice -n 10 ionice -c3`,
`TMPDIR=/mnt/ssd/tmp`, `VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251`), and measure
`ExecPS2` (>0) + entry PC + `frames_presented` before/after.

## STOP RULES

`ExecPS2` fires and the engine is entered → **STOP + report entry PC.** A fresh capture ≠ disclaimer →
**STOP** (milestone). Otherwise stop at a **new named limitation**, written down, record captured.

### T3 — architect — Angle C (static) — ANSWER: the hypothesis is FALSE; the DMAC invocation IS serviced (35x)

Static read of `EeScheduler.cpp` + cross-check against the builder's own `VULCAN4_W275_INV` trace
(`boot_w275inv.log`, `/mnt/ssd/vulcan4-build/run/`) — the two agree, and both refute the W275 premise.

**1. Enqueue function.** `EeScheduler::dispatchIrq` (EeScheduler.cpp:1797) builds the invocation
(:1929-2005: `pc=handler.handler` :1931, `a0=cause` :1932, `a1=handler.argument` :1933, `gp=handler.gp`
:1969, `sp=0` :1986, `ra=0` :1987) and calls `queueInvocation` (:2005 → :1250). The DMAC dispatch is
NOT rejected by the mask gate — `[w275:dispatch] n=2 dmac=1 cause=0 mask=0xffffffff gate=pass`,
`[w275:dispatch-match] ... matching=1 [en h=0x100dae0 arg=0x4 hasFn=1]`. So the DMAC invocation IS
enqueued — **35 times** (`[w275:enq] pc=0x100dae0` ×35).

**2. Drain/run function.** `EeScheduler::serviceInvocations` (EeScheduler.cpp:1299) drains the queue at
:1414 (pop + attach) and runs it at :1566 (`function(m_rdram, &context, &m_runtime)`). The DMAC
invocation is drained and RUN — **35 times** (`[w275:svc-pop] pc=0x100dae0` ×35,
`[w275:svc-run kind=Interrupt pc=0x100dae0]` ×35). There is **no kind filter, no count gate, no
`if(intc)` vs `if(dmac)` branch** in the servicing path: `dispatchIrq` sets `kind=Interrupt` identically
for `dmac=true` and `dmac=false`, and `m_pendingInvocations` is a single FIFO drained uniformly.

**3. Why "INTC drains but DMAC does not" is a false premise — the W274 numbers were measurement
artifacts.** (a) `inv_by_kind=[...,dmac=0]` prints `invocationsRunByKind[1]` = the **Alarm** slot, not
DMAC; the DMAC handler is `kind=Interrupt` (slot 0), so it is inside the `intr=94`. (b)
"0 `target_pc=0x100dae0`" is the harness **arrival** histogram (`resolveFunction` @
vulcan4_harness.cpp:2331), which structurally cannot see `serviceInvocations`' direct `function()` call
— documented verbatim at vulcan4_harness.cpp:3352-3354 ("the population the arrival histogram
structurally cannot see -- serviceInvocations() calls the generated function directly").

**4. The real drop (downstream).** The handler runs but its effect fails. Boot report:
`MMIO 0x7000206d accesses=5` across the whole run (the ch0 busy byte the cause-0 entry of the handler
must clear; the handler is entered 35x across causes 0/1/2, yet the byte ends the run un-cleared and
tid2 never wakes), `tid2:wait=sleep#0`, `tid1:wait=sema#7`, `ExecPS2` still 0. T2's oracle shows hardware
enters the handler via the **EE-kernel trampoline 0x00081FE0** (`a0=0` channel, `a1=0x01FFFBC0`) — code
below the recompiled image, so nothing we recompile ever calls `0x100dae0`; our `dispatchIrq` is the
only entry. Our invocation sets `a0=cause`, `a1=arg=0x4`, and **leaves `a2`/`a3`/t-regs zeroed** — but
FUN_0100DAE0's first real inputs are `sll v0,a3,2; lui v1,0x0103; addu v1,v1,v0; lw v1,0x2DF8(v1)`
(it indexes a channel table by `a3`, then gates on the DMAC state word at `0x70002050`).

**5. Fix site (hypothesis, NOT applied — for the builder+reviewer to confirm).** The drop is NOT in
`serviceInvocations`. Two candidates, both in `ps2xRuntime/src/lib/`:
- **`EeScheduler::dispatchIrq` invocation construction (EeScheduler.cpp:1932-1933, 1969)** — the
  register contract for DMAC handlers is wrong/incomplete: the handler reads `a3` (channel index) and
  gp-relative tables, but we set only `a0=cause, a1=arg, gp` and zero the rest.
- **`Interrupt.cpp` `addHandler` (:26-40) → `addIrqHandler` (:1720)** — `argument` is read from `a3`
  (r7), while the PS2 kernel's `AddDmacHandler(channel, handler, common)` third argument is `a2` (r6);
  the captured `arg=0x4` is a stray register, not the guest's `0`.

Stop condition unchanged: `ExecPS2` fires → STOP. No fix is written until T1 (runtime) and T3 (static)
name the same drop point — they now do: **not the servicing path**; the handler runs and its
busy-byte clear does not take effect.

---

## T1-followup — DECISIVE MEASUREMENT: the busy-byte trace (builder, 2026-10-08)

Probe: `VULCAN4_W275_BUSY=1`, OFF by default. Two TUs: `ps2_runtime.cpp` (pc-bearing, via
`ctx->pc` in `Load8/Store8/Load32/Store32`) and `ps2_memory.cpp` (pc-less backstop + the sites the
runtime cannot reach: `write128` DQAQ, SPR_TO memcpy, `readIORegister`/`writeIORegister`). A
`thread_local g_w275GuestBusyHandled` suppresses the double-log. Reads are throttled (value change /
first 8 / n%1e6); writes are never throttled, so the write list below is COMPLETE.

Patch: `tools/patches/ps2recomp-linux-w275-busybyte-probe.patch` (per-file capture check: EeScheduler
135/135, System 98/98, ps2_memory 91/91, ps2_runtime 104/104 — all OK).

Log: `/mnt/ssd/vulcan4-build/run/boot_w275busy.log` (6,381,697 bytes, 424 probe lines).
Boot: tag=w275busy entries=2000000 budget=15s. Halted `guest_blocked` at elapsed_ms=6436.

**Non-perturbation check.** W275busy vs the W274 probe run, byte for byte on every shape number:
`functions_entered=723`, `true_guest_entries=1694274`, `halt=guest_blocked`, `intr_queued=93`,
`intr_run=97`, `intr_run_by_kind=94`, `gs_packets=130`, `checkpoint_serviced=35`. Only
`frames_presented` differs (358 vs 479, wall-clock). The probe changes nothing.

### 1. Full write/read trace of 0x7000206D — COMPLETE, 5 events

| # | op | value | guest pc | owning function | instruction |
|---|----|-------|----------|-----------------|-------------|
| 505 | WRITE8 | 0x00 | 0x100ac78 | (ch0 setup) | init |
| 955 | READ8  | 0x00 | 0x100aed0 | `sub_0100AE78` | `lbu $v1,0x0($s1)` — the poll, n=1 |
| 956 | WRITE8 | **0x01** | **0x100debc** | `sub_0100DE58` | `sb $a1,0xD($a0)`, a0=0x70002060 — **SET** |
| 961 | WRITE8 | **0x00** | **0x100dcf0** | `sub_0100DAE0` | `sb $zero,0xD($a0)` — **CLEAR** |
| 962 | READ8  | 0x00 | 0x100aed0 | `sub_0100AE78` | the poll, n=2 — reads 0, exits |

Exactly two reads total (the throttle would have printed n=3..8 had the poll looped). **Nothing
re-sets it.** Writes are unthrottled, so there is no hidden set.

### 2. Does `WRITE8 0 @ pc 0x100dcf0` appear? — **YES.** The handler reached the clear.

The clear LANDS. Candidate (a) wrong-branch, (b) re-set, and (c) split-read are ALL FALSIFIED for VIF0.

The CHCR value the handler read at 0x100db10 was **0x70000045**: STR (bit 8, 0x100) CLEAR. The handler
therefore takes the completion branch and clears the byte. (Raw register before the read: the kick
wrote 0x10000145; `readIORegister` masks `& ~0x100u` and writes the masked value back —
`ps2_memory.cpp:2576-2579`. The TAG field 0x7000 is unexplained by anything this probe watched; noted,
not chased.)

### 3. CHCR trace around the VIF0 completion (channel 0 = VIF0, `ps2_memory.cpp:2034`)

```
957  R32 0x10008000 = 0x00000040  pc=0x100dedc   pre-kick read
958  W32 0x10008000 = 0x10000145  pc=0x100dee8   VIF0 kick (DIR|MOD=1|bit6|STR|TIE)
960  R32 0x10008000 = 0x70000045  pc=0x100db10   handler sees STR clear -> COMPLETE
961  W8  0x7000206d = 0x00        pc=0x100dcf0   clear
```
VIF0 is kicked **exactly once** in the whole boot. VIF1 (0x10009000) is kicked 26x and GIF
(0x1000A000) 33x; their handler reads (pc=0x100db10) return STR-clear every time too. So the DMAC
completion path is not special-cased broken for VIF0 — VIF0 simply runs once and then the guest blocks.

### What this kills, and what is left

The W274 mission hypothesis ("handler runs but its effect never lands") is **FALSIFIED**: the busy byte
completes 0 -> 1 -> 0, the poll at 0x100aed0 reads the cleared 0, and `sub_0100AE78` exits. The wall is
DOWNSTREAM of the byte. The surviving number in the boot report is `pending_hi=1` with
`irq_attach=0 irq_runsite=0 irq_done=0 thread_attached=0` — a high-priority interrupt is pending and
never attached/delivered; tid1 sits on `sema#7`, tid2 on `sleep#0`, `ExecPS2` 0.

> **SUPERSEDED (W275 T1-follow-up #2, see Segment B below).** The stride claim below is WRONG.
> The correct formula, read straight off the guest at 0x100dcf0 (`sb zero,13(a0)` with
> `a0 = a2 + 16 + 12*ch`, `a2 = 0x70002050`), is **byte = 0x7000206D + 12·channel** → VIF0
> 0x7000206D, VIF1 0x70002079, **GIF 0x70002085**, ch3 0x70002091. The 0x7000208D / stride-0x10
> text below is retained only as the record of a retracted claim; do not build on it.

~~Two corrections to the mission brief's address map: the stride is 0x10, not 12 — the byte for ch2 is
0x7000208D, not 0x70002089; and 0x7000207C / 0x70002088 are NOT busy flags — the guest uses them as
saved-sp slots (`sw $sp,0x0($v1)` at 0x100af18 stores 0x1ffcda0 / 0x1045970, pointers). The only byte
flag is 0x7000206D for channel 0; byte accesses at 0x7000207D / 0x70002089 are ZERO in the whole boot.~~

### Law-8 capture check — RAW

```
$ cd tools/PS2Recomp && git diff --numstat
135	0	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
91	2	ps2xRuntime/src/lib/ps2_memory.cpp
104	2	ps2xRuntime/src/lib/ps2_runtime.cpp

$ per-file added lines vs tools/patches/ps2recomp-linux-w275-busybyte-probe.patch
EeScheduler.cpp  live=+135 patch=+135
System.cpp       live=+98  patch=+98
ps2_memory.cpp   live=+91  patch=+91
ps2_runtime.cpp  live=+104 patch=+104
```

---

## Segment B — W275 T1 follow-up #2: the GIF chain walker, named drop

*2026-10-08 · builder · probe `VULCAN4_W275_GIFTAG=1` (OFF by default) + `VULCAN4_W275_BUSY=1`.*

**Decisive run:** `/mnt/ssd/vulcan4-build/run/boot_w275tag4.log` (6,461,080 bytes, EXIT=0,
`frames_presented=0` — "NO CAPTURE was taken, no 640x448 window ever appeared on :105").
Command:
```
VULCAN4_W275_GIFTAG=1 VULCAN4_W275_BUSY=1 VULCAN4_DISPLAY=:105 \
  bash tools/harness/run_capture.sh <name> 2000000 15 30
```

### B.1 The named drop — candidate **(a) CONFIRMED**: an empty, legal, zero-QWC source chain

The GIF source-chain kick the boot repeats is a **valid chain that transfers zero quadwords and
terminates with `tag_end`**. Raw walker output (25 WALKs in the run, all byte-identical):

```
[w275:giftag] KICK ch=GIF addr=0x1000A000 value=0x10000185 dctrl=0x00000001 dmacEnabled=1 gsVRAM=1
[w275:giftag] WALK ch=GIF chcr=0x10000185 mode=1 tie=1 asp=0 tadr=0x01045A90 madr=0x0106E6D0 qwc=0 \
                       asr0=0x00000000 asr1=0x00000000
[w275:giftag]   tag#0 addr=0x01045A90 raw=00000020805E04010000000000000000 id=2 qwc=0 dw=17063552 irq=0 end=0 payload=0 dataAddr=0x01045AA0 nextTagAddr=0x01045E80 chainBuf=0
[w275:giftag]   tag#1 addr=0x01045E80 raw=00000070000000000000000000000000 id=7 qwc=0 dw=0 irq=0 end=1 payload=0 dataAddr=0x01045E90 nextTagAddr=0x01045E80 chainBuf=0
[w275:giftag] END ch=GIF tagsProcessed=2 chainBuf=0 tagAddr=0x01045E80 asr0=0x00000000 asr1=0x00000000 asp=0 chcrFinal=0x70000185 enqueue=NO(chainBuf empty)
[w275:giftag] PROCESS ch=GIF auto=1 cb=0 arbiter=1 pendingGif=0 pendingVif1=0 pendingVif0=0
[w275:giftag]   SERVICED gif=0 vif1=0 vif0=0
[w275:giftag]   COMPLETE hadGif=0 hadVif0=0 hadVif1=0
```

**Tag decode (memory order → DMAtag qword).** The 32-hex-char `raw=` dump is the 16 tag bytes **in
memory order**; the DMAtag is the **first 8 bytes read little-endian**. Decode (QWC = bits 0-15,
id = bits 28-31, ADDR = bits 32-63):

| tag | address | raw (memory order) | DMAtag qword | id | QWC | ADDR |
|---|---|---|---|---|---|---|
| #0 | 0x01045A90 | `00000020805E04010000000000000000` | `0x01045E8020000000` | **2 `next`** | **0** | 0x01045E80 |
| #1 | 0x01045E80 | `00000070000000000000000000000000` | `0x0000000070000000` | **7 `end`** | **0** | — |

Cross-check: tag#0's `dw=17063552` = `0x01045E80`, which the walker's own parse puts equal to tag#1's
address — consistent only with tag#0's first 8 bytes being little-endian `01 04 5E 80 20 00 00 00`.

**Why this is a *completed* transfer, per the mandated corpus** (`resources/09-ps2tek.md`, not
intuition):
- `id=2 next` — "MADR = TADR+16; TADR = DMAtag.ADDR" (ps2tek.md:1784) → next tag at 0x01045E80. ✔ walked.
- `id=7 end` — sets `tag_end` (ps2tek.md:1782).
- **"When tag_end=true, the transfer ends after QWC has been transferred."** (ps2tek.md:1725.)
  QWC here is 0, so **zero quadwords is the whole transfer — and it is complete.**

On real hardware this clears CHCR.STR, sets D_STAT channel 2, and fires the TIE interrupt. The
runtime instead gates the entire GIF enqueue on a non-empty chain buffer:

- `enqueueTransfer` early-returns on `qwCount == 0` (ps2_memory.cpp:1513-1514).
- `if (mode == 0 && qwc > 0) enqueueTransfer(...)` (1531) — source-chain (mode 1) is not this path.
- **`if (!chainBuf.empty())` (ps2_memory.cpp:1769)** — the gate that drops the transfer.

So `chainBuf` stays size 0, `m_pendingGifTransfers` is never pushed, `hadGif=0` (~1924), and the
completion stanza (2163-2168: `raiseDStatChannel(2u); queueCompletedDmacCause(2u);
m_ioRegisters[GIF_CHANNEL+0x00] &= ~0x100u; m_ioRegisters[GIF_CHANNEL+0x20] = 0;`) never runs.
Measured end state: `chcrFinal=0x70000185` — **STR (bit 0x100) is still set after the walk**, which is
exactly the "transfer never completed" signature.

**Census over the whole run (not a sample):** KICK total 61; GIF CHCR write `0x10000185` = 25;
WALK 25; `END enqueue=NO(chainBuf empty)` = 25; **enqueue=YES = 0**; COMPLETE `hadGif=1` = 9 (those are
the *other* GIF kicks, CHCR `0x181` with real payloads), `hadGif=0` = 53. **25 of 25 source-chain GIF
kicks are this same empty chain; 0 of 25 enqueue.**

Kicker pc: the empty-chain kick is `pc=0x100dee8` inside `sub_0100DE58` (23 of 25 CHCR writes to
0x1000A000); the payload kicks come from `pc=0x100b0e4` (8×, value 0x181) and `pc=0x100ac54` (1×,
0x80). Both tags sit in segment-2 `.bss` (file-backed vaddr ends 0x01041724; 0x01045A90 and
0x01045E80 are past it), so these bytes are **runtime-written** — this captured dump is the only
ground truth, the ELF cannot supply them offline.

### B.2 Candidates (b) and (c) — both REFUTED by the same dump

- **(b) ID parse / `mode==1` rejection: REFUTED.** The walker decoded `next`→ADDR and terminated on
  `end` exactly as ps2tek specifies; the id parse is correct, it is the *payload gate after* the parse
  that drops it.
- **(c) DIR=1 GIF path not enqueued: REFUTED.** The DIR=1 GIF path is entered and walked —
  `PROCESS ch=GIF auto=1 cb=0 arbiter=1` appears for every one of the 25 kicks.

The drop is therefore **candidate (a): empty `chainBuf` because both tags carry QWC=0** — and a
zero-QWC `end`-terminated chain is legal and complete, so the runtime must not treat "empty payload"
as "nothing happened".

### B.3 The coordinator's premise is FALSIFIED by measurement

The handed premise — *"tid2 LIVE-LOCKS on a GIF chain transfer … the ch2 busy byte never clears"* — is
wrong on both halves:

1. **The GIF busy byte clears.** Measured: `0x70002085` (GIF; formula corrected in B.4) is **set 25×**
   (pc=0x100debc) and **cleared 32×** — 24× at guest `pc=0x100af5c` and 8× at `pc=0x100dcf0` (the DMAC
   handler). It is not stuck.
2. **The loop is bounded, not a live-lock.** Guest `0x100af50`/`0x100afa0`/`0x100afa8` is a bounded
   retry: `bgez $s3` guard with `addiu $s3,$s3,-1` in the taken delay slot. A live-lock cannot decrement
   and exit; this does.
3. **There is no `$a0 == 0` untimed park.** 15,576 `SleepThread` calls, **zero with us=0**:
   12,652× us=1 (carrying `s0=0x70002050`), 2,924× us=8 (`s0=0x70002085, ra=0x100afa8`). Every park
   is timed.

So the boot does **not** halt on a stuck GIF busy byte. The GIF busy byte is one symptom; the measured
halt is B.5.

### B.4 Busy-byte address map — CORRECTED (supersedes the top-of-file note)

Read straight off the guest disassembly of the DMAC serve at `0x100dcf0`:
```
100dce4:  00021080  sll v0,v0,0x2
100dce8:  24420010  addiu v0,v0,16
100dcec:  00c22021  addu a0,a2,v0
100dcf0:  a080000d  sb zero,13(a0)     ; a2 = 0x70002050 → byte = 0x70002050 + 16 + 12*ch + 13
```
`a2 = 0x70002050` (confirmed by the run's own `flag@0x70002050=2`), so the **stride is 0xC, not 0x10**,
and the byte is **`0x7000206D + 12*channel`**:

| channel | busy byte |
|---|---|
| ch0 VIF0 | 0x7000206D |
| ch1 VIF1 | 0x70002079 |
| **ch2 GIF** | **0x70002085** |
| ch3 | 0x70002091 |

The per-channel completion callbacks run at `0x100dcf8-0x100dd18` (`lw s0,0(v1)` then
`jal 0x10202e8`), i.e. right after the clear.

### B.5 The measured halt state (what actually stops the boot)

```
halt=guest_blocked
thread_state=tid1:status=2:wait=sema#7:woken=0:pc=0x0101f468:ra=0x01000de8:invocations=0
             tid2:status=2:wait=sleep#0:woken=0:pc=0x0101f348:ra=0x0100afa8:invocations=0
SEM id=7 count=0 waiters=1   (SEM 1..6 count=1 waiters=0)
elapsed_ms=6497 ee_cycle=248373842 next_event_cycle=250680249
sleepCurrentCalls=15576 intr_run_by_kind=[intr=94,dmac=0,override=0,other=0]
```
`wait=sleep#0` is **waitReason=sleep with waitId 0**, NOT "microseconds 0" (harness
`vulcan4_harness.cpp:3197-3200`). tid2 is in a *timed* 8 µs sleep (ra=0x0100afa8, s0=0x70002085).

### B.6 Open leads — HYPOTHESES, not measurements (flagged as such)

1. **A scheduler gap may be what actually halts the boot.** `completeTimedSleeps()` runs only from
   `accountCycles()`, i.e. only when guest code executes; `EeScheduler::canDispatchGuest()`
   (EeScheduler.cpp:1629) returns false when every ready queue is empty and no invocation is pending.
   With `m_nextDeadlineCycle`/`processDueDeadlines()` handling only `m_deadlines` (VBlank/timers) and
   not timed sleeps, a timed sleeper whose deadline is nearer than the next scheduled event
   (tid2's wake ≈ `ee_cycle + 8*295`, well under the +2,306,407 cycle gap to `next_event_cycle`) has
   nothing to advance it. If so, the run is being stopped early and masking the true consequence of
   the empty-chain drop. **Not yet measured** — one env-gated print of the sleeper's `wakeCycle`
   versus `next_event_cycle` would settle it.
2. **Whether the GIF completion IRQ is what signals sema#7.** The DMAC handler walks per-channel
   callbacks at `0x100dcf8-0x100dd18`; there are 64 `jal SignalSema` sites in the ELF
   (`SignalSema` trampoline @0x101f440, `trampoline(n)=0x101f2f0+16*(n-45)`), one at 0x1000de8 —
   numerically adjacent to tid1's `ra=0x01000de8`. **Not conclusive.**

### B.7 Law-8 capture check — RAW (this segment's probe tree)

```
$ cd tools/PS2Recomp && git diff --numstat
164	0	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
202	3	ps2xRuntime/src/lib/ps2_memory.cpp
127	2	ps2xRuntime/src/lib/ps2_runtime.cpp

$ per-file added lines vs tools/patches/ps2recomp-linux-w275-t1b-giftag-empty-chain.patch
EeScheduler.cpp                   live+164   patch+164   PASS
System.cpp                        live+98    patch+98    PASS
ps2_memory.cpp                    live+202   patch+202   PASS
ps2_runtime.cpp                   live+127   patch+127   PASS
patch total added lines: 595   ( = 591 real + 4 '+++' header lines )
```
All four modified files are carried by the patch. `System.cpp`'s +98 is the already-captured 0x5b
GetEntryAddress fix (`ps2recomp-linux-w274-0x5b-getentryaddress.patch`); the other three are the
W275 probes.

### Mechanical gate — RAW (`bash .auto/verify-dish.sh`, 2026-10-08, clean tree)

```
=== VULCAN 4 dish gate — 2026-10-08 09:59 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275busy-capture.png
      GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference - the screen did not change.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275busy-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275tag4.log
      halt=guest_blocked
INFO  missing-function hits in that log: 0
=== RESULT: MECHANICAL CLAIMS HOLD ===
EXIT=0
```
The menu gate correctly reports the screen is still the disclaimer: this dish **names a boot blocker**,
it does not draw the menu (law 4). `missing-function hits: 0` — the halt is not a dispatch miss.

---

## Segment C — the zero-QWC chain completion fix (R1 gate attempt)

*2026-10-08 · applied fix, rebuilt, booted, measured. Probes OFF for the R1 run.*

**The fix** (ps2_memory.cpp, `tools/patches/ps2recomp-linux-w275-zeroqwc-chain-complete.patch`):
`bool chainWalkedToEnd = false;` set true on the `endChain` break; gate widened
`if (!chainBuf.empty())` → `if (!chainBuf.empty() || chainWalkedToEnd)`. A valid NEXT→END zero-QWC
chain now pushes an empty `PendingTransfer`, so `hadGif=1`, the completion stanza raises D_STAT ch2 +
`queueCompletedDmacCause(2)` and clears `GIF_CHANNEL+0x00` STR. Safe for the empty case: `pt.qwc = 0`
and empty `chainData` fall through both consume branches (ps2_memory.cpp:1967/1973) without touching
data.

**R1 result — ExecPS2 does NOT fire.** `boot_w275r1.log` (probes OFF, 6,338,289 bytes, 0 frames):

```
grep -ac EXECPS2 boot_w275r1.log -> 0
functions_entered=751  halt=guest_blocked  frames_presented=363  intr_run_by_kind=119
thread_state=tid1:status=2:wait=sema#7:...:pc=0x0101f468:ra=0x01000de8
             tid2:status=2:wait=sleep#0:...:pc=0x0101f348:ra=0x0100b680
ee_cycle=248393998  next_event_cycle=250680249  sleepCurrentCalls=16076
```

**But the fix DOES change the mechanism** — the target poll is passed. `boot_w275r1busy.log`
(`VULCAN4_W275_BUSY=1`), GIF busy byte 0x70002085:

| | before fix (boot_w275tag4) | after fix (boot_w275r1busy) |
|---|---|---|
| set (=1) | 25 | 34 |
| **clear at pc=0x100dcf0 (DMAC handler)** | **8** | **33** |
| clear at pc=0x100afa8 (guest poll) | 24 (at 0x100af5c/0x100afa8) | **1** |

So the empty-chain transfers now complete and the DMAC handler clears the GIF busy byte 33× instead of
8× — the drop at line 1769 was real and is closed. tid2's return address moved off the busy poll:
`ra=0x0100afa8` → `ra=0x0100b680`.

**The NEW blocker — named, with a number, NOT fixed.** tid2 is now in the DMAC-struct handshake at
0x100b628: `s0 = 0x70002050`, loop `ld v1,0(s0); beq s1,v1 -> jal 0x101f340` (SleepThread), i.e. spin
until the qword at **0x70002050** changes from the snapshot at 0x100b668.

```
0x32 sce_SleepThread calls=16076  ra_count=0x0100b680 x16075, 0x0100afa8 x1
                                  arg a0=0x00000001 x16075 (us=1), 0x100afa8/a0=0x8 x1
0x42 sce_SignalSema   calls=57    a0 = 5 (25x), 6 (29x), 2 (1x), 4 (1x) -- NEVER 7
SEM id=7 count=0 waiters=1
```
**SignalSema is called 57 times in the whole run and never once with `a0=7`.** tid1's only reason to be
runnable is sema7 being signalled, and nothing signals it. That is the next blocker: sema7 has no
signaller on this path.

The coordinator's suspicion (a W161 SleepThread-as-timed-sleep / `completeTimedSleeps` gap) is **not**
what is observed here: the sleeps are `us=1` and they *do* return (tid2 re-checks 16,075 times), so the
timed-sleep wake path works. The wall is the missing SignalSema(7).

### Law-8 capture — RAW (Segment C)

```
$ cd tools/PS2Recomp && git diff --numstat
164	0	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
212	4	ps2xRuntime/src/lib/ps2_memory.cpp
127	2	ps2xRuntime/src/lib/ps2_runtime.cpp

ps2_memory.cpp  live+=212  patch+=212  PASS   (tools/patches/ps2recomp-linux-w275-zeroqwc-chain-complete.patch)
EeScheduler/System/ps2_runtime carried by ps2recomp-linux-w275-t1b-giftag-empty-chain.patch (164/98/127)
```

### Mechanical gate — RAW (Segment C)

```
=== VULCAN 4 dish gate — 2026-10-08 10:07 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275busy-capture.png
      GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference - the screen did not change.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275busy-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275r1busy.log
      halt=guest_blocked
INFO  missing-function hits in that log: 0
=== RESULT: MECHANICAL CLAIMS HOLD ===
GATE_EXIT=0
```

## Segment D — SleepThread is UNTIMED (the W161 divergence), and ExecPS2 FIRES

**Change.** `sce_SleepThread` (syscall 0x32) no longer reads `$a0` as microseconds. The guest
trampoline at `0x101f340` sets only `$v1=0x32` and issues `syscall 0` — it never writes `$a0`, so the
W88 "read $a0 as a duration" branch was reading a register the caller never set. ps2tek.md:5065 and
09-ps2tek.md both give `32h SleepThread: void`. The W88 comment in `EeScheduler.cpp` asserting the
opposite is replaced with the settlement. Two A/B knobs, BOTH OFF by default:
`VULCAN4_W161_SLEEP_TIMED=1` (restores the W88 timed read) and `VULCAN4_W161_SLEEP_US=<n>`.
`DelayThread`/semaphore code untouched.

Files: `ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp`, `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`.

### Measurement — 15 s plain boot, no probe env set

Before = `boot_w275r1.log` (zero-QWC fix, SleepThread timed). After = `boot_w275sleept.log`
(this fix). Both: `/mnt/ssd/vulcan4-build/run/`, engine `recomp_engine_w251`, `-j2`, probes OFF.

| | before (`w275r1`) | after (`w275sleept`) |
|---|---|---|
| **ExecPS2 invocations** | **0** | **1** |
| `functions_entered` | 751 | **10,417** |
| `halt` | guest_blocked | guest_blocked |
| `sce_SleepThread` calls (0x32) | 16,076 | **14** |
| `sleepCurrentCalls` | 16,076 | 14 |
| `vblanks_processed` | 50 | 50 |
| `intr_queued` / `intr_run` | 118 / 119 | 67 / 58 |
| `frames_presented` | 363 | 333 |
| tid1 state | `status=2 wait=sema#7 pc=0x0101f468` | `status=Dormant pc=0x5ad8c8` |

**EXECPS2 FIRED — the coordinator's stop condition. Entry PC reported:**

```
[execps2] entry=0x100008 gp=0x0 argc=2 argv=0x80075334
VULCAN4 EXECPS2 -> unified resolve entry=0x00100008 (engine 0x00100008,0x00617a14)
VULCAN4 EXECPS2 relaunch#1 entry=0x00100008 gp=0x00000000 argc=2 argv=0x80075334
```

The call site: `run_rpc` path — `[w119:unrouted] NEW syscall 0x7 raw=0x0 guestV1=0x7 a0=0x100008
a1=0x0`, `PC=0x101f078 RA=0x1028b30`, `$a2=0x2 $a3=0x80075334`. So the guest invoked ExecPS2 with
entry `0x100008`, `gp=0`, `argc=2`, `argv=0x80075334`. Both runs before this point load the same IRX
set (SIO2MAN, MTAPMAN, MCMAN, MCSERV, PADMAN) — the module load is NOT the difference; what changes is
that the guest now gets *past* it instead of parking 751 functions in.

### The new wall (post-ExecPS2)

The relaunch runs, then the EE hits five targets with no generated function and tid1 exits:

```
Error: No exact recompiled function for guest PC 0x5b7560 ... codeRegion=no
[guest-branch:missing-target] kind=IndirectJump op=dispatch source=0x0 target=0x5b7560 pc=0x5b7560 ra=0x1001f0 sp=0x2000000 gp=0x6dddf0 a0=0x8a215c a1=0xffffffff
Error: No exact recompiled function for guest PC 0x5adf20
Error: No exact recompiled function for guest PC 0x48ef90
Error: No exact recompiled function for guest PC 0x107f08
Error: No exact recompiled function for guest PC 0x5b78a0
```

`0x04 sce_ExitThread calls=1 last_pc=0x005ad8c8` → `VULCAN4 THREADS runningThreadId=0 count=1` with
tid1 `status=Dormant` → `halt=guest_blocked`. Note `0x5b7560` is an `IndirectJump` dispatched with
`source=0x0` and `tableBase=0x1000008 tableEnd=0x102dbec` — the target is far outside the recompiled
image, so this is a dispatch-input (analyzer/TOML) problem, not a runtime one.

### Law-8 capture — RAW (Segment D)

```
$ cd tools/PS2Recomp && git diff --numstat
171	6	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
28	7	ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
212	4	ps2xRuntime/src/lib/ps2_memory.cpp
127	2	ps2xRuntime/src/lib/ps2_runtime.cpp

# content check: every live added line must appear in the union of tools/patches/*.patch
PASS  live_added=171  distinct=136  MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
PASS  live_added=98   distinct=74   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
PASS  live_added=28   distinct=27   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
PASS  live_added=212  distinct=150  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_memory.cpp
PASS  live_added=127  distinct=86   MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_runtime.cpp
```

Segment D's own files are carried whole by
`tools/patches/ps2recomp-linux-w275-sleepthread-untimed.patch` (EeScheduler +171, Thread +28 —
16905 bytes). A **count-based** check is not enough here and was rejected: the old patch
`ps2recomp-linux-g18c-baselineframe.patch` shows `best+176` added lines for `EeScheduler.cpp` while the
live diff is 171 — a bigger number that does not prove it carries today's lines. The check above
compares line *content*, not counts.

### Mechanical gate — RAW (Segment D, `bash .auto/verify-dish.sh`)

```
=== VULCAN 4 dish gate — 2026-10-08 10:21 ===
FAIL  suite failed=unknown (ran /mnt/ssd/vulcan4-build/ps2xTest/ps2x_tests)
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275sleept-capture.png
      structural sig  : capture 1 colours / nonblack 0.1085   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 1 colours and a non-black fraction (0.1085) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275sleept-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275sleept.log
      halt=guest_blocked
INFO  missing-function hits in that log: 49
=== RESULT: CLAIM NOT SUPPORTED ===
GATE_EXIT=1
```

**The gate does NOT exit 0. The one failing check is the suite, and the suite failure is
pre-existing — it is not Segment D's.** Note the gate prints `failed=unknown` rather than a count
because the test binary dies before printing its `Failed:` line.

### The suite SEGFAULT — proven pre-existing, RAW at pure nested HEAD

`ps2x_tests` exits **139** inside `[Suite]: PS2RuntimeExpansion`, after 240 `[Passed]` lines.

Proof it is not this segment's: the pre-existing test binary predated the Segment D source edits and
carried none of its strings —

```
$ stat -c '%y %n' ps2xRuntime/src/lib/ps2_runtime.cpp ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
2026-10-08 10:17:58 ps2xRuntime/src/lib/ps2_runtime.cpp
2026-10-08 10:17:58 ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
$ stat -c '%y %n' /mnt/ssd/vulcan4-build/ps2xTest/ps2x_tests
2026-10-08 10:16:34 /mnt/ssd/vulcan4-build/ps2xTest/ps2x_tests
$ strings -a /mnt/ssd/vulcan4-build/ps2xTest/ps2x_tests | grep -c 'W161_SLEEP_TIMED\|Segment D'
0
```

and, decisively, ALL W275 nested edits stashed at pure HEAD, binary rebuilt from scratch:

```
$ cd tools/PS2Recomp && git stash push -m w275-segD-law8verify && git rev-parse --short HEAD
76c4290
$ cd /mnt/ssd/vulcan4-build && cmake --build . --target ps2x_tests -j4   # tail
[100%] Linking CXX executable ps2x_tests
[100%] Built target ps2x_tests
$ stat -c '%y' ps2xTest/ps2x_tests
2026-10-08 10:21:40
$ ./ps2xTest/ps2x_tests ; echo $?
Segmentation fault
139
[Suite]: PS2RuntimeExpansion
```

Then the stash was popped; `git diff --numstat` came back byte-identical (171/6, 98/0, 28/7, 212/4,
127/2) and the binary rebuilt. Final run with the edits restored: `exit 139`, 240 `[Passed]`, dies at
`[Suite]: PS2RuntimeExpansion`. **Same crash, same place, with and without every W275 edit.**

Site (from the pre-compaction backtrace): `PS2Runtime::hasFunction` (`ps2_runtime.cpp:1803`, which
normalises and calls `generatedFunctionTableSlot` at `:1750` and reads
`g_ps2RecompiledFunctionTable[slot]`) ← `PS2Runtime::dispatchGuestBranch` ← test lambda #11 ← `main`.
The test side defines that table in `ps2xTest/src/test_function_table.cpp`:
`Base = 0`, `End = PS2_RAM_SIZE`, `SlotCount = (End - Base) >> 2`. `hasFunction`'s bounds check passes
a slot the table was never sized for. **Not fixed here** — out of Segment D's scope, and it must be
owned: it is the only reason this dish's gate is not green.

**Segment D is therefore a FAILED-GATE dish by law 4, with the gate output above.** What it proves
is the measurement: ExecPS2 fires. The suite SEGFAULT is the handoff.

---

## R2 — the 5 dispatch misses: root cause + fix (architect, 2026-10-08)

**Claim in tasks.md (rejected):** "0x5b7560/0x5adf20/0x48ef90 ARE in CSV yet missed · 0x5b78a0 is an
analyzer gap · 0x107f08 is a loader entry". Measured against the artifacts, this is wrong on all three.

**Measured truth — all 5 are already recompiled and registered in the ENGINE table:**

| target | engine-symbols.csv? | engine register_functions.cpp? | entry vs target | why missed |
|---|---|---|---|---|
| 0x5b7560 | YES `FUN_005b7560` size 4 (line 16938) | YES slot 1236310 `sub_005b7560_0x5b7560` | function ENTRY | lookupFunction = loader table only |
| 0x5adf20 | YES `FUN_005adf20` size 16 (line 16798) | YES slot 1226694 `sub_005adf20_0x5adf20` | function ENTRY | same |
| 0x48ef90 | YES `FUN_0048ef90` size 4 (line 12636) | YES slot 932834 `sub_0048ef90_0x48ef90` | function ENTRY | same |
| 0x107f08 | NO | YES slot 8128 `sub_00107f08_0x107f08` | function ENTRY (recompiler-discovered) | same |
| 0x5b78a0 | NO | YES slot 1236518 `sub_005b7788_0x5b7788` `// 0x5b78a0` | interior jump TARGET inside sub_005b7788 | same |

(`grep -in "0x005b7560"` etc. — the CSV stores 8-digit uppercase hex, which is why a 6-digit grep
read as "not in CSV".)

**Root cause (single, uniform):** `PS2Runtime::lookupFunction` (`ps2xRuntime/src/lib/ps2_runtime.cpp:1850`)
resolves a guest PC against ONLY `g_ps2RecompiledFunctionTable` (loader, base `0x1000008` end
`0x102dbec`). Two other resolvers already handle the second image:
- `hasFunction` (same file :1803) falls through to `g_ps2EngineFunctionTable` (W250 block);
- the harness arrival loop's `resolveFunction` (vulcan4_harness.cpp:1605) consults both (W241).

The EE Scheduler drive loop (`EeScheduler.cpp:332` gate → `:351` resolve) uses `hasFunction` for the
gate (passes for engine addresses) then `lookupFunction` for the pointer (fails for engine addresses).
So an engine target passes the gate and then dies in the lookup, printing exactly
`No exact recompiled function ... tableBase=0x1000008 tableEnd=0x102dbec`, returning the
`missingFunction` lambda, which `reportMissingFunction`s with `kind=IndirectJump op=dispatch source=0x0`
— byte-for-byte the boot-log line 65588. `sce_ExitThread @0x005ad8c8` → tid1 Dormant → halt guest_blocked.

**Fix (applied):** `lookupFunction` now normalises KSEG0/KSEG1 and falls through to
`g_ps2EngineFunctionTable`, mirroring `hasFunction` including the weak-symbol null guard (so the
no-engine unit-test link still resolves `&g_ps2EngineFunctionTableSlotCount == nullptr`). This is a
runtime source fix, NOT a TOML/analyzer change and NOT generated-code regeneration.

- Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r2-lookup-engine-table.patch` (reverse-apply
  check passed against the live tree — the patch carries exactly the applied change).
- Rebuild (the driver directs rebuild+boot): `cd /mnt/ssd/vulcan4-build && VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash /home/or/vulcan4/tools/harness/build_harness.sh`

The 6 added lines beyond the board's W275 "127/2" ps2_runtime.cpp numstat are these two R2 hunks;
the rest of ps2_runtime.cpp's diff remains the W275 probe work, already captured in the w275 patches.

## Segment E (R2) — `lookupFunction` falls through to the engine table; the 5 misses are GONE

**Change (architect-authored, in the tree uncommitted when measured).** `PS2Runtime::lookupFunction`
(`ps2_runtime.cpp:1850`) normalised the address and consulted ONLY `g_ps2RecompiledFunctionTable`
(the loader table). `hasFunction` had always also consulted `g_ps2EngineFunctionTable`, and the engine
image — the GT4 code the recompiler emitted into the second image, base `0x00100000`, end `0x00617A14`
— holds every one of R1's five blocks. So an indirect jump or a scheduler resume into the engine image
resolved as "No exact recompiled function" while `hasFunction` said the address existed. R2 makes
`lookupFunction` mirror `hasFunction`: normalise, try the loader table, then fall through to the engine
table (weak-symbol-guarded so the no-engine unit-test link still works). Diff: **ps2_runtime.cpp
+158/-5** (was +127/-2 before R2).

### Measurement — 15 s plain boot, probes OFF, `boot_w275r2.log` vs `boot_w275sleept.log`

Both in `/mnt/ssd/vulcan4-build/run`, engine `recomp_engine_w251`, `-j2`, no probe env set.

| | R1 (`w275sleept`) | **R2 (`w275r2`)** |
|---|---|---|
| ExecPS2 invocations | 1 | 1 |
| **`functions_entered`** | 10,417 | **10,446** |
| `true_guest_entries` | 1,535,095 | **1,730,333** |
| **`ee_cycle`** | 3,770,458 | **37,262,408** (9.9×) |
| `frames_presented` | 333 | **864** (2.6×) |
| `distinct_pcs` | 188 | **207** |
| **`halt`** | `guest_blocked` | **`stuck_in_syscall`** |
| `sce_ExitThread` (0x04) | **1** call at `pc=0x005ad8c8` | **0** |
| `[guest-branch:missing-target]` events | **1** line / 5 targets | **0** |
| `grep -ac 'missing-target\|no generated function'` | 49 | 57 |
| tid1 | `status=Dormant pc=0x5ad8c8` | `status=0(running) pc=0x005b0ac8` |

**R2's gate: the dispatch misses are gone, the thread survives, the halt is a NAMED reason.**
`functions_entered` rose only +29 because it counts *distinct generated functions entered*, and the
recompiled set was already nearly saturated — the growth is in **work, not in reach**: 9.9× the
EE cycles and 2.6× the presented frames for the same 15 s wall clock. The block that ended R1 —
`sce_ExitThread` at `pc=0x005ad8c8`, killing tid1 (`status=Dormant count=1 runningThreadId=0`) — does
not happen at all in R2. tid1 is alive and running at `pc=0x005b0ac8`, which is **past** `0x5ad8c8`.

**Engine-image PCs executed: 19 distinct** inside `0x00100000..0x00617A14`, measured from the distinct
`last_pc=` values on every syscall tally line (`/mnt/ssd/tmp/r2_pcs.txt`, 33 distinct syscall-site PCs,
19 in range). Plus the running PC `0x005b0ac8` itself, in range. `> 0` — gate met.

### The new wall (named, not a crash)

```
VULCAN4 BOOT REPORT ... halt=stuck_in_syscall ...
VULCAN4 HARNESS detail=blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x005b0ac8
    -- this syscall is the wall  pc=0x005b0ac8 distinct_pcs=207 ... ee_cycle=37262408
    ... thread_state=tid1:status=0:wait=none#0:pc=0x005b0ac8:ra=0x005b0ac8 ... deadline_s=15
  0x83 sce_FindAddress calls=4 last_pc=0x005b7410
     ra_count=0x010286dcx1,0x010286f0x1,0x005b74acx1,0x005b74c0x1
     a0=0x80000000  a1=0x80080000  s0=0x010286dc/0x01035350, 0x005b74ac/0x00658368
```

This is the **watchdog deadline**, not a deadlock: the guest was alive and executing when the 15 s
budget expired. `activeSyscallId()` was 0x83, so the harness names it. FindAddress was called only
**4** times (so it is not the livelock shape the harness also detects), each over the window
`0x80000000..0x80080000` — 131,072 words. The caller `ra=0x010286dc` / `0x010286f0` is in the
`0x0102xxxx` loader/IRX region and `ra=0x005b74ac` / `0x005b74c0` is in the engine image.

**Counter, and it matters: the 57 `no generated function` hits in R2 are NOT the R1 misses.** They are
a *different*, pre-existing limitation and none of them is a dispatch miss:

```
57  VULCAN 4 LIMITATION: syscall 0xNN override handler 0xADDR has no generated function in either
    image — not invoking (was silent KE_ERROR).
```
48 of the 57 are syscall 0x56; handlers `0x5b79f8`, `0x5b98d0`, `0x800750c8`, `0x80076000` — the
`0x8007xxxx` ones are the IOP-side addresses R1 also reported. `grep -ac
'\[guest-branch:missing-target\]'` in R2 is **0**. So the coordinator's grep
`'missing-target\|no generated function'` reads 57 while the *dispatch misses it is meant to count*
are 0 — the two are not the same thing and the number alone would have been misread.

### Law-8 capture — RAW (Segment E)

```
$ cd tools/PS2Recomp && git diff --numstat
171	6	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
28	7	ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
212	4	ps2xRuntime/src/lib/ps2_memory.cpp
158	5	ps2xRuntime/src/lib/ps2_runtime.cpp

PASS  live_added=171  distinct=136  MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
PASS  live_added=98   distinct=74   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
PASS  live_added=28   distinct=27   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
PASS  live_added=212  distinct=150  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_memory.cpp
PASS  live_added=158  distinct=111  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_runtime.cpp
```

The R2 fallback is carried by `tools/patches/ps2recomp-linux-r2-lookup-engine-table.patch`
(11,222 bytes, 158 added lines vs the live 158 — the whole-file diff, so it carries the earlier
ps2_runtime.cpp work as well and cannot under-capture).

### Confirmation boot — 45 s, same binary, probes OFF (`boot_w275r2long.log`)

| | 15 s | **45 s** |
|---|---|---|
| `functions_entered` | 10,446 | **10,458** |
| `true_guest_entries` | 1,730,333 | **2,380,133** |
| `ee_cycle` | 37,262,408 | **63,253,716** |
| `frames_presented` | 864 | **2,620** (3.03×) |
| `vblanks_processed` | 57 | 62 |
| `sce_FindAddress calls` | 4 | **4** (unchanged) |
| `sce_ExitThread calls` | 0 | **0** |
| `[guest-branch:*]` / `No exact recompiled function` | 0 / 0 | **0 / 0** |
| halt | `stuck_in_syscall` | `stuck_in_syscall` (same pc `0x005b0ac8`) |

**So the FindAddress halt is NOT a hard wall and NOT a re-entry loop.** Frames keep presenting
*linearly* (864 → 2,620 = 3.03× for 3.0× wall clock), no dispatch miss ever occurs, and `FindAddress`
is called **4 times in both runs** — the guest is not stuck inside it. The wall is the 15 s / 45 s
**deadline**: tid1 is alive and `status=0 (running)` at `pc=0x005b0ac8` doing guest-side compute.
`functions_entered` climbs only +12 over 3× the time, which is the signature of a **guest loop inside
the already-recompiled set** — reach is saturated; the growth is in cycles executed, not functions
reached. That is the next thing to name (what is at `0x005b0ac8`), not a dispatch miss.

### Mechanical gate — RAW (`bash .auto/verify-dish.sh`, run on commit 2900615, tree clean)

```
=== VULCAN 4 dish gate — 2026-10-08 10:38 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275r2long-capture.png
      structural sig  : capture 1 colours / nonblack 0.1085   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 1 colours and a non-black fraction (0.1085) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275r2long-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275r2long.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 57
=== RESULT: MECHANICAL CLAIMS HOLD ===
GATE_EXIT=0
```

`GATE_EXIT=0` — the first exit-0 gate in this dish. Check 1 (suite) flipped from the pre-existing
SEGFAULT to **497/497** via commit `097695a` (null-guard the weak engine-table symbols in
`hasFunction`, the crash recorded earlier in this doc). The picture check is informational and still
reports the disclaimer; that is expected — R2 is a dispatch fix and does not draw a menu.

---

## R3 — the "FindAddress" wall is actually syscall 0x7A (SifGetReg) unwired (architect, 2026-10-08)

**The coordinator's premise (FindAddress returns wrong) is rejected by the log.** Measured in
`boot_w275r2.log`:

| syscall | calls | args | verdict |
|---|---|---|---|
| 0x83 FindAddress | **4** | a0=0x80000000 a1=0x80080000 (512KB window, all 4) | fast, not looping |
| 0x7A (unwired) | **195100** | a0=4 (first call 0x80000000), a1=0x5b0e30, a3=0x20 | the wall |

`[w119:unrouted] NEW syscall 0x7a ... pc=0x5ae0b8` + `Warning: Unimplemented PS2 syscall called
... v0=0x0` repeated — syscall 0x7A falls through `dispatchNumericSyscall`'s default and returns 0.

**The loop** (decompiled `sub_005b08c8`, engine `ps2_recompiled_functions_41.cpp:192864`):
```
0x5b0ac0: jal  0x5AE0B0        # thunk: addiu $v1,0x7A; syscall 0   (SifGetReg)
          addiu $a0, $zero, 4  # a0 = 4
0x5b0ac8: and  $v0, $v0, $s0   # s0 = 0x20000
          beqz $v0, 0x5b0ac0   # while ((SifGetReg(4) & 0x20000) == 0)
```
A boot-ready poll: it spins until SIF register 4 has bit 0x20000 set. 0x7A returns 0, so the bit never
sets — hence the 195100 calls and `halt=stuck_in_syscall`.

**The runtime already knows the answer.** `SIF.cpp`:
```cpp
constexpr uint32_t kSifRegBootStatus = 0x4u;        // SIF register 4 = boot status
constexpr uint32_t kSifBootReadyMask  = 0x00020000u; // bit 0x20000
// seedDefaultSifRegsLocked(): g_sifRegs[kSifRegBootStatus] = kSifBootReadyMask;
```
`ps2_stubs::sceSifGetReg` (SIF.cpp:464) returns `g_sifRegs[reg]` = 0x20000 for reg 4. The ONLY missing
link: `Dispatcher.cpp` had no `case 0x7A` (nor 0x79/0x7B). This is the concrete bug, not FindAddress.

**Why the harness said "FindAddress"**: its stuck-detection reads a stale `m_activeSyscallId` (0x83)
instead of the dominant syscall (0x7A) — the dominant-syscall branch requires `active == dominantId`,
and the stale active value fails that test. The guest pc in the halt detail (0x005b0ac8) is the poll
loop, not a FindAddress site, which is the giveaway.

**Fix applied** (`ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp`, after case 0x78):
```
case 0x79: ps2_stubs::sceSifSetReg(rdram, ctx, runtime); return true;
case 0x7A: ps2_stubs::sceSifGetReg(rdram, ctx, runtime); return true;
case 0x7B: ps2_stubs::sceSifStopDma(rdram, ctx, runtime); return true;
```
- Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r3-sifgetreg-dispatch.patch` (reverse-apply OK).
- Rebuild (driver directs): `cd /mnt/ssd/vulcan4-build && VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash /home/or/vulcan4/tools/harness/build_harness.sh`
- Note: FindAddress's return logic (KSEG1-canonical hit / `end` on miss) was already correct; no change.

### R3 HLE comparison — syscall 0x83 (FindAddress) vs the PCSX2/BIOS oracle

**1. PCSX2 does NOT HLE 0x83 — it falls through to the BIOS.**

- `SYSCALL()` dispatcher: `/mnt/ssd/tools/pcsx2-src/pcsx2/R5900OpcodeImpl.cpp:908`.
- The HLE table is `enum Syscall` at `/mnt/ssd/tools/pcsx2-src/pcsx2/R5900OpcodeTables.h:10-26`. It lists
  exactly 14 ids: `SetGsCrt=2, ExecPS2=7, SetVTLBRefillHandler=13, StartThread=34, ChangeThreadPriority=41,
  RFU060=60, SetOsdConfigParam=74, GetOsdConfigParam=75, SetOsdConfigParam2=110, GetOsdConfigParam2=111,
  sysPrintOut=117, sceSifSetDma=119, Deci2Call=124, GetMemorySize=127`. `FindAddress` (0x83 = 131) is not among them.
- Any id without a case hits `default: break;` (`R5900OpcodeImpl.cpp:1200-1201`) and then
  `cpuRegs.pc -= 4; cpuException(0x20, cpuRegs.branch);` (`:1204-1205`). Exception 0x20 is the standard SYSCALL
  vector, i.e. control transfers to the BIOS ROM kernel, which implements 0x83 itself. **Therefore PCSX2's
  behaviour for 0x83 IS the BIOS behaviour** — there is no emulator-side shortcut to compare against; the oracle
  is the BIOS ROM code.

**2. The FindAddress contract (what the BIOS does).**

- ps2sdk `ee/kernel/include/syscallnr.h`:
  https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h — defines
  `#define __NR_FindAddress 0x83` with **no comment** (no signature, no return-value doc). ps2sdk does not
  implement it (it is a BIOS/rom0 syscall, not a libkernel function).
- Corpus: `~/.config/opencode/skills/ps2-recomp-Agent-SKILL/resources/09-ps2tek.md` has **no** FindAddress / 0x83
  entry (grep returns nothing). The only corpus row is `db-syscalls.md:159`
  (`| 0x83 | FindAddress | a0=id | $v0=addr | impl | |`) — the `a0=id` is wrong; a0 is the table start. Lines
  `db-syscalls.md:188-189` group it under Memory/Cache.
- The concrete reference implementation is upstream PS2Recomp PR #93 (Whoneon), which shipped with kernel tests:
  https://github.com/ran-j/PS2Recomp/pull/93/files. It reads `a0`=table start (inclusive), `a1`=table end
  (exclusive), `a2`=target; aligns the window to word boundaries; steps by `sizeof(uint32_t)` (**word scan**);
  on a match returns the guest address of the first matching word (**preserving the segment it was found in**);
  on a miss returns **0**. Its kernel test asserts exactly those three outcomes (first-word address, KSEG-alias
  match preserving segment, 0 on miss).

**3. Diff against OUR semantics (`System.cpp:956`).**

| Aspect | PCSX2/BIOS + upstream PR #93 | Ours (`System.cpp:956`) | Divergence? |
|---|---|---|---|
| Scan unit | word (4-byte), window word-aligned | word (`start=(start+3)&~3; end&=~3; addr+=4`) | no — matches |
| Alias fold in compare | KSEG fold `0x80000000–0xBFFFFFFF` | `normalizeKernelAlias` (`:791-798`, same range) | no — matches |
| De-dup of aliased words | none | high-water mark (`:1040-1050`) | **yes — ours skips re-visited physical words** |
| Match return | address of first matching word, **segment preserved** | forced to KSEG1: `0x80000000 | (resultAddr & PS2_RAM_MASK)` (`:1224-1228`) | **yes — ours canonicalises** |
| Miss return | **0** (upstream PR #93 test) | **`end`** (aligned-down window end, `:1229`) | **yes** |

Two structural notes:

- `computeBuiltinFindAddressResult` (`System.cpp:800-830`) is a SECOND, unwired FindAddress that returns 0 on
  miss, does no de-dup and no canonicalisation — closer to upstream than the live handler, but it has no callers
  (dead code). The live path is `Dispatcher.cpp:314-315` → `FindAddress`.
- Root cause of the de-dup: our host address map WRAPS physical modulo 32 MB
  (`include/runtime/ps2_memory.h:137-140`, `if (phys >= PS2_RAM_SIZE) phys &= PS2_RAM_MASK;`). On real hardware
  only 32 MB of RDRAM exists and the regions beyond it do not mirror; in our runtime every 0x02000000 guest block
  re-reads the same 32 MB, which is what produced the historic 537M-word / 2 GB scan (comment at
  `System.cpp:1026-1039`). The high-water mark exists only to make that converge.

**4. Can the high-water mark skip the FIRST legitimate match? (the named risk)**

- What it does: skips any guest address whose resolved physical offset `<= highestScannedPhysicalOffset`
  (`System.cpp:1044-1047`). Because our map makes `phys(addr) == addr & 0x1FFFFFFF`, `phys` resets to 0 at every
  0x02000000 boundary, so the mark scans the first 32 MB block of the window fully and then **skips every later
  block**. It can therefore only produce a **false MISS** (return `end`) — never a wrong "later alias" — and only
  when (a) the window spans a 32 MB boundary and (b) the matching word's lowest in-window alias lies in a block
  after the one that set the high-water mark (its phys ≤ block-0 max).
- For the calls GT4 actually makes in this boot: `a0=0x80000000`, `a1=0x80080000` (512 KB, entirely inside the
  first KSEG0 32 MB block; phys monotonic 0x00000000→0x0007FFFF, no boundary). **The high-water mark is inert for
  these 4 calls** — it never triggers. The builder's R2 measurement also recorded 0 misses across the 4 calls, so
  the miss-return divergence is not exercised either. Conclusion: the 0x005b0ac8 loop is **not** caused by the
  FindAddress de-aliasing or by the miss return; this is consistent with the board's "guest compute loop, not a
  re-entry loop" reading. The high-water mark remains a latent false-miss bug for any future window that crosses a
  32 MB alias boundary, but it is not the current wall.

No fix written, per the R3 angle brief.

## R3 — MEASURER / ORACLE: side-by-side at pc 0x005b0ac8, HARDWARE vs OURS (2026-10-08T07:56:36Z)

Instrument: PCSX2 `-debugger` (DebugServer :21512), GT4 (USA) v2.00, temporary breakpoint, paused **exactly** at
PC=0x005b0ac8 (first pass), engine verified loaded at that instant
(`pcsx2_read_memory 0x005b0ac0` = `2c b8 16 0c 04 00 04 24 24 10 50 00 fc ff 40 10` = `jal 0x5ae0b0 / li a0,4 / and v0,s0 / beqz v0,-4`).

| reg @ 0x005b0ac8 | HARDWARE | OURS (`/mnt/ssd/vulcan4-build/run/boot_w275r2.log`) |
|---|---|---|
| **v0** | **0x00070000** | **0x00000000** |
| a0 | 0x00070000 | 0x00000004 |
| a1 | 0x80018F58 | 0x005b0e30 |
| a2 | 0x80019058 | 0x00000000 |
| a3 | 0x00000020 | 0x00000020 |
| s0 | 0x00020000 | 0x00020000 |
| ra / pc | 0x005b0ac8 | 0x005b0ac8 |

### The first divergence, named

* **First diverging hardware event:** the **return of `jal 0x005AE0B0`** — the thunk for kernel syscall `v1=0x7A`
  (syscall instruction at 0x005ae0b8). Hardware runs a real kernel handler and returns **v0=0x00070000**. Ours has no
  handler for 0x7A (absent from `include/runtime/syscall_names.h`), takes the "Unimplemented PS2 syscall" path and returns
  **v0=0**. Our own log, 195,099 times: `Warning: Unimplemented PS2 syscall called. PC=0x5ae0b8, RA=0x5b0ac8, Encoded=0x0, v0=0x0, v1=0x7a`.
* **First instruction whose effect differs:** `and v0, s0` at **0x005b0ac8** → hardware 0x00020000, ours 0x00000000.
* **First control-flow divergence:** `beqz v0` at **0x005b0acc** → hardware falls through to 0x005b0ad4; ours branches back
  to 0x005b0ac0. Ours then spins **195,099 iterations** (our log: `a0=0x00000004 x195099`) until the harness deadline at
  `ee_cycle=37262408`; hardware leaves the loop on the **first** pass.

### The second call — never reached on ours

`0x7A(a0=2)` after the loop: hardware v0 = **0x0001E640**, captured at PC=0x005b0adc with a0=0x00000002, s0=0x00886818
(the delay slot `addiu s0,s2,0x6818` had executed). Ours never reaches 0x005b0adc — our log shows only
`a0=0x00000004 x195099` and `a0=0x80000000 x1`; `a0=2` does not occur at all.

### Macro state, same boot, after hardware left the loop

Hardware: **20 EE threads**; EE idle at PC=0x00081FC0. 13 threads blocked in **SleepThread** (thunk 0x005adbc0 =
`li v1,0x32; syscall`, PC=0x005adbc8, waitType=2) and 6 in **WaitSema** (thunk 0x005adce0 = `li v1,0x44; syscall`,
PC=0x005adce8, waitType=1); 0x32/0x44 map to SleepThread/WaitSema in both our table and `db-syscalls.md:71,97`.
Ours: **1 thread**, `runnable_threads=tid1@prio0:pc=0x005b0ac8(running)`, no other thread ever runs.

### Caveats — measured, not assumed

1. Post-call `a0/a1/a2` are kernel-clobbered on hardware (a1 in 0x5b0e30 → out 0x80018F58; a0 in 4 → out 0x70000, = v0).
   Per O32 only v0 is meaningful after a call, and the deciding instruction `and v0,s0` consumes only v0 and s0. **v0 is the comparison.**
2. Hardware RDRAM 0x00012180 / 0x0001218C is **all zero** (`pcsx2_disassemble 0x00012180` = undefined; `read_memory u32_array` = eight 0x00000000).
   Our `VULCAN4 SYSTABLE ... slot=0x1218c handler=0x5b73c8` is **our own layout**, not hardware's. Hardware's own syscall-override
   descriptor block sits at physical **0x658360**: `{0x80014f40, 0x00000000, 0x00000083, 0x005b73c8}` then `{0x0000005A, 0x005b7390}`
   — GT4 overrides 0x83 and 0x5A itself, which our runtime already reproduces (`[SetSyscall] n=131 handler=0x5b73c8 slot=0x1218c`).
3. PCSX2's debugger read of SIF MMIO `0x1000F200..0x1000F260` returns **all zero**, so this oracle confirms 0x7A's **return values**,
   not its name. The mask claim is confirmed independently: hardware's `0x7A(a0=4)` return **does** carry bit 0x20000.
4. `pcsx2_read_memory` with a plain address reads **RDRAM** (engine visible, real nops at 0x00081fc0); the `0x8000_0000+` form reads
   **BIOS ROM** and cannot see the runtime-loaded engine — the two paths disagree by design, not by fault.

### Bottom line

The R3 wall is **not** FindAddress. It is the **unwired kernel syscall 0x7A**: hardware returns **0x00070000** (bit 0x20000 set,
loop exits immediately) and ours returns **0x00000000** (loop never exits). Everything downstream — `stuck_in_syscall`, the
195,099 iterations, `runnable_threads=tid1` — is a symptom of that one return value.

No fix written. No repo file modified except this append and `.auto/crew/board.md`. PCSX2 left paused with all breakpoints cleared.

---

## R3 STATIC ANGLE — what the code around 0x005b0ac8 actually does with the return value

Static-only decompile of the generated engine C++. No build, no commit, no fix written.

### 1. Which function contains 0x005b0ac8

`sub_005B08C8_0x5b08c8` — Address `0x5b08c8 - 0x5b0b48`.

- **Not** in `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`: that file is the main ELF
  image (lowest function `0x1000008`), and `0x005b0ac8` is *below* the ELF load base `0x00100000`. It is
  the engine module, loaded at `0x005b0000`.
- Active engine source (the one `VULCAN4_ENGINE_DIR=recomp_engine_w251` rebuilds):
  `ps2_recompiled_functions_41.cpp:192331` (`void sub_005b08c8_0x5b08c8(...)`; header `:192330`).
  The single-file copy `/mnt/ssd/vulcan4-build/_orphan_recomp_engine_105MB/ps2_recompiled_functions.cpp:2226705`
  is byte-identical for this function.

### 2. The call site — and the correction to the premise

The loop at `0x005b0ac8` does **not** call FindAddress (0x83). It calls `func_5AE0B0`, which is the
syscall-0x7A trampoline: `addiu $v1, $zero, 0x7A; syscall 0` (`_orphan...:2212104-2212110`). The recompiled
instruction is `syscall 0` (encoded id 0), so `handleSyscall` reads the real number from `$v1`
(`ps2_runtime.cpp:4915`: `encodedSyscallId != 0 ? encodedSyscallId : getRegU32(ctx, 3)`). Dispatcher
`case 0x7A -> ps2_stubs::sceSifGetReg` (`Dispatcher.cpp:298-300`).

So the loop's syscall is **0x7A `sceSifGetReg`**, a SIF register read, and `$v0` receives the register
value (`SIF.cpp:464-498`, `setReturnU32(ctx, value)`).

Only `$a0` is set at this site — the FindAddress 3-arg signature (start/end/target) is absent:

- first call (`label_5b0ac0`, `file_41:192868`): `jal func_5AE0B0`, delay slot `addiu $a0, $zero, 0x4`
  (`file_41:192875`) → `sceSifGetReg(reg=4)`.
- retry (delay of the `beqz`, `file_41:192895`): `addiu $a0, $zero, 0x2` → `sceSifGetReg(reg=2)`.

`a1`/`a2` are not touched by this loop.

### 3. What the caller does with $v0

`file_41` (recomp_engine_w251/ps2_recompiled_functions_41.cpp):

```
:192864  label_5b0abc: lui  $s0, 0x2          → $s0 = 0x00020000
:192868  label_5b0ac0: jal  func_5AE0B0       → syscall 0x7A (delay: a0 = 4)
:192884  label_5b0ac8: and  $v0, $v0, $s0     → $v0 = $v0 & 0x00020000   (:192885)
:192888  beqz $v0, 0x5b0ac0                   → loop if bit 17 clear     (:192888)
          delay: addiu $a0, $zero, 0x2        → a0 = 2 on retry          (:192895)
:192903  goto label_5b0ac0
```

- **v0 == 0 after the AND** (bit 0x20000 clear): branch taken, loops back to `label_5b0ac0` with `a0=2`.
- **v0 != 0 after the AND** (bit 0x20000 set): branch not taken, falls through `:192907` to `0x5b0ad4`,
  which reads `sceSifGetReg(reg=2)` into `0x886820` (`sw $v0, 0x8($s0)`), then tail-jumps to
  `sub_005B0DB0` (the SIF DMA command handler).

### 4. The loop EXIT condition

Branch: `beqz $v0` at guest `0x5b0acc` (`file_41:192888`), backward target `0x5b0ac0`
(`goto label_5b0ac0`, `file_41:192903`).

Exit value: `($v0 & 0x00020000) != 0` — i.e. `sceSifGetReg(reg=4)` must return with bit 17 set. Reg 4 is
`kSifRegBootStatus = 0x4` and the mask is `kSifBootReadyMask = 0x00020000` (`SIF.cpp:82-83`); the stub
seeds `g_sifRegs[4] = 0x00020000` (`SIF.cpp:100`). So the guest is polling the **SIF boot-ready** flag.

Boot log confirms this is the real wall, not FindAddress: syscall tally `0x7a ... calls=195100
last_pc=0x005ae0b8 ra_count=0x005b0ac8x195099,0x005b0a74x1` (`boot_w275r2.log:651162`), and the w119 spin
probe shows `0x7a ... v0Same=187971` — the register value never changes, so the AND never becomes
nonzero. The harness's `"blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x005b0ac8"` label pairs a
live `m_activeSyscallId` sample with a stale `ctx.pc`; the dominant syscall is 0x7A (195100 of 287128 calls).

### What the engine EXPECTS FindAddress (0x83) to return — the actual 0x83 caller

The module's real FindAddress consumer is `sub_005B7450_0x5b7450`
(`recomp_engine_w251/ps2_recompiled_functions_42.cpp:29857`, Address `0x5b7450 - 0x5b7560`):

```
:29954  0x5b74a4  jal func_5B7408    ; func_5B7408 = addiu v1,0x83; syscall 0  (the 0x83 wrapper)
:29948  0x5b749c  lui $a0, 0x8000    ; a0 = 0x80000000  (table start)
:29951  0x5b74a0  lui $a1, 0x8008    ; a1 = 0x80080000  (table end)
:29960  0x5b74a8  addiu $a2, $s5, 0x73C8   ; a2 = 0x005B73C8  (target)
:29970  0x5b74ac  daddu $s3, $v0, $zero    ; s3 = $v0  (result 1)
:29979  0x5b74b8  jal func_5B7408          ; second scan
:29973  0x5b74b0  lui $a0, 0x8000          ; a0 = 0x80000000
:29976  0x5b74b4  lui $a1, 0x8008          ; a1 = 0x80080000
:29985  0x5b74bc  addiu $a2, $s4, 0x7390   ; a2 = 0x005B7390  (target)
:29995  0x5b74c0  addiu $s1, $s3, -0x20C   ; s1 = s3 - 0x20C   (0x20C = 0x83*4)
:30001  0x5b74c8  addiu $s0, $s2, -0x168   ; s0 = s2 - 0x168   (0x168 = 0x5A*4)
:30004  0x5b74cc  beq  $s1, $s0, 0x5b7520  ; converged → store s1 (0x658360), return
:30024  0x5b74d8  beqz $v0, 0x5b74f8       ; (s1<s0) advance s3 side, else advance s2 side
:30109  0x5b7510  bne  $s1, $s0, 0x5b74d8  ; LOOP back while s1 != s0
```

Contract: the two FindAddress calls must return addresses `0xA4` apart
(`s3 - s2 == 0x20C - 0x168 == 0xA4 == (0x83 - 0x5A) * 4`), i.e. the module's `0x83` handler slot
(`0x5b73c8`) and its `0x5A`-pair slot (`0x5b7390`) are 41 entries apart in the syscall table. This is the
module's copy of the W10 convergence loop, and it is what `System.cpp:1205-1223` documents
(`s1 = s3 - 0x20C`, `s0 = s2 - 0x168`, `loop until s1 == s0`, slots `0x8001218C` / `0x800120E8`).

In this boot the 0x83 loop **already converged**: FindAddress was called exactly 4 times
(`ra=0x010286dc, 0x010286f0, 0x005b74ac, 0x005b74c0`, each once — `boot_w275r2.log:651171`), with **zero**
re-scan calls (`ra=0x5b74ec` / `0x5b7508` absent), so it never entered the `0x5b74d8/0x5b7510` loop.
The stall is downstream, in the `0x5b0ac8` SIF boot-ready poll.

---

## SEGMENT F — R3 EXECUTION (builder): syscall 0x7A (SifGetReg) is wired; the SIF boot poll EXITS

**Change (the whole of it).** `ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp`, 9 added lines, beside the
existing `case static_cast<uint32_t>(-0x78): sceSifSetDChain`:

```cpp
        case 0x79:  ps2_stubs::sceSifSetReg(rdram, ctx, runtime);  return true;
        case 0x7A:  ps2_stubs::sceSifGetReg(rdram, ctx, runtime);  return true;
        case 0x7B:  ps2_stubs::sceSifStopDma(rdram, ctx, runtime); return true;
```

The runtime already had `sceSifGetReg` (`ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp:464`), and
`seedDefaultSifRegsLocked()` seeds `g_sifRegs[kSifRegBootStatus=0x4] = 0x00020000u`. Nothing else was
touched. `dd`/`dma` are unchanged; probes stayed OFF.

### R2 → R3, 15 s plain boot, same binary path

| metric | R2 `boot_w275r2.log` | R3 `boot_w275r3.log` | factor |
|---|---|---|---|
| `functions_entered` | 10446 | **10543** | +97 |
| `true_guest_entries` | 1730333 | **13252077** | **7.7×** |
| `ee_cycle` | 37262408 | **498121721** | **13.4×** |
| `distinct_pcs` | 207 | **217** | +10 |
| `vblanks_processed` | 57 | **151** | 2.6× |
| `vsync_tick` | 7 | **101** | 14× |
| `intr_run` | 61 | **108** | 1.8× |
| `frames_presented` | 864 | 848 | — |
| syscall `0x7a` calls | 195100 | **4** | **the poll EXITED** |
| `Unimplemented PS2 syscall` | 195117 | **17** | 195100 removed |
| halting pc | `0x005b0ac8` | **`0x005b0880`** | moved |
| log size | 54,692,052 B | 6,422,434 B | shrank 8.5× |

The removed 195100 calls are exactly the R2 `Chosen=0x7a` count (195099 of them), i.e. the
`while ((SifGetReg(4) & 0x20000) == 0);` boot poll at `0x005b0ac8` — the wall named by the architect.

### The 17 remaining `Unimplemented PS2 syscall` lines are PRE-EXISTING, not caused by R3

Both are present identically in R2 (16 + 1 there too), and both are in the **main ELF image** (`0x01xxxxxx`),
not the engine:

```
  16  PC=0x101f0b8  Encoded=0x0  v0=0x1, v1=0xb  Chosen=0xb   RA=0x10183e0
   1  PC=0x101f078  Encoded=0x0  v0=0x1, v1=0x7  Chosen=0x7
```

- **syscall 0x0B — genuinely unmapped.** No `case 0x0B` anywhere in `Dispatcher.cpp`; falls to
  `default: return false;` → the warning path, v0 left 0. 16 calls, all from the same site.
- **syscall 0x07 — NOT unmapped, the log line is misleading.** ExecPS2 is special-cased in
  `System.cpp` *after* the warning is printed (`if (syscallId == 0x07u) { ... requestExecPS2(...) }`),
  and R1 proved it fires (count 1, entry `0x00100008`). So 0x07 logs as unimplemented while working.
- Both are `Encoded=0x0` → the dispatcher took `$v1` (`System.cpp:350`), so the *logged* id is the
  guest's `$v1`, not the encoded syscall field.

### Engine PCs — strict metric (distinct CODE addresses at syscall sites)

Distinct `last_pc=` / `from=Npc[...]` / `ra_count=0xNNNx` addresses inside the engine image
`0x00100000..0x00617A14`: **R2 27 → R3 31**. Main image (`0x01xxxxxx`): unchanged at 62. Total distinct
code addresses 90 → 94. (A looser count that also includes data-argument tokens gives 62 → 69.) Either
way, far past R2's floor of ">19".

Dispatch health: `No exact recompiled function` = **0**, `[guest-branch:` = **0**, `sce_ExitThread` = **0**.
The 57 `missing-target|no generated function` hits are all the pre-existing
`VULCAN 4 LIMITATION: syscall override handler has no generated function` line (the R2 trap) — never a
dispatch miss.

### The halt moved, but the halt LABEL is stale

`halt=stuck_in_syscall`, `detail=blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x005b0880`.
`0x83 sce_FindAddress calls=4` — **unchanged from R2** (4 calls, 0 re-scans): FindAddress never entered
its re-scan loop, so it is not the wall. The `0x83` in the detail line is the stale `m_activeSyscallId`
artifact the measurer identified; the engine's R3 tally still names the halting pc correctly via `pc=`.

### 45 s confirmation: pc=0x005b0880 is a DEADLINE, not a spin

`boot_w275r3long.log` (6,821,122 B, exit 0, 2 frames):

```
functions_entered=10786  true_guest_entries=52847539  halt=stuck_in_syscall
pc=0x005b1180  distinct_pcs=217  ee_cycle=2081907532  frames_presented=2525
vblanks_processed=473  intr_run=249  ra_count ... 0x83 sce_FindAddress calls=4
```

The pc **moved** (`0x005b0880` → `0x005b1180`), `functions_entered` rose 10543 → 10786,
`true_guest_entries` 13.25M → **52.85M** (4×), `ee_cycle` 498M → **2082M** (4.2×), `vblanks` 151 → 473.
That is monotone progress with the deadline in sight — the run is not stuck at that pc.

### The picture did NOT change

`w275r3long-capture.png` md5 `ad5a5c503111d04bf67d34522cc31c0c` is **byte-identical to R2's
`w275r2-capture.png`** and to `w275r2long-win-01.png`. Structural signature 1 colour / nonblack 0.1111
vs the reference's 14 / 0.1150 → the v5 structural test calls it the disclaimer. R3 is a CPU/behaviour
win, **not** a graphics win; nobody should read it as progress toward the picture.

### Law 8 — the capture check, RAW

```
$ cd tools/PS2Recomp && git diff --numstat
171	6	ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
9	0	ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
98	0	ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
28	7	ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
212	4	ps2xRuntime/src/lib/ps2_memory.cpp
158	5	ps2xRuntime/src/lib/ps2_runtime.cpp

$ ls -la tools/patches/ | grep r3
-rw-rw-r-- 1 or or 848 Oct  8 10:49 ps2recomp-linux-r3-sifgetreg-dispatch.patch
$ md5sum tools/patches/ps2recomp-linux-r3-sifgetreg-dispatch.patch
776ccc9a6119e4d4274922ade6f3255d  .../ps2recomp-linux-r3-sifgetreg-dispatch.patch

$ bash /mnt/ssd/tmp/law8check.sh
PASS  live_added=171  distinct=136  MISSING_from_patches=0  .../EeScheduler.cpp
PASS  live_added=9    distinct=7    MISSING_from_patches=0  .../Dispatcher.cpp
PASS  live_added=98   distinct=74   MISSING_from_patches=0  .../System.cpp
PASS  live_added=28   distinct=27   MISSING_from_patches=0  .../Thread.cpp
PASS  live_added=212  distinct=150  MISSING_from_patches=0  .../ps2_memory.cpp
PASS  live_added=158  distinct=111  MISSING_from_patches=0  .../ps2_runtime.cpp
```

Per-file, every one of the 9 live added `Dispatcher.cpp` lines is carried by the R3 patch (the patch's
added lines are a superset: 9 case lines + the hunk header). `MISSING_from_patches=0` for all six files.

### Handoffs (both are naming/telemetry, neither is a dispatch failure)

1. **`syscall_names.h` is STALE and nothing regenerates it.** `tools/harness/gen_syscall_names.py` parses
   `Dispatcher.cpp`'s cases into `ps2xRuntime/include/runtime/syscall_names.h` (gitignored, on disk dated
   **Sep 30 22:45**), and **`build_harness.sh` never calls it**. So a syscall we just correctly wired prints
   as `sce_unnamed_syscall` — which is what `0x7a` printed even while it was working. Fix: add the regen
   step to `build_harness.sh`, or the mapping will keep lying about newly-wired ids.
2. **The `Unimplemented PS2 syscall` line is misleading for 0x07.** ExecPS2 is handled in `System.cpp`
   after the warning, so R1's working ExecPS2 (and every future syscall special-cased there) is counted as
   an error. Anyone counting "unwired syscalls" from that grep over-counts by one per ExecPS2.

### The mechanical gate — RAW output (2026-10-08 11:01, `bash .auto/verify-dish.sh`)

```
=== VULCAN 4 dish gate — 2026-10-08 11:01 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275r3long-capture.png
      structural sig  : capture 1 colours / nonblack 0.1111   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 1 colours and a non-black fraction (0.1111) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275r3long-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275r3long.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 57
=== RESULT: MECHANICAL CLAIMS HOLD ===
GATE_EXIT=0
```

Law-8 capture check re-run at the same moment (the union of `tools/patches/*.patch` covers every live
added line of all six dirty files; the R3 patch carries the 9 `Dispatcher.cpp` lines):

```
PASS  live_added=171  distinct=136  MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
PASS  live_added=9    distinct=7    MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
PASS  live_added=98   distinct=74   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
PASS  live_added=28   distinct=27   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
PASS  live_added=212  distinct=150  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_memory.cpp
PASS  live_added=158  distinct=111  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_runtime.cpp
```

One note for the next seat: on arrival this window the R2 patch
(`ps2recomp-linux-r2-lookup-engine-table.patch`) had been trimmed in the working tree to the
`lookupFunction` hunk only, dropping its `hasFunction` weak-symbol guard hunk (209 lines). That guard is
carried by `ps2recomp-linux-w275-suite-fix.patch`, so coverage held either way; the committed R2 patch
was restored rather than the trim committed, because the trim deletes committed content without adding
anything. Nobody should trim one patch because another already carries the line — duplicate coverage is
free, and a patch that silently loses its own hunk is how 1,065 lines went missing on 2026-10-08.

## SEGMENT G — R4 (builder, 2026-10-08): syscall 0x0B wired, 0x07 un-logged, syscall_names.h regenerated — and the wall is NOT FindAddress

Handed: (1) wire EE syscall 0x0B, (2) stop 0x07 ExecPS2 logging as "unimplemented", (3) regenerate the
stale `syscall_names.h`. All three are in. Commit `035387f`. Raw gate `GATE_EXIT=0` and the raw law-8
capture check are pasted at the end of this segment.

### G.1 — Item 1: syscall 0x0B (AddSbusIntcHandler), decided from the caller, not from taste

Ground truth first (`/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`, `sub_010183B0`
@0x10183b0 — the only caller):

```
label_10183d8:
    // 0x10183d8: 0xc407c2c  jal func_101F0B0
    SET_GPR_U32(ctx, 31, 0x10183E0u);
    // 0x10183dc: daddu $a0, $s0, $zero (Delay Slot)
label_10183e0:
    // 0x10183e0: addiu $s0, $s0, 0x1
    // 0x10183e4: slti  $v0, $s0, 0x10      <-- OVERWRITES $v0 from the call
    // 0x10183e8: bnez  $v0, label_10183d8
```

A 16-iteration loop over `$a0` = 0..15 whose `$v0` is clobbered by `slti` two instructions after the
call. **The returned id is never read**, so `0` vs. a stable nonzero id is not observable by this
caller — the id is safe either way. Wired as `AddSbusIntcHandler` returning a stable per-cause id, and
it emits a law-2 `VULCAN 4 LIMITATION:` line on first registration of each cause:

```
VULCAN 4 LIMITATION: EE syscall 0x0B AddSbusIntcHandler(cause=N) registered with id N -- SBUS
interrupts are IOP-side and are never delivered to a recompiled EE; the handler will not run and
the cause will not be dispatched.
```

Files: `Dispatcher.cpp` `case 0x0B:` (+12/0 with the R3 block), `ps2_call_list.h` `X(AddSbusIntcHandler)`
(+1), `Interrupt.cpp` body (+33).

MEASURED, 45 s (new callkind line vs. R3 which had no such line):

```
  0x0b sce_AddSbusIntcHandler calls=16 last_pc=0x0101f0b8 from=1pc[0x0101f0b8] ra_count=0x010183e0x16 \
  arg=... a0=0x010183e0/a0=0x00000000x1,...,0x00000005x1,+10 a1=0x010183e0/a1=0x00000002x16
```

16 calls, one per cause 0..15, matching the static loop exactly. 16 LIMITATION lines on disk.

### G.2 — Item 2: 0x07 ExecPS2 no longer logs as unimplemented

0x07 is handled upstream (the harness calls `runtime->requestExecPS2()` and stops the guest), so its
presence in `TODO()` was producing a false "Unimplemented PS2 syscall" warning for a syscall that is
in fact implemented. `TODO()` now dispatches 0x07 before any reporting.

MEASURED, 45 s: `Unimplemented PS2 syscall` **17 → 0**; `[Syscall TODO]` **17 → 0**.
The ExecPS2 request still fires: `[execps2] entry=0x100008 gp=0x0 argc=2 argv=0x80075334`.

### G.3 — Item 3: `syscall_names.h` regenerated AND hooked into the build

The header was a generated artifact with no generator in the build: frozen at Sep 30 with 58 entries
while the dispatcher had grown to 114. Every syscall wired after Sep 30 therefore printed as
`sce_unnamed_syscall`, so **a wiring gap and a genuinely unknown syscall looked identical in the log** —
which is how 0x79/0x7A/0x78/0x77/0x2F (45,894 calls) and 0x07 read as unnamed in R3.

- `gen_syscall_names.py` rewritten as a line-based switch parser (handles `ps2_stubs::name(...)`,
  fall-through label groups, and `case static_cast<uint32_t>(-0xNN):`), plus an `EXTRA_NAMES` entry for
  0x07. It warns on stderr for any numbered label it cannot name.
- `build_harness.sh` now runs it unconditionally before compiling the generated unit, and **fails the
  build** (`exit 1`) if it errors. No timestamp check on purpose: a staleness check is the thing that
  failed here.

MEASURED, 45 s: `sce_unnamed_syscall` **7 → 0**; header 58 → **114** names; 0x79/0x7A/0x0B/0x07 all
print by name. Cosmetic residue: the harness prints `sce_` + the header name, so names already carrying
the prefix print doubled (`sce_sceSifSetReg`). The wiring is right; only the label prefix doubles.

### G.4 — 45 s vs 45 s, R3 → R4 (both 2,000,000 entry budget, 45 s deadline)

| metric | R3 (`boot_w275r3long.log`) | R4 (`boot_w275r4long.log`) |
|---|---|---|
| functions_entered | 10786 | 10790 |
| true_guest_entries | 52,847,539 | 53,510,043 |
| halt | `stuck_in_syscall` | `stuck_in_syscall` |
| halt pc | 0x005b1180 | 0x005b1188 |
| distinct_pcs | 217 | 217 |
| total_syscall_calls | 92036 | 92036 |
| syscalls (distinct) | 35 | 35 |
| missing_functions | 0 | 0 |
| frames_presented | 2525 | 2515 |
| vsync_tick | 423 | 428 |
| ee_cycle | 2,081,907,532 | 2,108,407,112 |
| intr_run | 249 | 252 |
| mmio_accesses | 2002 | 2012 |
| `Unimplemented PS2 syscall` | 17 | **0** |
| `sce_unnamed_syscall` | 7 | **0** |
| `[Syscall TODO]` | 17 | **0** |

**The unwired-syscall count is 0 new.** Every syscall the guest issues in this window now resolves to a
named handler. That did NOT move the halt.

### G.5 — Engine PCs: the recorded "31" is NOT reproducible, and the 8-byte halt move is a SAMPLER ARTIFACT

Two separate honesty items, both measured.

(a) **"engine PCs 27 → 31" could not be reproduced.** Counting distinct code addresses at syscall sites,
split by image (engine = 0x00100000..0x00617A14, main = everything else), from the `last_pc=`/`from=`
/`ra_count=` tokens: three token subsets give **21 / 25 / 59** engine PCs, none of them 31. On my own
reproducible metric (union of `from=` pcs, engine-vs-main by address) the figure is **engine=59,
main=124, total=183 — byte-identical between R3 and R4, and between the 15 s and 45 s logs.**
`0x0B` is issued from 0x0101f0b8, which is > 0x00617A14, i.e. the main image, so it is not an engine PC
by construction. **Treat 31 as unreproduced.** The lesson is the project's own rule, one more time:
a `functions_entered`-class number is a run-shape number, not an event.

(b) **The halt pc moving 0x005b1180 → 0x005b1188 is the checkpoint sampler landing on a different
instruction of the SAME 3-instruction loop.** Both addresses are inside one spin:

```
label_5b1180:
    // 0x5b1180: jal  func_5B0880
    // 0x5b1184: daddu $a0, $zero, $zero   (Delay Slot)
label_5b1188:
    // 0x5b1188: beqz $v0, 0x5b1180        <-- loops back
    // 0x5b118c: ld   $ra, 0x30($sp)       (Delay Slot)
```

and every one of the 301 `[Yield]` events is identical:

```
[Yield] n=12d source_pc=0x5b1180 target_pc=0x5b0880 fallthrough_pc=0x5b1188 kind=DirectCall ra=0x5b1188
```

298 in R3, 301 in R4. Nothing progressed.

### G.6 — THE WALL, NAMED: a busy-wait on one RDRAM word, `while (*(u32*)0x008869C0 == 0);`

The harness reports `blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x005b1188 -- this syscall
is the wall`, and that label is **one level too coarse**. Chasing FindAddress's return value is the
wrong chase; here is what the guest is actually doing:

`func_5B0880` (`ps2_recompiled_functions_41.cpp`, engine image) is a 5-instruction table lookup:

```
0x5b0880: lui   $v0, 0x0088
0x5b0884: sll   $a0, $a0, 2
0x5b0888: addiu $v0, $v0, 0x69C0        => $v0 = 0x008869C0
0x5b088c: addu  $a0, $a0, $v0
0x5b0890: jr    $ra
0x5b0894: lw    $v0, 0x0($a0)           (Delay Slot)
```

so `func_5B0880(0)` is `*(uint32_t *)0x008869C0`, and the loop at G.5(b) is:

```
while (*(uint32_t *)0x008869C0 == 0) { }
```

**The guest is waiting for a word at 0x008869C0 that nothing in this run ever writes.** That is the
wall, and it is a scheduling/producer wall, not a dispatch wall and not a return-value wall.

Supporting measurements:
- `0x83 sce_FindAddress calls=4` in the whole 45 s — four calls, not a livelock. Its last entry pc is
  0x005b7410, and two of the four calls return to 0x005b74ac / 0x005b74c0 — i.e. from inside the
  guest's OWN installed 0x83 override: `VULCAN4 SYSTABLE n=0x83 slot=0x1218c handler=0x5b73c8`,
  installed by `sce_SetSyscall` at 0x005b749c with `a1=0x5b7390`. The harness's `activeSyscallId()` is
  0x83 at the deadline because `handleSyscall(0x83)` has not returned — the guest is executing guest
  code inside that override, which is why the label is *correct but not specific*.
- `sub_005b0e30` (the function containing the spin) has **no static JAL caller** anywhere in either
  generated image — it is reached indirectly, consistent with a callback/override path. Not proven
  which; do not assume.
- The thread is not blocked: `thread_state=tid1:status=0:wait=none#0 ... pc=0x005b1188(running)`,
  `blocked_on_servicing=0`, `sleepCurrentCalls=14`, and the machine is alive — 2515 frames presented,
  428 vsyncs, 252 interrupt runs.

**Next seat: do not re-derive the wall from `halt=`. Start at `0x008869C0`.** Whoever is supposed to
produce that word is the missing piece — find the writer (it is a guest store, so the W253 byte-store
probe or a PCSX2 oracle watchpoint on 0x008869C0 answers it in one run).

### G.7 — the gate's "missing-function hits: 57" is NOT a regression

All 57 are pre-existing `VULCAN 4 LIMITATION: syscall 0xXX override handler 0xYYY has no generated
function in either image -- not invoking (was silent KE_ERROR)` lines: 48x (0x56, handler 0x800750c8),
6x (0x5b, 0x80076000), 2x (0x5a, 0x5b79f8), 1x (0x5a, 0x5b98d0). **Identical 57 in
`boot_w275r3long.log`.** No change, no new miss.

### G.8 — LAW 8 RAW (per-file, against the live `git diff --numstat`)

`tools/patches/ps2recomp-linux-w275r3-sbus-intc-and-syscall-names.patch`, 1508 lines, all 12 dirty files:

```
PASS  live_added=22   distinct=22   MISSING_from_patches=0  ps2xRecomp/src/lib/control_flow_emitter.cpp
PASS  live_added=32   distinct=32   MISSING_from_patches=0  ps2xRecomp/src/lib/instruction_translator.cpp
PASS  live_added=1    distinct=1    MISSING_from_patches=0  ps2xRuntime/include/ps2_call_list.h
PASS  live_added=90   distinct=60   MISSING_from_patches=0  ps2xRuntime/include/ps2_runtime_macros.h
PASS  live_added=57   distinct=57   MISSING_from_patches=0  ps2xRuntime/include/runtime/syscall_names.h
PASS  live_added=171  distinct=136  MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
PASS  live_added=12   distinct=9    MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
PASS  live_added=33   distinct=32   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Interrupt.cpp
PASS  live_added=141  distinct=109  MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
PASS  live_added=28   distinct=27   MISSING_from_patches=0  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
PASS  live_added=212  distinct=150  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_memory.cpp
PASS  live_added=158  distinct=111  MISSING_from_patches=0  ps2xRuntime/src/lib/ps2_runtime.cpp
LAW8_EXIT=0
```

`cd tools/PS2Recomp && git diff --numstat` for the same 12 files — every live added-line count is <=
the count the patch set carries:

```
22      0       ps2xRecomp/src/lib/control_flow_emitter.cpp
32      0       ps2xRecomp/src/lib/instruction_translator.cpp
1       0       ps2xRuntime/include/ps2_call_list.h
90      0       ps2xRuntime/include/ps2_runtime_macros.h
57      1       ps2xRuntime/include/runtime/syscall_names.h
171     6       ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
12      0       ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
33      0       ps2xRuntime/src/lib/Kernel/Syscalls/Interrupt.cpp
141     35      ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
28      7       ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
212     4       ps2xRuntime/src/lib/ps2_memory.cpp
158     5       ps2xRuntime/src/lib/ps2_runtime.cpp
```

### G.9 — MECHANICAL GATE RAW

`bash .auto/verify-dish.sh`, run on the committed tree at `035387f`:

```
=== VULCAN 4 dish gate — 2026-10-08 11:26 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w275r4long-win-14.png
      structural sig  : capture 1 colours / nonblack 0.1085   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 1 colours and a non-black fraction (0.1085) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w275r4long-win-14.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w275r4long.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 57
=== RESULT: MECHANICAL CLAIMS HOLD ===
GATE_EXIT=0
```

The screen is still the disclaimer — this segment claims no picture. Everything it claims is a wiring
and a measurement.

## SEGMENT H — R4 STATIC ANGLE (architect): every writer of 0x008869C0, and why none runs

The spin is `while (*(u32*)0x008869C0 == 0);` (loop 0x5b1180/0x5b1188, engine
`ps2_recompiled_functions_41.cpp:195611-195659`). `func_5B0880` reads `[0x008869C0 + idx*4]`
(`41.cpp:192227-192258`, read at `:192248`). Exhaustive search of the whole build tree
(`grep -rni 0x8869c0 /mnt/ssd/vulcan4-build/` → zero hits; `grep -rn 27072` on the engine → the
sites below) gives exactly **two EE stores** that can touch the word, and **neither writes non-zero
to slot 0**:

### H.1 — the two EE writers

| # | pc (store) | function | what it writes |
|---|---|---|---|
| 1 | **0x5b08b0** `sw $a1,0($a0)` | `func_5B0898` (inside `sub_005b07d0`) | `[0x008869C0 + idx*4] = a1` — the generic setter (`41.cpp:192284`; `lui 0x88`/`addiu 0x69C0` at `:192261-192272`) |
| 2 | **0x5b09b0** `sw $zero,0($v0)` | `sub_005b08c8` init loop | zeroes `0x008869C0..0x00886A3C` (32 words, descending) — reset only (`41.cpp:192570`) |

`func_5B0898` has **one** static caller in either image: `40.cpp:166636-166646`, the `J func_5B0898`
at **0x590a74**, with `a0=1, a1=0` → clears **slot 1** (`0x008869C4`), never slot 0. There is no
`lui 0x88`+`sw ..,0x69C0` immediate store anywhere (the two immediate siblings write *different*
bases: `07.cpp:21232` @0x1b2624 → `0x008269C0 = 3`, `15.cpp:170416` @0x2a0894 → `0x008369C0 = 0x1D` —
those are the 0x82/0x83 instances of the same table, status codes, NOT 0x88).

### H.2 — what 0x008869C0 is

Slot 0 of a **32-entry status/result table** in the 0x5B module's IOP-communication layer
(SIF RPC). Evidence: (a) `sub_005b08c8` builds a 32-byte DMA/SIF descriptor at 0x00886818 whose
field `[0x886834] = 0x8869C0` is a pointer **at** the table (`41.cpp:192481-192495`); the src/dst
pointers are `|0x20000000` (uncached KSEG1), classic DMA addressing; (b) the send primitive
`func_5B0DB0`/`sub_005b0c78` builds a SIF command header (`lbu` cmd id, `sll 8` size, `or`) and reads
the SIF flag `0x886820` (`41.cpp:193623-193628`), i.e. `sceSifSendCmd`; (c) the module's syscall
surface is SIF: 0x79 SifSetReg / 0x7A SifGetReg / 0x0B AddSbusIntcHandler (R3/R4 work), and the
guest's own 0x83 override (`SetSyscall a1=0x5b7390`; `0x5b7390` = word-copy, `0x5b73c8` = word-scan,
`42.cpp:29551`, `42.cpp:29627`).

Loop EXIT: `beqz $v0` at 0x5b1188 branches back while `v0==0`; **any non-zero** value of `0x008869C0`
ends it. That non-zero is the SIF RPC result/status for slot 0.

### H.3 — why none of the writers set it in our boot

- Writer 2 (init zero-loop) only ever writes 0 — it keeps the slot "pending".
- Writer 1 (`func_5B0898`) is the only EE setter and is never invoked with idx=0 (its sole caller uses
  idx=1, value 0).
- Therefore the non-zero value the spin waits on is written by the **IOP side** — the SIF0 (IOP→EE)
  DMA response to the `sceSifSendCmd` issued immediately before the spin at 0x5b1174
  (`41.cpp:195581`, `jal func_5B0DB0` with `a0=0x80000002`, packet at 0x886A80, size 0x10).
- Our boot runs **no IOP** (no BIOS, IOP modules not loaded, SIF RPC never answered), so the response
  never lands and `0x008869C0` stays 0 → the EE spins at 0x5b1180 forever.

### H.4 — confidence + the one check that settles it

High confidence on the writer inventory (exhaustive literal+offset grep of the tree). "IOP writes the
result" is an inference from the SIF structure; it is NOT a hand-waved claim — confirm it in one run:
PCSX2 oracle watchpoint on **0x008869C0** during GT4's own boot will show the store (IOP-side SIF0
DMA, not an EE instruction). That is the next seat's job. No fix written (per instruction).

---

## SEGMENT I — ORACLE VERDICT on the wall (the measurer's run, 2026-10-08T08:44:51Z)

One clean run: fresh `pcsx2-qt -debugger -fastboot "Gran Turismo 4 (USA) (v2.00).iso"` (pid 612359,
DebugServer 21512, DebugServer-verified). Section H's closing question — *"PCSX2 oracle watchpoint on
0x008869C0 will show the store (IOP-side SIF0 DMA, not an EE instruction)"* — is answered, and the
`not an EE instruction` half of it is **refuted**.

### I.1 — hardware: it IS written, by an EE instruction

| | hardware (measured) | ours |
|---|---|---|
| 0x008869C0 | `0x00000001` (slot 0 set) | never written |
| writing instruction | `sw v1,(v0)` @ **0x005b0868**, delay slot of `jr ra` @0x005b0864 | — |
| enclosing leaf | **func_5B0850** (0x005b0850-0x005b0868) | present, never invoked with idx=0 |
| caller | **ra = 0x005B0F50**, inside record-table dispatcher **sub_005B0E30** | — |
| value | **0x00000001** (idx=0, val=1 from frame 0x00081F20: +0x10=0, +0x14=1) | — |
| context | sp=**0x00081F20** = 0x81FC0-0xA0, GT4 kernel trampoline @**0x00081FE0**, gp=0, IRQs disabled | n/a |
| where the spin goes | exits at 0x005b1190 → `j 0x005AE0A0` (syscall 0x79, a0=0x80000002) | never reached |

- Spin entry (bp 0x005b1180) hit @cycle **1,787,808,870**, only **3 threads**: TID0 PC=0x00081fc0 st=2
  (idle) / TID1 PC=0x005adcb8 **st=1 RUNNING** — the spinner, sp=0x01FFFC60, ra=0x005B117C / TID4
  st=4 waitType=1. The write lands **+25,809 cycles later** @cycle **1,787,834,679**.
- The store computes `*((u32*)(*(u32*)(a1+0x1C) + *(u32*)(a0+0x10)*4)) = *(u32*)(a0+0x14)`; at the
  write a1=**0x00886818** (ctx), a1+0x1C=0x008869C0 (base), a2=0x008869C0, a0+0x10=**0** (idx),
  so dest v0 = base + idx*4 = **0x008869C0** (the flag word itself), value v1 = **1**.
- Proof the loop exits: 0x008869C0 read `0,0,0,0` pre-store; after single-stepping the delay slot it
  read **0x00000001**, so `beqz v0,0x005B1180` @0x005b1188 falls through.
- Proof hardware finishes init: end state **18 threads** (TID 0,1,4..16,18,19,20) @cycle
  2,550,854,656; **TID 1 left the loop** and now sits at 0x005adbc8 st=4 waitType=2 (a syscall wait).
  0x008869C0 = slot0 **1**, slot1 **1**, rest 0.

### I.2 — why ours spins (the named diff)

The flag is set from GT4's **asynchronous kernel handler dispatch**: trampoline **0x00081FE0**
(`lui sp,0x8 / jalr v1 / addiu sp,sp,0x1FC0 / li v1,0xFFFB / syscall`) → dispatcher **sub_005B0E30**
→ record table **0x00886840** (ctx+0x0C, count 0x20 at ctx+0x10) → leaf **func_5B0850** with frame
idx=0/val=1. **Nothing on the spin thread's own call chain writes it**, so no amount of scheduling
that thread alone can clear the loop: our runtime must *drive the dispatch* (deliver the event and run
the handler). The dispatcher has no static JAL caller in the image — it is reached only through the
trampoline, which is exactly why a static pass could not see it.

### I.3 — correction to SEGMENT H

- H's "the non-zero value ... is written by the IOP side — the SIF0 (IOP→EE) DMA response ... not an EE
  instruction" is **disproved**: the observed store is a plain EE `sw` in func_5B0850. What survives is
  only the *trigger* half — our boot runs no IOP, so the dispatch never fires.
- H's writer inventory was **incomplete**: it missed func_5B0850 (0x5b0868) as a writer of the table,
  and func_5B0870 (0x5b0878) writes **ctx+0x08**, not the table. Adding them makes the inventory
  exhaustive: init zero-loop (0) + func_5B0898 (idx/val setter) + **func_5B0850 (frame setter)**.

### I.4 — caveats (instrument, not game)

- The temp bp at 0x005b1190 (spin exit) **never fired** although TID 1 demonstrably reached the exit
  syscall (it now waits at 0x005adbc8). Instrument order — armed/resumed race — not a game fact.
- A pre-compaction note of mine said the destination was `0x008869C4`; the clean re-measure is
  **v0 = 0x008869C0** (idx 0, the flag word itself).
- `pcsx2_get_backtrace` **SIGSEGVs** this build — never call it.
- Oracle left **UP and clean** (pid 612359, all breakpoints/watchpoints cleared), settled at the
  18-thread state.

---

## R4 — the wall is DMAC channel-5 (SIF0) handler dispatch, not a syscall-override ordering bug (architect, 2026-10-08)

**The coordinator's hypothesis (point 2: "my case 0x79 builtin bypasses the engine's override") is REJECTED.**
- `[SetSyscall]` dump (boot_w275r4.log) shows NO override for 0x79 (or anything pointing at 0x5b0e30):
  `0x54->0x5b9e40, 0x55-0x59->0x80075038..0x800751a8 (blob), 0x5A->0x5b98d0, 0x5B->0x80075000 (blob),
  0x83->0x5b73c8`. 0x79 correctly reaches the builtin sceSifSetReg.
- `dispatchSyscallOverride` already runs BEFORE the builtin switch (`Dispatcher.cpp::dispatchNumericSyscall`).

**What the oracle's `sub_005B0E30 via trampoline 0x81FE0` actually is:** sub_005B0E30 is the engine's
**DMAC channel-5 (SIF0) handler**, registered at boot:
```
0x12 sce_AddDmacHandler  a0=5 a1=0x005b0e30  (ra=0x005b0a58)     [log 66948]
0x16 sce_EnableDmac      a0=5                 (ra=0x005ae8f0)     [log 66951]
```
The trampoline `0x00081FE0` is the EE-kernel interrupt/DMAC dispatch (the R1 DMAC gap, W275), NOT the
syscall SYSTABLE. On hardware the SIF0 (IOP->EE) transfer completes -> trampoline JALRs sub_005B0E30 ->
it calls func_5B0850@0x5b0850 (ra=0x5b0f50) -> `sw v1,0(v0)` @0x5b0868 writes 0x008869C0=1 -> the
`beqz v0,0x5b1180` loop exits.

**The concrete bug:** the SIF0 (IOP->EE) completion path `writeEeRange` (SIF.cpp:206, sole call site
`:425` inside `sceSifGetOtherData`) writes the payload into EE RAM but never dispatches the channel-5
DMAC handler. `dispatchDmacHandlersForCause(...,5u)` exists only in `sceSifSetDma` (SIF.cpp:753, the
EE->IOP/SIF1 direction). So sub_005B0E30 never fires, 0x008869C0 stays 0, the 0x5b1180 loop spins.

**Fix applied** (SIF.cpp, after the `writeEeRange` success in `sceSifGetOtherData`):
```cpp
if (runtime) { ps2_syscalls::dispatchDmacHandlersForCause(rdram, runtime, 5u); }
```
- Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r4-sif0-dmac-dispatch.patch` (reverse-apply OK).
- Rebuild: `cd /mnt/ssd/vulcan4-build && VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash /home/or/vulcan4/tools/harness/build_harness.sh`

**Separate unfixed wall (do not conflate):** `0x56 WaitEventFlag` hits
`override handler 0x800750c8 has no generated function` (48x). The engine's 0x55-0x59 event-flag syscalls
are implemented in a 0x330-byte blob (Copy 0x01036300 -> 0x80075000) whose handlers use `mtc0/sync/mfc0`
(the R5900 COP0 mailbox to GT4's own kernel). That is a second, independent wall (unrecompiled guest
kernel blob), not the R4 DMAC-dispatch one.

**Caveat:** `writeEeRange` emits no log without a store observer, so I could not statically confirm it
fires before the 15s halt. If the rebuild shows no change, the SIF0 trigger is a different path
(`IopHost::writeGuest` / ps2xIOP), and the next measurement is a `VULCAN4` store-watch on 0x8869c0's
writer.

## SEGMENT J — R4 RESULT (builder, 2026-10-08): the SIF fix is INERT, the spin is REAL, and the writer set is EMPTY

**Run shape (reproducible, two identical back-to-back 15 s runs `w276tab9a` / `w276tab9b`):**
```
functions_entered=10539 / 10538   true_guest_entries=12316818 / 12222919   true_guest_exits=0
halt=stuck_in_syscall   intr_queued=66  intr_run=43  intr_run_by_kind=40  gs_packets=15  frames_presented=859
detail=blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x005b0880 ra=0x005b1188
```
(The SCE-syscall label is the harness's *classifier*, not the mechanism — see below. Mechanism is the spin.)

**1. THE SIF0 FIX IS INERT (R4 premise REJECTED).**
`sceSifGetOtherData` is never called in the boot: `writeEeRange` never runs, so the new
`dispatchDmacHandlersForCause(...,5u)` at `Stubs/SIF.cpp:450` never fires. The only `dmac=1 cause=5`
dispatches come from the pre-existing call at `SIF.cpp:764` inside `sceSifSetDma` (2 calls, ra=0x5b0d8c).
Patch kept (it is correct for when that path does run) but it does **not** unblock R4.

**2. THE SPIN, read from the generated engine code (not inferred).**
- `func_5B0880` (`ps2_recompiled_functions_41.cpp:192228`, VA 0x5b0880, an interior entry of
  `sub_005b07d0_0x5b07d0` — `case 0x5b0880u: goto label_5b0880;`) is a 3-instruction leaf:
  `return *(uint32_t*)(0x008869C0 + (a0 << 2));`
- Its only caller is `label_5b1180` in `sub_005b0e30_0x5b0e30`, called with `a0 = 0` (delay slot
  0x5b1184 `daddu $a0,$zero,$zero`) → it polls **tab[0] at 0x008869C0** forever.
- `sub_005b0e30` spans **0x5b0e30 - 0x5b11c8** — i.e. the spin is INSIDE the DMAC channel-5 (SIF0)
  handler itself. The handler IS entered, not missing.

**3. DEFINITIVE WRITER SET FOR 0x008869C0..0x008869DF — 20 writes, ALL ZERO.**
A store subscriber (`VULCAN4_W276_TABWATCH=1`) that fires on **every** guest store *and* every host-side
range copy (`ps2TraceGuestRangeWrite` → `IopHost::writeGuest`/`zeroGuest`/RPC), printing the writer pc
and the `op` discriminator. Full list:
```
n=1..4    addr=0x008869c0/0x008869d0 val=0x00000000 writerPc=0x00100160 op=WRITE128   (guest boot clear)
n=5..20   addr=0x008869c0..0x008869dc val=0x00000000 writerPc=0x005b09b0 op=WRITE32   (engine table init, 8 words)
(+ a Ps2FastWrite* twin at writerPc=0 for each — same store via the fast path)
```
**Not one write carries a non-zero value.** No `SIF IOP-to-EE DMA` op, no `memcpy`/`memset` range copy,
ever lands in the region. At halt `tab[0..7]` is all `0x00000000`.

**4. THE PREDICTED WRITER IS NEVER REACHED.** The R4 doc predicted `func_5B0850@0x5b0850 -> sw v1,0(v0)
@0x5b0868 writes 0x008869C0=1` with `ra=0x5b0f50`. Measured: `0x5b0868` appears **0** times in the log
and `0x5b0f50` appears **0** times. So the branch of the handler that would publish the record is never
taken. Also note `func_5B0850` is address-computed, not constant: it writes
`*(uint32_t*)(a1->[0x1C] + (a0->[0x10] << 2)) = a0->[0x14]` — it only lands on 0x8869C0 if `a1->[0x1C]`
is `0x8869C0`, which is exactly the descriptor the engine built at 0x886834.

**5. PROBE HAZARD, MEASURED — do not call `getenv()` in a per-store hook.** The first narrowed-watch
build resolved its window bounds with a per-store `std::getenv()`; that single change moved the boot to
a *different path entirely* in 15 s (`functions_entered=10374`, `halt=stuck_in_syscall` at
**guest pc 0x01002218**, syscall 0x32 SleepThread, a second thread tid2, `descPtr@0x00886818=0`, and
**zero** in-region writes because the guest never reached GT4's SIF code). Caching the bounds into
`static const` restored byte-identical reproducibility across two runs. **Corollary for the crew: a
run that lands somewhere new may be the probe, not the guest — bisect the instrument before the guest.**

**6. Law-8 capture check (RAW, this session):** `cd tools/PS2Recomp && git diff --numstat` lists 13
files. Per-file patch-added-line comparison vs the live numstat printed **OK for all 13**
(`SIF.cpp live=11 patchAdded=11 ps2recomp-linux-r4-sif0-dmac-dispatch.patch`), final line `ALL-COVERED`.

**Segment J — mechanical gate RAW output (`bash .auto/verify-dish.sh`, commit 73dc33f):**
```
=== VULCAN 4 dish gate — 2026-10-08 12:30 ===
PASS  suite 497 tests, 0 failed
PASS  newest commit authored as the captain
PASS  working tree clean
INFO  verify-menu.sh: not passed (last lines below) — screen is not the menu yet
      newest capture : /mnt/ssd/vulcan4-build/run/w276tab9b-capture.png
      structural sig  : capture 1 colours / nonblack 0.1085   vs reference 14 / 0.1150
      GATE FAIL: STRUCTURAL MATCH to the disclaimer: only 1 colours and a non-black fraction (0.1085) within 0.05 of the reference (0.1150). That is dark-grey-text-on-black at some fade level - the disclaimer is STILL on screen, whatever the perceptual diff says.
INFO  newest capture: /mnt/ssd/vulcan4-build/run/w276tab9b-capture.png
INFO  newest log: /mnt/ssd/vulcan4-build/run/boot_w276tab9b.log
      halt=stuck_in_syscall
INFO  missing-function hits in that log: 57
=== RESULT: MECHANICAL CLAIMS HOLD ===
GATE_EXIT=0
```
The `verify-menu.sh` INFO is expected and correct: R4 is not the menu yet — the guest is blocked at
the 0x5b1180 spin, so the frame is GT4's own startup text, not a menu. This dish claims no picture.

**Segment J — law-8 capture check RAW output:**
```
$ cd tools/PS2Recomp && git diff --numstat     # 13 files, unchanged from the w275 capture
22     0   ps2xRecomp/src/lib/control_flow_emitter.cpp
32     0   ps2xRecomp/src/lib/instruction_translator.cpp
1      0   ps2xRuntime/include/ps2_call_list.h
90     0   ps2xRuntime/include/ps2_runtime_macros.h
57     1   ps2xRuntime/include/runtime/syscall_names.h
171    6   ps2xRuntime/src/lib/Kernel/EeScheduler.cpp
11     0   ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp
12     0   ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
33     0   ps2xRuntime/src/lib/Kernel/Syscalls/Interrupt.cpp
141   35   ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp
28     7   ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp
212    4   ps2xRuntime/src/lib/ps2_memory.cpp
158    5   ps2xRuntime/src/lib/ps2_runtime.cpp
$ python3 -I <per-file adder over tools/patches/*.patch>
OK  ps2xRecomp/src/lib/control_flow_emitter.cpp live=22 patchAdded=88
OK  ps2xRecomp/src/lib/instruction_translator.cpp live=32 patchAdded=128
OK  ps2xRuntime/include/ps2_call_list.h live=1 patchAdded=1
OK  ps2xRuntime/include/ps2_runtime_macros.h live=90 patchAdded=432
OK  ps2xRuntime/include/runtime/syscall_names.h live=57 patchAdded=128
OK  ps2xRuntime/src/lib/Kernel/EeScheduler.cpp live=171 patchAdded=1132
OK  ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp live=11 patchAdded=11
OK  ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp live=12 patchAdded=21
OK  ps2xRuntime/src/lib/Kernel/Syscalls/Interrupt.cpp live=33 patchAdded=33
OK  ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp live=141 patchAdded=924
OK  ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp live=28 patchAdded=56
OK  ps2xRuntime/src/lib/ps2_memory.cpp live=212 patchAdded=924
OK  ps2xRuntime/src/lib/ps2_runtime.cpp live=158 patchAdded=3274
ALL-COVERED
$ ls -la tools/patches/ps2recomp-linux-r4-sif0-dmac-dispatch.patch
-rw-rw-r--r-- 1 or or 1143 Oct  8 12:11 ps2recomp-linux-r4-sif0-dmac-dispatch.patch   <- SIF.cpp, 11 added lines
```

---

## R4 spin (corrected) — `0x5b1150` polls `SifGetReg(0x80000002)` which returns 0 → E2 (IOP/SIF0 ack), not a stub gap (architect, 2026-10-08)

The prior R4 SIF0-DMAC-dispatch fix is **reverted**: the coordinator measured `writeEeRange` never runs,
and the ch5 handler `sub_005b0e30` IS entered — the "never dispatched" hypothesis was wrong.

**Static trace of the actual spin** (`sub_005b0e30`, `ps2_recompiled_functions_41.cpp:195480`):
```
0x5b1144: lui   a0, 0x8000         ; a0 = 0x80000000
0x5b1148: jal   func_5AE0B0          ; SifGetReg (0x7A)
0x5b114c: ori   a0, a0, 0x2          ; a0 = 0x80000002   (delay slot)
0x5b1150: bnez  v0, 0x5b11b0         ; skip the spin iff v0 != 0
```
- `func_5AE0B0` (0x5ae0b0) = `addiu v1, zero, 0x7A; syscall 0` — the **SifGetReg (0x7A) thunk, N fixed**;
  the caller only supplies a0=0x80000002 (log 66926: `0x7a ... ra=0x005b1150 a0=0x80000002 a1=0x006dddf0`).
- The engine needs `SifGetReg(0x80000002) != 0` to take the `bnez v0 → 0x5b11b0` skip.
- Ours returns **0**: `SIF.cpp::seedDefaultSifRegsLocked()` seeds `g_sifRegs[kSifRegMsCom]=0`
  (`kSifRegMsCom = 0x80000002u`), and nothing in the runtime ever writes that key
  (`sceSifSetReg` only ever writes 0x80000000 and 0x80000001 in this boot). So v0=0 → the spin runs.

**What 0x80000002 is:** the SIF master-command register — the IOP's SIF0 response/ack to the EE, which the
engine polls after `SifSetReg(0x80000000, 0)` + `SifSetReg(0x80000001, 0x00886818)` (the RPC buffer setup).
Our IOP model never produces that ack.

**VERDICT — E2, not a stub gap (do not fake):** the runtime's IOP/SIF model must write a non-zero
acknowledgment to `g_sifRegs[0x80000002]` when it responds to the engine's SIF RPC init, so
`SifGetReg(0x80000002) != 0` at 0x5b1148. This is the SIF0/IOP-response modeling gap, not a return-value bug.

**Next wall (confirmed, not to be fixed now):** the unrecompiled TLB blob — `0x55-0x59 → 0x80075000`
(`0x56 WaitEventFlag` hits "override handler 0x800750c8 has no generated function" 48×); the runtime has no
TLB/COP0-mailbox model (W274 T2 §5). Still behind the current spin.

---

## R4 (runtime angle) — where the SIF RPC init ack belongs (design, NOT applied)

**The handshake, decoded.** GT4's sifcmd `SifInitRpc` low-level (inside the ch5 handler `sub_005b0e30`) runs the
standard SCE SIF-RPC init over raw registers:

| Register | W/R | Meaning | Value GT4 uses |
|---|---|---|---|
| `0x80000000` | EE writes | `SIF_RPC_ID` — RPC init command | `0` |
| `0x80000001` | EE writes | `SIF_RPC_SDATA` — EE send-buffer addr | `0x00886818` (EE RDRAM) |
| `0x80000002` | IOP writes / EE polls | `SIF_RPC_RDATA` — IOP receive-buffer addr | **the ack** |

The engine polls `sceSifGetReg(0x80000002)` at 0x5b1148 until non-zero. On real hardware the IOP's SCE sifcmd module
receives the SIF1 command, publishes its RPC receive buffer in IOP RAM, and writes that address back through SIF0 —
so `SifGetReg(0x80000002) != 0`.

**Why ours spins (no bridge exists).** The EE's `g_sifRegs` map in `SIF.cpp` is only touched by EE stubs.
Nothing on the IOP side can write it:
- `IopRpcBridge::dispatchSifManImport` / `dispatchSifCmdImport` (iop_rpc.cpp) fire **only on IOP import thunks**
  (IOP code calling sifman/sifcmd), never on an EE `SifSetReg`.
- `IopSubsystem::onSifTransfer → IopEmulator::onSifTransfer → IopRpcBridge::onSifTransfer` is a **documented no-op**
  ("The EE SIF transport owns the actual directional memory movement").
- `signalRpcCompletionSema` (RPC.cpp:145) signals an EE RPC-completion semaphore; `IopHost::writeGuest` writes EE RAM
  for DMA replies. Neither reaches `g_sifRegs[0x80000002]`.
- `iop_memory` models no `0x1000F200`/`0x1F803800` SIF register file at all.

So the ack **cannot** come from the IOP emulator today — it has no SIF register/DMA/interrupt path.

**FIX SITE: `ps2_stubs::sceSifSetReg` (SIF.cpp:763).** It is the only site that observes the init writes and owns
the reply key. Add, inside the existing `g_sifCmdStateMutex`-locked block after `g_sifRegs[reg] = value;`:
- on `reg == kSifRegSubAddr` (0x80000001) **and** `g_sifRegs[kSifRegMsCom] == 0u` (0x80000002 still unacked):
  `const uint32_t recvBuf = runtime ? runtime->allocateIopMemory(64u, 64u) : 0u;` then `g_sifRegs[kSifRegMsCom] = recvBuf;`

**ACK VALUE + justification.** The ack is the IOP's RPC receive-buffer address. We reply with the address of a buffer
**reserved from the runtime's own IOP allocator** (`allocateIopMemory(64,64)`; first allocation lands at
`IopMemory::HeapBase = 0x00120000`, non-zero and inside the 2 MB IOP RAM). It is not a hardcoded guess:
- the poll only requires non-zero (`bnez v0 → 0x5b11b0`), and the value must be a valid IOP RAM target for any later
  SIF0 reply DMA — an actually-allocated buffer satisfies both;
- it models what the real IOP sifcmd module does (publish *a* receive buffer); the specific address is an IOP-side
  implementation detail that even differs across game revisions, and it is **unknowable from the EE side**.
Re-arms naturally: `resetSifState`/`sceSifExitCmd` reseed `0x80000002 = 0`, so a re-init triggers the hook again.

**E2 sub-task (the honest long-term fix, not to be faked into this hook):** model a SIF command-register mailbox in
`IopEmulator`/`IopRpcBridge` so the IOP produces the ack itself (shared SIF register file + SIF1-in/SIF0-out +
interrupt), replacing the EE-side hook. Optionally measure the bit-exact hardware receive-buffer address with PCSX2
(break at 0x5b1148, read `v0` after the first non-zero `SifGetReg(0x80000002)`).

**Not applied / not committed** — design only, per instruction.

---

## R4 · MEASURER · the SIF "ack" — what reality does (PCSX2 DebugServer, 3 boots)

Task asked: the exact ack value(s) + timing + writer for `g_sifRegs[0x80000002]`, so the runtime can
synthesize it. Measurement says the premise is wrong: **reg 0x80000002 is not the ack surface, and the
value the runtime needs to produce is not a buffer address.**

### 1. Address of reg 0x80000002 — NOT MMIO

It is entry [2] of the 32-entry EE-kernel table `sif_regs[32]`, base **0x800212C0**, so reg2 lives at
**0x800212C8**:

- read:  `lw v0, 0x12C0(at)` @ **0x80006D68**  (`at = 0x80020000 + (reg<<2)`)
- write: `sw a1, 0x12C0(at)` @ **0x80006C98**, guarded by `sltiu v1,v0,0x0020` (index < 32)

The SIF MMIO windows (0x1000F000 / 0x1000F200) read back all zeros through this build — the register is
served from kernel RAM, not from the SIF block.

### 2. Writer of the initial 0 — the BIOS

Live disasm at **0x9FC00CBC**:

```
0x9fc00cbc: 3c04a008  lui   a0, 0xA008          ; end = 0xA0080000
...
0x9fc00cd0: 7ca20000  sq    v0, (a1)           ; v0 = 0 (por v0,zero,zero)
0x9fc00cd4: 24a50010  addiu a1, 0x10
0x9fc00ce4: 1440fffa  bnez  v0, ->0x9FC00CD0
```

a1 = 0xA00212C0 (uncached alias of the table), a0 = 0xA0080000. Caught by a write watchpoint on
0x800212C8 at EE cycle **8,115,350**, stored value **0**, caller ra = **0xbfc008d4**. The BIOS
zero-fills the *whole* sif_regs table.

### 3. What the engine's poll actually reads

Poll site: `jal SifGetReg` @ **0x5B114C**, result consumed at **0x5B1150** (`bnez v0` early-out;
slow path `jal 0x005B0DB0` send, then spin `jal 0x005B0880; beqz v0`).

| run | call#1 value | call#1 cycle | call#2 value | call#2 cycle | engine writes reg2=1 @0x5B11A8 |
|---|---|---|---|---|---|
| r4e | 1 | 1,641,908,483 | 0 | 1,787,808,418 | 1,787,834,936 |
| r4i | 1 | 1,635,282,239 | 0 | 1,779,588,415 | 1,779,616,230 |
| r4k | 1 | 1,630,360,856 | 0 | 1,774,661,942 | 1,774,688,525 |

**One value, not a sequence: 1 on the first poll, 0 on the second.** Absolute cycle stamps drift run
to run; the Δ (~26.6k cycles) is the stable quantity.

In r4k the BP at 0x5B11A8 was armed from before ELF entry and did **not** fire before call#1 — so the
engine did not write the 1 that call#1 sees. Writer of that first 1 is still unmeasured (BIOS only
zero-fills; 0x5B11A8 has not fired yet). **Open item.**

### 4. The real ack surface — RDRAM 0x008869C0

- read by **0x005B0880** (`v0 = *(0x008869C0 + index*4); jr ra`)
- cleared by the 32-iteration loop 0x5B09A0-0x5B09C8 and by `sq zero` @ 0x00100160
- written **1** by the store helper **0x005B0850**'s `sw v1,(v0)` @ **0x005B0868**
  (`lw v0,0x10(a0); lw a2,0x1c(a1); lw v1,0x14(a0); sll v0,v0,2; addu v0,a2; jr ra; sw v1,(v0)`)
- reached by the indirect **`jalr a2` @ 0x005B0F48** inside dispatcher **0x005B0E30**
  (SCE sifcmd low-level handler; handler table base **0x00886818**, stride 0x0C)

### 5. Writer / mechanism — an EE interrupt running the game's own handler

At the dispatcher entry the return address is **ra = 0x00081FEC**, **sp = 0x00081FC0** — i.e. control
came from `jalr v1` @ **0x00081FE4** (`addiu sp,0x1FC0` in the delay slot) and returns via
`li v1,-5; syscall` @ 0x00081FEC, the EE kernel **interrupt trampoline**.

r4hunt6 timeline: dispatcher entry cycle 1,787,896,200 → store helper 1,787,896,338 → engine writes
reg2=1 @1,787,896,535.

So the ack is **asynchronous, delivered by an EE interrupt ~26,600 cycles (~180 µs @ 147.456 MHz)
after the spin starts, and executed by the game's own SIF handler**. Not IOP DMA. Not a BIOS routine.

### 6. Consequence — do not write a fix, but here is what the fix must target

Nothing needs to be synthesized in `g_sifRegs[0x80000002]` to unblock the spin: the spin waits on
**RDRAM 0x008869C0 == 1**. The architect's SIF.cpp:763 hook (recvBuf = allocateIopMemory) targets the
wrong variable at the wrong site, and the "break at 0x5b1148, read v0 for a receive-buffer address"
sub-task is refuted — at 0x5b1148, a1 == gp == 0x006dddf0 in every measured hit, not a buffer address.

### 7. Instrument limits measured (for the next seat)

- An execution BP or a memcheck-break that fires inside EE BIOS/kernel context **cannot be resumed
  past**: the EE stays pinned at the same PC and cycle count forever (reproduced at 0x9fc00cd0 and
  0x80006c98). Removing the execution BP is the only way out; a memcheck cannot be removed without
  crashing.
- `remove_watchpoint` / `remove_memcheck` **double-frees and kills PCSX2** — `double free or
  corruption (fasttop)`, same family as `clear_all_breakpoints`.
- memcheck `action=log` emits **nothing** to stdout, never increments its hit counter, and pushes
  nothing to a persistent socket: a dead instrument.
- `pcsx2_step` hangs and conditional breakpoints never fire (pre-existing).

Logs: /mnt/ssd/tmp/pcsx2-r4i.log (crash), /mnt/ssd/tmp/pcsx2-r4j.log, /mnt/ssd/tmp/pcsx2-r4k.log.
Scratch instruments: /tmp/r4hunt4.py, /tmp/r4hunt6.py, /tmp/r4hunt7.py, /tmp/r4listen.py.
Raw: /tmp/r4w7.out, /tmp/r4poll.out, /tmp/r4k.out.

---

## R4 (reconciled) — `jalr a2 @0x5b0f48` → func_5B0850 is gated by the record TYPE byte, which is the IOP's SIF0 payload (E2) (architect, 2026-10-08)

Static trace of `sub_005B0E30` (0x5b0e30-0x5b11c8, `ps2_recompiled_functions_41.cpp`):

**The handler is baked in by the engine, not supplied by the IOP.** Init at 0x5b09cc-0x5b09ec populates the
record array `0x00886840`:
```
0x5b09e4: sw v0,0x6840(t1)   ; 0x00886840[0] = 0x005B0870
0x5b09ec: sw v1,0xC(a0)      ; 0x0088684C[3] = 0x005B0850   <- func_5B0850
0x5b09f0: sw s1,0x10(a0)     ; 0x00886850[4] = 0x00886818
```

**The dispatch chain** (`jalr a2` @0x5b0f48):
```
0x5b0e48: lw  a3, 0x6818(v1)   ; a3 = *(0x00886818) = current-record pointer
0x5b0e4c: lbu v0, 0(a3)        ; v0 = record type byte
0x5b0e50: andi a1, v0, 0xff    ; a1 = type
0x5b0e54: beqz a1, 0x5b0f64    ; <-- DECIDING GATE: type==0 -> return, no dispatch
0x5b0ed8: lw  v1, 0xC(s1)      ; v1 = *(0x00886824) = 0x00886840 (record array)
0x5b0ee0: addu v0, a1, v1      ; v0 = array + type*stride
0x5b0ee4: lw  a2, 0(v0)        ; a2 = handler (func_5B0850 for type 3)
0x5b0ee8: beqz a2, 0x5b0f58    ; handler==0 -> skip
0x5b0ef0: lw  v1, 8(v0)        ; gp = record gp
0x5b0f48: jalr a2              ; -> func_5B0850
0x5b0868: sw  v1, 0(v0)        ; (inside func_5B0850) -> 0x008869C0 = 1
```

**The deciding register is a1 (record type byte).** It is the first gate and the only one that is DYNAMIC —
the handler pointers are baked into `0x00886840`. The record (and its type byte) is the **IOP's SIF0
payload**: the engine set the sub-buffer address `SifSetReg(0x80000001, 0x00886818)` and the IOP writes the
command record there over SIF0. Our IOP model never delivers it, so the type byte stays 0, `beqz a1` returns
early, `func_5B0850` never runs, `0x008869C0` stays 0, and the 0x5b1180 spin never exits.

**VERDICT — E2 (IOP SIF0 response modeling), not a stub gap; do not fake.** Precise enabler: the runtime's
IOP/SIF model must write a non-zero command record (a type byte != 0, with the 12-byte record fields the
dispatcher reads) to the buffer at `0x00886818` on the engine's SIF init, so `sub_005B0E30`'s `jalr a2`
reaches `func_5B0850` and sets `0x008869C0=1`.

Also reconciled with the oracle: my earlier `SifGetReg(0x80000002)` reading was refuted — 0x80000002 is
entry[2] of the kernel table at 0x800212C0 (→ 0x800212C8), read by `lw v0,0x12C0(at)` @0x80006D68, and the
game's own code writes 1 there @0x005B11A8. It is not SIF MMIO.

---

## R4 ORACLE — the exact SIF0 record, measured on PCSX2

Objective: capture the byte-for-byte record the IOP delivers to the engine's receive buffer, its type, and
the handler it selects. Instrument: PCSX2 DebugServer build, fresh `-debugger -fastboot` boot, GT4 USA v2.00.
All figures are hardware events (PC / cycle / memory), never frame or function counts.

### Correction to the model: 0x00886818 is a CTX struct, the record is at 0x00886740

`SifSetReg(0x80000001, 0x00886818)` installs a 0x60-byte context struct. `ctx+0x00` is the *current-record
pointer*, not the record. Measured (u32 @0x00886818):

```
+00 = 0x20886740  current record ptr (uncached mirror of 0x00886740)
+04 = 0x208867c0  next record ptr  (ring of 2 buffers, 0x80 apart)
+08 = 0x0001e640
+0C = 0x00886840  table B base
+10 = 0x00000020  table B count = 32
+14 = 0             table A base   (empty)
+18 = 0             table A count  (empty)
+1C = 0x008869c0  FLAG base
+20 = 0
+24 = 0
```

### Correction to the model: table B stride is 12 bytes, fields {handler, arg, gp}

`0x00886840 + 12*k`. So `0x0088684C` is entry **1** (not "entry 3") and `0x00886850 = 0x00886818` is entry 1's
**arg**, not a handler. 32 entries x 12 = 0x180 bytes, ending exactly at the flag base (`0x00886840+0x180 =
0x008869C0`). Full populated dump:

```
[0]  = { 0x005b0870, 0x00886818, 0x00000000 }
[1]  = { 0x005b0850, 0x00886818, 0x00000000 }
[8]  = { 0x005b1328, 0x00888240, 0x006dddf0 }
[9]  = { 0x005b1700, 0x00888240, 0x006dddf0 }
[10] = { 0x005b1920, 0x00888240, 0x006dddf0 }
[12] = { 0x005b1438, 0x00888240, 0x006dddf0 }
[17] = { 0x005b23b8, 0x00889a00, 0x006dddf0 }
[18] = { 0x005803e0, 0x00000000, 0x006dddf0 }
[19] = { 0x005b28a8, 0x00889a40, 0x006dddf0 }
[28] = { 0x00590b80, 0x00885ac0, 0x006dddf0 }
```
(indices 2-7, 11, 13-16, 20-27, 29-31 are zero.)

### The record — byte-for-byte, captured at the 0x5b0e4c gate

Breakpoint at `0x5b0e4c` (the `lbu v0,0(a3)` that reads the type), condition `[a3+8] == 0x80000001`, 64 bytes
at 0x00886740, pristine (before the dispatcher clears byte 0):

```
00886740  18 00 00 00 00 00 00 00  01 00 00 80 00 00 00 00
00886750  00 00 00 00 01 00 00 00  01 00 00 80 00 00 00 00
00886760  0a 00 00 80 00 00 00 00  00 00 00 00 00 00 00 00
00886770  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
```

As u32 LE — this is the message the dispatcher consumes:

| off | value | meaning |
|---|---|---|
| +0x00 | `0x00000018` | **TYPE byte = 0x18** (length; nonzero -> gate passes) |
| +0x04 | 0 | |
| +0x08 | `0x80000001` | dispatch word: bit31 set, idx = 1 |
| +0x0C | 0 | |
| +0x10 | `0x00000000` | flag index |
| +0x14 | `0x00000001` | value to store |
| +0x18 | `0x80000001` | (next entry / leftover) |
| +0x1C | 0 | |
| +0x20 | `0x8000000a` | (next entry) |

Only the first 0x18 bytes are this message. **The type byte is a LENGTH, not an index** — the dispatcher
copies `ceil(type/16)*16` bytes to the stack (0x18 -> 0x20, 0x40 -> 0x40). The handler index comes from the
separate word at msg+0x08.

### Dispatch -> handler

`idx = (msg+0x08) & 0x7FFFFFFF = 1` -> `table B[1] = 0x005B0850`, arg `0x00886818`, gp 0.

Call convention (measured, and the reverse of the earlier guess): **a0 = MESSAGE pointer, a1 = table-entry
ARG.** `func_5B0850`:

```
0x5b0850: lw  v0, 0x10(a0)   ; v0 = msg[0x10] = 0   (flag index)
0x5b0854: lw  a2, 0x1C(a1)   ; a2 = ctx[0x1C] = 0x008869C0  (flag base)
0x5b0858: lw  v1, 0x14(a0)   ; v1 = msg[0x14] = 1   (value)
0x5b085c: sll v0, v0, 2
0x5b0860: addu v0, a2
0x5b0864: jr  ra
0x5b0868: sw  v1, 0(v0)      ; *(0x008869C0 + idx*4) = value
```

Empirical confirmation at PC=0x005b0864 (cycle 1,787,896,355): v0=0x008869C0, v1=0x00000001,
a0=0x00081F20 (msg copy on the stack), a1=0x00886818, a2=0x008869C0, ra=0x005B0F50, gp=0. After execution
0x008869C0 read `0x00000001`, and the 0x5b1180 spin **exited** — the emulator ran forward from cycle
1,787,896,355 to 4,246,064,214.

The spin is `while (func_5B0880(0) == 0);` — `func_5B0880(a0)` = `*(0x008869C0 + a0*4)`.

### Provenance: the record BODY is written by a non-EE agent (IOP SIF0 DMA)

Two write watchpoints on the record buffer:

- `0x00886740..0x00886780`: **5 hits, all at PC=0x005b0e78** — the dispatcher's own `sb zero,(a3)` clearing
  the type byte. Exactly one EE write per dispatch.
- `0x00886748..0x00886760` (the body, which the dispatcher never writes): **0 hits, ever.**
- Yet the body content changed between two consecutive dispatches (read A `40400200 403f8700 .. 003f8700`;
  read B `40400000 00cb8600 .. 44ca8600`) with no EE write observed.

Conclusion: the EE only clears byte 0 of the record; the message body arrives by DMA (SIF0), which PCSX2's
EE write-watchpoints do not observe. This is the hardware-event confirmation of the E2 verdict.

The dispatcher runs in asynchronous kernel/interrupt context: the breakpoint's ra = `0x00081FEC`, inside the
EE callback trampoline at `0x00081FE0` (`lui sp,0x0008 / jalr v1 / addiu sp,sp,0x1FC0 / li v1,-5 / syscall`),
consistent with a DMA-completion interrupt.

### The enabler shape (for whoever builds it — no fix written here)

The ring is drained message by message; each message carries its own type and dispatch index. A single
message (idx 1) unblocks THIS spin, but the drain must deliver *all* of them or flags go missing. A second,
different message was captured earlier at the same buffer (cycle 1,641,919,852):

```
+00 = 0x40        (length 64)
+08 = 0x80000008  -> table B[8] = 0x005B1328, arg 0x00888240
+14 = 0x20886a40
+1C = 0x008899c0
+20 = 0x80000009
+24 = 0x00047e88
+28 = 0x00047ed0
```

### Instrument notes (corrections to the board's limits list)

- `remove_breakpoint` is SAFE (used repeatedly, PCSX2 alive). Only `remove_watchpoint` /
  `clear_all_breakpoints` kill it.
- Conditional EE breakpoints with a memory operand WORK: `[a3+8] == 0x80000001` fired correctly.
- Watchpoints on RDRAM fire for EE writes and their hit count is reliable.
- `pcsx2_step` inside kernel context times out (does not advance) — read registers at the BP instead.

No fix was written; this is the oracle measurement only.

---

## R4 E2 fix — SIF0 response record delivery (oracle-measured record, not a fake) (architect, 2026-10-08)

Implemented in `ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp` `sceSifSetReg`. Trigger: the engine calls
`SifSetReg(0x80000001, 0x00886818)` (installing the SIF RPC receive-buffer address). Delivery:
```
RDRAM[0x00886818] = 0x20886740        (record pointer, KSEG0 alias)
RDRAM[0x00886740..0x0088677F] = 64-byte record:
  +0x00 = 0x18          (type byte)
  +0x08 = 0x80000001    (dispatch word)
  +0x10 = 0x0           (func_5B0850 store index)
  +0x14 = 0x1           (func_5B0850 store value)
  rest  = 0
```

**Why this exact record** (static trace, `ps2_recompiled_functions_41.cpp`):
- `sub_005B0E30` 0x5b0e4c reads type byte; `beqz a1` @0x5b0e54 is passed (0x18 != 0).
- 0x5b0eac reads dispatch word; 0x5b0ec8 `a1 = 0x80000001 & 0x7FFFFFFF = 1`; 0x5b0ed8-0x5b0ee4 computes
  `a2 = *(0x00886840 + 1*12 + 0) = *(0x0088684C) = 0x005B0850` (func_5B0850, baked in by init 0x5b09ec).
- 0x5b0f48 `jalr a2` with `a0=sp` (record copy), `a1=*(0x00886850)=0x00886818` (the CTX).
- `func_5B0850` 0x5b0850: `v0=sp[0x10]` (index), `a2=*(0x00886818+0x1C)=*(0x00886834)=0x008869C0`
  (base), `v1=sp[0x14]` (value); 0x5b0868 `sw v1, 0(base + index*4)` → `0x008869C0 + 0 = 0x008869C0 = 1`.
- `0x008869C0=1` is the ack the engine's 0x5b1180 spin (`jal 0x5b0880; beqz v0,loop`) polls.

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r4-sif0-record.patch` (+44/-0, reverse-apply OK).

Rebuild (coordinator directs): `cd /mnt/ssd/vulcan4-build && VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash /home/or/vulcan4/tools/harness/build_harness.sh`

Expected next wall (unfixed, behind this): the unrecompiled TLB blob — `0x55-0x59 → 0x80075000`
(`0x56 WaitEventFlag` 48× "override handler 0x800750c8 has no generated function"); runtime has no
TLB/COP0-mailbox model (W274 T2 §5).

---

## R4 E2 result — SIF0 record delivery WORKS; next wall = `missing_function` @0x005b0850 (architect, 2026-10-08)

Rebuilt (`VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 build_harness.sh`) and booted 15s
(`boot_w275r4e2.log`) + 45s (`boot_w275r4e2long.log`).

| | R4 (before) | R4 E2 (after) |
|---|---|---|
| halt | `stuck_in_syscall` (0x5b1180 spin) | `missing_function` @ **0x005b0850** |
| functions_entered | 10547 | 10443 |
| distinct_pcs | 217 | 208 |
| frames | 846 | 342 / 329 |
| intr_run | 111 | 62 |

The SIF0 record delivery is confirmed: `sub_005B0E30`'s `jalr a2` @0x5b0f48 now reaches **func_5B0850
(0x005B0850)** (the halt detail shows `pc=0x005b0850 ra=0x005b0f50`, i.e. the dispatch fired). But
0x005B0850 has no generated function: it is an INTERIOR entry point of `sub_005b07d0` (0x5b07d0) whose
switch cases are `0x5b0808/0x5b082c/0x5b083c/0x5b0880/0x5b0898` — `0x5b0850` is absent, and it is not in
`g_ps2EngineFunctionTable` (register_functions.cpp registers only 0x5b083c and 0x5b0880 for that function).
It is reached only by the runtime-computed `jalr a2` (a2 = record-array entry `0x0088684C`), so the
recompiler could not statically discover it as a jump target.

**Next wall — tool-input dispatch miss:** add `0x005b0850` as an entry point (a switch-case label of
`sub_005b07d0`) in the engine recompilation — TOML `entry_points` / analyzer function list — and regenerate
`recomp_engine_w251`. The TLB blob wall (`0x56 WaitEventFlag` 48×, 0x55-0x59 → 0x80075000) is still BEHIND
this new wall; the boot does not reach it yet.

---

## R5 — 0x005b0850 dispatched (entry_point fix); next wall = `guest_blocked` sema5 (TLB blob dependency) (architect, 2026-10-08)

**Fix:** added `"func_5B0850@0x005B0850"` to the engine `entry_points` in
`/mnt/ssd/gt4/work/w251-engine_authoritative.toml`, regenerated
(`PS2RECOMP_ENTRY_ADDR_CSV=/mnt/ssd/vulcan4-build/engine-symbols.csv
PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable ps2_recomp w251-engine_authoritative.toml`), recompiled
(build_engine.sh, 47 units), relinked. Verified: `case 0x5b0850u: goto label_5b0850;` in sub_005b07d0 and
`g_ps2EngineFunctionTable[1229330] = sub_005b07d0_0x5b07d0; // 0x5b0850`.

**Boot result** (15s `boot_w275r5.log`, 45s `boot_w275r5long.log`):
- 0x005b0850 now dispatches (the `missing_function` wall is gone).
- functions_entered=10451 (was 10443), distinct_pcs=214, frames 337/350.
- halt=`guest_blocked`: tid1 parked in `WaitSema(sema5)` @0x005adce8 ra=0x005b18b4 —
  "no other thread is runnable ... wakeup we do not yet deliver".
- `0x56 WaitEventFlag` LIMITATION is **still 48×** (`0x55-0x59 → 0x80075000` unrecompiled).

**Next wall (the one the coordinator flagged):** the unrecompiled TLB blob. The engine's event-flag
syscalls (0x55 iClearEventFlag, 0x56 WaitEventFlag, 0x57 PollEventFlag, 0x58 iPollEventFlag,
0x59 ReferEventFlagStatus) are blob-implemented (`mtc0/sync/mfc0` COP0 mailbox, blob copied
0x01036300 → 0x80075000) and have no generated function. The engine's event-flag/semaphore signal that
would wake `sema5` depends on that machinery, so the main thread deadlocks. Requires a TLB/COP0-mailbox
model for 0x55-0x59 (W274 T2 §5), not a stub.

---

## R6 — TLB stubs (0x55-0x59) implemented; 0x56 LIMITATION gone, but sema5 STILL deadlocked (architect, 2026-10-08)

**Fix:** `ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp` — added a 48-entry shadow TLB and
`emulateGuestTlbOp` (called from `dispatchSyscallOverride` before the `hasFunction` gate) emulating the
blob's 0x55-0x59 COP0 round-trip:
- 0x55 TLBWR(PageMask,EntryHi,EntryLo0,EntryLo1) → v0 = slot | -1 (segment guard `(EntryHi>>24)&0xF0 ∈ {0x10,0x20,0x30,0x40,0x50}`).
- 0x56 TLBWI(Index<0x30,PageMask,EntryHi,EntryLo0,EntryLo1) → v0 = Index | -1.
- 0x57 TLBR(Index) → PageMask→(a1), EntryHi→(a2), EntryLo0→(a3), EntryLo1→(t0).
- 0x58 TLBP(EntryHi) → v0 = Index | -1; hit → PageMask→(a1), EntryLo0→(a2), EntryLo1→(a3).
- 0x59 composite probe (approximate: EntryHi == vaddr).

**Boot result** (15s `boot_w275r6.log`):
- `0x56` "no generated function" LIMITATION count: **48 → 0** — the stubs intercept.
- But halt is **unchanged**: `guest_blocked`, tid1 parked `WaitSema(sema5)` @0x005adce8, ra=0x005b18b4,
  functions_entered=10451 (same as R5), distinct_pcs=214, intr_run=64.

**Conclusion:** the TLBWI return-value change (KE_ERROR −1 → index) did not alter the guest flow, so the
TLB blob was **not** the sema5 blocker. The wall is the sema5 wakeup itself — "an interrupt or a wakeup we
do not yet deliver": `intr_run=64` but the interrupt handler that should signal sema5 is not doing so.
The TLB stubs are a necessary step (0x55-0x59 is now emulated, not a LIMITATION) but the sema5 deadlock is
a separate, still-open wall.

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r6-tlb-stubs.patch` (+285/−35 full System.cpp diff,
reverse-apply OK — carries the R6 TLB stubs on top of the pre-existing W275 probes in that file).

## R6 static angle — sema5 wakeup source (SignalSema(5) never fires)

### 1. Where tid1 blocks (the wall)
`sub_005b17d0` (0x5b17d0–0x5b19b0, engine file 41) is an async-op completion-wait:
- `0x5b184c jal func_5ADCA0` = CreateSema (struct on stack, init_count 0) → id stored at `0x8($s1)`.
- `0x5b1884 jal func_5B0DB0` = the async op (SIF0/DMA send).
- `0x5b18ac jal func_5ADCE0` = WaitSema, `a0 = lw 0x8($s1)` = **sema id 5**.
  cite: `ps2_recompiled_functions_41.cpp:198397` (jal), `:198403` (a0 load). Parked pc=0x5adce8, ra=0x5b18b4.
Sema5 = count 0, 1 waiter (boot report). Only tid1 exists → the wake must come from an INTERRUPT.

### 2. SignalSema(5) sites — none fire
- Engine SignalSema stub = `func_5ADCC0` (0x5adcc0, `li v1,0x42`), `ps2_recompiled_functions_41.cpp:173207`.
- **158** call sites (`jal/j func_5ADCC0`) across files 32/38/39/40/42. **Every one loads a0 from a memory
  slot** (`READ32(base+off)`); **none** loads literal 5. The sema id is a runtime struct-field value.
- Proof none fired: boot log `0x42 sce_SignalSema calls=13` lists ra ONLY in 0x0100xxxx (kernel) — zero
  0x005xxxxx (engine) call sites executed. The 2 `SignalSema(5)` (ra=0x0100b1a0, kernel `sub_0100B108`) are a
  boot handshake, consumed by the 2 kernel `WaitSema(5)` (ra=0x0100b144) BEFORE tid1 ever waits.

### 3. The interrupt handler that should wake sema5 — and the FALSE guard
Engine registers two interrupt handlers (boot log AddIntcHandler/AddDmacHandler):
- **INTC line 0xb → `sub_005b8158`** (0x5b8158–0x5b8450, file 42). FIRST action after prologue:
  `lw v0,0x0(0x10001010); andi v0,0x400; beqz v0 → 0x5b8308` (early return).
  cite `ps2_recompiled_functions_42.cpp:34643` (load), `:34647` (mask), `:34651` (branch).
  0x10001010 = **EE Timer 2 MODE** (`kEeTimerBases[2]=0x10001000`, mode offset 0x10 —
  `src/lib/ps2_memory.cpp:199-206`); 0x400 = bit 10 = **EQUF** (compare-match flag,
  `kEeTimerModeEquf = 1<<10`, `ps2_memory.cpp:216`).
- **DMAC line 5 (SIF0) → `sub_005b0e30`** (0x5b0e30, file 41).

**Why neither fires (missing state):**
- Boot report `inv_by_kind=[intr=52,dmac=0,override=0,other=0]`, `irq_attach=0 irq_runsite=0 irq_done=0` —
  the harness raises 52 synthetic VBLANKs and **zero DMAC (SIF0) interrupts**, and never attaches/runs a
  guest IRQ handler.
- `sub_005b8158`'s guard reads **Timer 2 EQUF (0x10001010 & 0x400)**; the harness never advances Timer 2
  to its compare value, so EQUF stays 0 and the handler bails at `0x5b8194 → 0x5b8308` every entry.

**Net:** sema5's wake is an interrupt-driven completion signal (SIF0 DMAC, or the Timer-2 frame handler).
The word that is never set = `READ32(0x10001010) & 0x400` (Timer 2 EQUF) — and the DMAC interrupt source is
never delivered at all (`dmac=0`). No fix written (static analysis only).

---

# R6 ORACLE ANGLE — what REALITY does (PCSX2 DebugServer + Ghidra)

**Date 2026-10-08.** Seat: measurer (the oracle). Instrument: PCSX2 `-debugger`, DebugServer :21512,
game `Gran Turismo 4 (SCUS-97328)`, PID 858872, log `/mnt/ssd/vulcan4/tmp/pcsx2-r6-b5.log`.
Every number below was read from the live oracle; the source is named for each. **No fix is written here.**
Mission text as handed to this seat: *"name the sema5 signaller on hardware — deliverable: the SignalSema(5)
caller (pc + ra + thread) on hardware, and the condition that gates it."*

## 0. THE ANSWER IN ONE LINE

On hardware there is **no `SignalSema(5)`**: id 5 is never used. The semaphore the recomp calls "sema5" is a
recomp-local dense id, and its hardware counterpart is signalled from **interrupt/kernel context** — pc
`0x00557AF0`, ra `0x00081FEC` (the kernel exception vector's own return), thread = the kernel interrupt
stack (`sp = 0x00081FC0`), never a game thread. The signal is gated by a **two-stage** check: the handle
validator at `0x00578290`, then **CP0 Status bit 0** at `0x005784AC`.

---

## 1. Hardware semaphore ids are dynamic — "sema5" does not exist

Measured ids at the stubs `0x005ADCE0` (WaitSema) / `0x005ADCD0` (iSignalSema) / `0x005ADCC0` (SignalSema)
across this and prior sessions: **4, 7, 17, 26, 30, 31, 40, 41**. A probe with `a0 == 5` at every stub
never fired in any session.

The EE kernel assigns semaphore ids densely from its own allocator; they are therefore a property of the
**run**, not of the program. The recomp's id 5 comes from `EeScheduler::allocatePositiveId`
(`m_nextSemaphoreId = 1`) — a recomp-local counter. **The two numbers are in different id spaces and must
not be compared.** The hardware equivalent of the recomp's "sema5" is the semaphore the park site creates
for itself (next section).

## 2. The park site is reached on hardware, and its WaitSema argument is dynamic

- BP at `0x005b18ac` (the `jal 0x005ADCE0` WaitSema) **fired** at cycles 2014530152. `s1 = 0x008899C0`,
  `ra = 0x005B188C`. (Disproves any doubt that hardware never executes this site.)
- The argument is **not a constant**: `[s1+8]` read `0x1E` (30) on the first invocation and `0x1F` (31) on
  the second. The id is created by the same function — `jal 0x005ADCA0` (CreateSema) at `0x005b184c`,
  return stored at `0x005b1858` — with the RPC name `"SceSifrpcBind"` (`0x006D25D8`).
- Confirmed at the stub `0x005ADCE0`: `a0 = 0x0000001F (31)`, `ra = 0x005B18B4`, `s1 = 0x0086CC84`,
  `k0 = 0x70030C13`.

So the recomp's constant `WaitSema(5)` corresponds to a **freshly created id per call** on hardware.

## 3. The signaller: pc + ra + thread (measured twice, byte-identical)

**pc = `0x00557AF0`.**

Handler entry (BP at `0x00557AF0`, two independent captures):

| reg | value | meaning |
|---|---|---|
| `v1` | `0x00557AF0` | the `jalr v1` target of the kernel exception vector |
| `ra` | `0x00081FEC` | **the vector's own return address** — entered directly, no game caller |
| `sp` | `0x00081FC0` | **the kernel interrupt stack** (`lui sp,0x0008` + delay slot `addiu sp,sp,0x1FC0`) |
| `k0` | `0x70030C12` | CP0 Status mirrored into k0 at interrupt entry |
| `a0` | `0x00000002` | the dispatcher's index/channel at entry |
| `a1` | `0x00000129` | **the semaphore handle, supplied by the dispatcher** |
| `a3` | `0x00000129` | same handle, mirrored |

Body (live disasm, 11 instructions):

```
0x00557af0: addiu sp, -0x10
0x00557af4: sd    ra, (sp)
0x00557af8: jal   ->0x00578480      ; GT4's signal-a-semaphore helper
0x00557afc: dmove a0, a1            ; DELAY SLOT: the handle is the handler's own a1
0x00557b00: SYNC
0x00557b04: ei
0x00557b08: dmove v0, zero
0x00557b0c: ld    ra, (sp)
0x00557b10: jr    ra
0x00557b14: addiu sp, 0x10
```

The signal it issues (BP at `0x005ADCD0`, identical in both captures):

- `a0 = 0x00000029` → **semaphore id 41** (handle `0x129`, validator index `0x29`)
- `ra = 0x005784C8` (inside the helper), `sp = 0x00081F90`, `k0 = 0x70030C12`, `s0 = 0x29`

Stack proof that the caller is `0x00557AF0` (read live at the stub, `sp = 0x00081F90`, 64 B):

```
[0x81F90] = 0x80019160   ; helper's saved s0
[0x81FA0] = 0x00557B00   ; helper's saved ra  -> 0x00557AF0 + 0x10  == THIS handler
[0x81FB0] = 0x00081FEC   ; handler's saved ra -> the kernel vector's jalr return
[0x81FB8] = 0x00081FEC
```

**thread:** interrupt/kernel context — the kernel interrupt stack, entered from the exception vector. The
signal is **not** issued by a game thread. (This is why a recomp that models "a game thread signals sema"
will never reproduce it.)

**The handle is a dispatcher-supplied argument.** The handler does not look the handle up: it signals
whatever the kernel dispatcher placed in `a1` when it entered the handler. Therefore the identity of the
semaphore that gets signalled is decided at **handler-registration time**, and the gate on "does the signal
happen at all" is **whether the interrupting line asserts** — not any value inside the handler.

A second, distinct once-per-frame signaller reaches the same helper through GT4's own stub: the handler at
`0x00551728` loads its handle from the fixed slot `[0x0064C718] = 0x0000011A` → validator index `0x1A` →
**id 26**; measured Δ cycles 4,918,903 ≈ 294.912 MHz ÷ 59.94 = one frame. `[0x0064C718]` is **unchanged**
(`0x0000011A`) at the moment the id-41 signal fires, proving the two signallers are genuinely different
paths, not one path re-entered.

## 4. The gate (disassembled live, two stages)

**(a) handle validator, `0x00578290`** — runs first, in the helper at `0x00578490`:

```
0x00578294: sra  v1, a0, 0x08
0x005782a0: and  v1, 0x7FFFFF      ; v1 = generation = (handle >> 8) & 0x7FFFFF
0x005782a4: andi a0, 0x00FF        ; a0 = index = handle & 0xFF
0x005782ac: beqz v1, ->0x005782C8  ; generation == 0 -> error
0x005782bc: lw   v0, 0x4550(at)    ; v0 = table[0x00874550 + index*4]
0x005782c0: beq  v1, v0, ->0x005782D8
0x005782c8: (error path) -> a0 = -1
```

and in the helper, `0x0057849c-0x005784a4`:

```
0x0057849c: li   v0, -1
0x005784a0: beql s0, v0, ->0x005784EC   ; id == -1  ->  RETURN, NO SIGNAL AT ALL
```

So a stale or wrong-generation handle produces **no signal whatsoever** — not a failed signal, an absent one.

**(b) CP0 Status bit 0, `0x005784ac-0x005784b8`:**

```
0x005784ac: mfc0  v0, Status
0x005784b0: xori  v0, 1
0x005784b4: andi  v0, 1
0x005784b8: beqzl v0, ->0x005784D8    ; IE == 0 -> iSignalSema
0x005784c0: jal   ->0x005ADCD0        ; iSignalSema(-0x43)
0x005784d8: jal   ->0x005ADCC0        ; SignalSema(0x42)
```

Measured `Status = 0x70030C12` at every stub hit → bit 0 = 0 → **iSignalSema** (`0x005ADCD0`). `SignalSema`
(`0x005ADCC0`) is taken only when bit 0 is set. The helper's full frame is `0x20` bytes
(`sd s0,(sp); sd s1,8(sp); sd ra,0x10(sp)`), so the caller's ra is at `sp+0x10`.

## 5. Consequence for the recomp — leads, NOT fixes

1. **The engine stub scan targets the wrong stub.** All interrupt-context signals go to **`0x005ADCD0`
   (iSignalSema, syscall `-0x43`)** via helper `0x00578480`. `0x005ADCC0` (SignalSema, `0x42`) is reached
   only when CP0 Status bit 0 is set. A scan of "the 158 call sites of `0x5adcc0`" therefore **cannot see
   the real wake path**; the path to scan is `0x005ADCD0` and its single static caller `0x005784C0`.
2. **The values to gate on** are the validator table entry `table[0x00874550 + index*4]` vs
   `(handle>>8) & 0x7FFFFF`, and CP0 Status bit 0 — not the syscall number.
3. **Recomp-side lead:** `ei` compiles to `ctx->cop0_status |= 0x10000; // Enable interrupts` (bit 16) while
   the guest gate tests **bit 0** (`andi v0,1`). The guest's own signal path would therefore choose the
   *wrong* stub if it were ever reached. Reported as a lead.
4. **Cross-seat correction (my own earlier false lead, now withdrawn):** `sub_00551728_0x551728` **is**
   emitted and **is** registered — `ps2_recompiled_functions_38.cpp` / `register_functions.cpp:467482-467483`,
   `g_ps2RecompiledFunctionTable[1131976] = sub_00551728_0x551728; // 0x551728` and `[1131982] ... // 0x551740`
   (engine build: `g_ps2EngineFunctionTable[...]`). The "zero references" claim came from grepping the wrong
   symbol name (`func_551728`). What remains genuinely open: **no reader of that function table was found
   outside `register_functions.cpp`** — so the JALR/dynamic-dispatch consumer is still unidentified.

## 6. What was NOT observed (and the instrument that was checked first)

- **No signal to the park site's semaphore (ids 30/31)** during ~1.65 s / 485 M cycles of free run.
- A one-shot BP at **`0x005b18b4`** — the instruction *after* the park-site WaitSema call, which executes
  only if the wait **RETURNED** — **did not fire** in ~920 M cycles, while the EE sat in the kernel idle loop
  `0x00081FC0` with `Paused: false` and the frame timer running (`QObject::startTimer` lines in the log,
  confirming active rendering). This is consistent with the completion being signalled in kernel/interrupt
  context on a line the recomp never delivers — matching the architect's `dmac=0`, `irq_attach=0`.
- Before drawing any conclusion the instrument was verified running: `pcsx2_status` showed `Paused: false`
  with a changing PC/cycle count, and the emulator log showed live frame timers.

## 7. Instrument defects found this session (all measured, all on this build)

1. Setting a breakpoint while the emulator is **RUNNING** → `double free or corruption (fasttop)`.
2. `pcsx2_get_backtrace` **hard-aborts the emulator** (`DebugInterface.cpp:682` ← `DebugServer.cpp:787`
   ← `DebugServer.cpp:909`). Do not call it.
3. **Removing a BP while paused leaves PC parked at that address**; it needs `pcsx2_step` to leave.
4. `pcsx2_read_memory` returns **all zeros** for the EE hardware-register window (`0x10000000`, `0x1000F000`,
   `0x10001000`) while a positive control (`0x0064C710`) returns real data.
5. `pcsx2_evaluate("[0x1000F000]")`, `[0x1000F010]`, `[0x10001010]` all return the failure sentinel
   `0xffffffff`. **Consequence: the architect's Timer2-EQUF hypothesis (`[0x10001010] & 0x400`) cannot be
   verified by reading hardware registers on this oracle build** — it must be decided another way (a BP on
   the handler, or a host-side trace), not by reading the register.
6. **Conditional breakpoints never fire** on this build: `cond a0 == 26` (a known-true condition, the handler
   signals 26 every frame) never fired, while an unconditioned one-shot at the same address did.
7. **The cycle counter is non-monotonic** — consecutive reads gave 3936394118 → 3197931520 → 114055806.
   Any cadence claim must rest on an adjacent monotonic pair (the one-frame figure in §3 does).
8. `temporary=True` breakpoints are sometimes **consumed without ever pausing** (list shows none set, yet
   the emulator kept running).

## 8. Ghidra note

Ghidra holds only the main ELF (`.text 0x01000000-0x0102DC0F`), so `0x00551728`, `0x00578290`,
`0x00578480`, `0x00557AF0` and `0x005ADxxx` have **no static presence**. Every overlay measurement in this
section was taken from the live oracle, not from Ghidra.

**No fix written.** Emulator left running (PID 858872); no relaunch was needed this session.

---

## R7 — CP0 Status EIE→IEc copy applied; sema5 still deadlocked (fix necessary, not sufficient) (architect, 2026-10-08)

**Fix:** `EeScheduler::dispatchIrq` now copies the live context's Status.EIE (bit 16) to the Interrupt
invocation's Status.IEc (bit 0), clearing EIE:
```cpp
const uint32_t liveStatus = m_runtime.cpu().cop0_status;
invocation.context.cop0_status = (liveStatus & 0x10000u) ? 0x1u : 0u;
```

**Boot result** (15s `boot_w275r7.log`): functions_entered=10451, distinct_pcs=214, halt=`guest_blocked`
(tid1 parked WaitSema(sema5) @0x005adce8) — **byte-identical to R5/R6**. No `iSignalSema` (the interrupt-safe
variant) appears; only `0x42 SignalSema` 13× from loader PCs. The engine's cause-11 INTC handler
`0x005b8158` is registered (AddIntcHandler ra=0x005b7c68, a0=0x0b) but not observed as invoked, and the
oracle's sema-signalling handler `0x00557AF0` (reached via the 0x81FE0 trampoline, ra=0x81FEC, sp=0x81FC0)
never runs in our boot.

**Verdict:** the EIE→IEc copy is a necessary step (the handler's `andi v0,1` gate needs it), but it does not
unblock sema5 — the interrupt that would invoke the sema-signalling handler (0x00557AF0 / 0x005b8158) is
either not firing or not matching after ExecPS2. That is the next wall: trace the post-ExecPS2 interrupt
dispatch for the cause that reaches 0x00557AF0 and delivers `iSignalSema(a0=0x29 → sema 41)`.

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r7-cp0-iec.patch` (EeScheduler.cpp, +10/-0 on the R7
hunk; full file diff reverse-apply OK).

---

## R8 — EE timer model ALREADY exists; the real wall is the scheduler never idling to the timer deadline (architect, 2026-10-08)

**Premise refuted.** The coordinator's "the runtime doesn't model the EE timers" is wrong. The model is
complete and wired:
- `ps2_memory.cpp`: `EeTimer[4]`, `advanceEeTimers()` (advances COUNT from the EE-cycle clock, sets
  EQUF/OVFF, returns an interrupt mask), `cyclesUntilNextEeTimerInterrupt()`, MMIO read/write handlers
  (`writeIORegister` → `decodeEeTimerRegister`).
- `EeScheduler.cpp`: `accountCycles()` ORs `advanceEeTimers` into `m_pendingEeTimerInterrupts`;
  `processPendingEvents()` drains it and calls `dispatchIrq(false, 9+timer)`; `waitForEvent()` advances
  `accountCycles` to `cyclesUntilNextEeTimerInterrupt()`.

**Measured (diagnostic traces, then reverted):** Timer2 IS armed —
`MODE=0x382` (clock=2 → 576 kHz, CUE=1, CMPE=1), `COMPARE=0xFFFF` (65535). EQUF would fire at
65535 ticks = 33.5M EE cycles (113.8 ms). But the guest parks in `WaitSema(sema5)` at 29.4M EE cycles
(100 ms) — *before* the timer fires — and `cyclesUntilNextEeTimerInterrupt` is **never called** (zero
DEADLINE trace lines), so the scheduler never idles `m_eeCycle` forward to 33.5M. The timer stalls at
57536 ticks, EQUF never sets, `dispatchIrq(false,11)` never runs, and `sub_005b8158` is never invoked.

**Real fix (not "model the timers"):** when the guest blocks and no thread is runnable, the scheduler /
harness drive loop must advance `m_eeCycle` to the next timer deadline — i.e. call `waitForEvent()` /
`accountCycles(cyclesUntilNextEeTimerInterrupt())` — so Timer2 EQUF fires and `dispatchIrq(false, 11)`
invokes `sub_005b8158`. That is the idle-advance gap, not a missing timer model.

Diagnostic traces were reverted (`ps2_memory.cpp` back to +212/−4). No source change committed this round.

---

## R9 — idle-advance wired: sema5 cleared, guest runs deep into the engine (architect, 2026-10-08)

**Fix:** `EeScheduler::serviceInvocations`' block path (nothing runnable) now advances `m_eeCycle` to the
nearest EE-timer deadline before reporting `Blocked`:
```cpp
const uint64_t timerCycles = m_runtime.memory().cyclesUntilNextEeTimerInterrupt();
if (timerCycles != max && timerCycles > 0) { accountCycles(timerCycles); continue; }
```
This mirrors what `run()` does via `waitForEvent()` but on the driver's `serviceInvocations` path, which the
harness actually uses. After the fire EQUF is set, so the next probe is "no pending timer" → no spin.

**Boot result** (15s `boot_w275r9.log`, 45s `boot_w275r9long.log`):

| | R7 (before) | R9 (after) |
|---|---|---|
| functions_entered | 10451 | **27730** (45s) |
| distinct_pcs | 214 | **282** |
| ee_cycle | 29.4M | **1.11B** |
| intr_run | 64 | **191** |
| frames | 350 | 1144 |
| halt | guest_blocked sema5 | **missing_function @0x0060b548** |

Timer2 EQUF fired, `dispatchIrq(false,11)` reached `sub_005b8158`, sema was signalled, and tid1 woke
(`tid1 RUNNING`, no longer parked). `0x56` LIMITATION is now 0.

**Next wall:** dispatch miss @ `0x0060b548` ("no generated function") — the same class as R5's `0x005b0850`
(an interior entry reached only by a runtime-computed branch, absent from the switch cases). Fix: add
`0x0060b548` to the engine `entry_points`, regenerate `recomp_engine_w251`.

**Still latent:** (a) the sema id-allocation divergence (hardware signals id 41, ours dense id 5) is behind
this wall; (b) remaining `0x5a`/`0x5b` LIMITATIONs (`0x5b79f8`, `0x5b98d0`, `0x80076000`).

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r9-timer-idle-advance.patch` (EeScheduler.cpp full
diff, reverse-apply OK).

---

## R10-R13 — interior-entry batch: dispatch misses cleared, guest now spins at 0x00580dd8 (architect, 2026-10-08)

**Class batched.** The `missing_function` wall was the "runtime-computed `jalr` into a packed sub-function /
shared epilogue" class — interior addresses of a function reached only by an indirect branch, so the
control-flow analyzer never discovered them as switch-case labels. Added to `entry_points`
(`/mnt/ssd/gt4/work/w251-engine_authoritative.toml`):
```
func_5B0850@0x005B0850   (R5)
func_60B548@0x0060B548   func_60B590@0x0060B590
func_60B5C0@0x0060B5C0   func_60B608@0x0060B608
func_5DA258@0x005DA258   func_5DA308@0x005DA308
```
Regenerated (`PS2RECOMP_ENTRY_ADDR_CSV` + `PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable`), recompiled,
relinked. Each addition became a `case 0x…u: goto label_…;` arm of its owner function (the
`collectInternalEntryTargetsImpl` path).

**Boot result** (45s `boot_w275r13.log`):

| | R9 | R13 (batch) |
|---|---|---|
| functions_entered | 27730 | **32804** |
| distinct_pcs | 282 | **747** |
| ee_cycle | 1.11B | 1.40B |
| intr_run | 191 | 197 |
| halt | missing_function @0x0060b548 | **guest_cycle_no_progress @0x00580dd8** |

The interior-entry dispatch misses are cleared: the halt is no longer `missing_function`. The guest now
cycles on 1 address, `0x00580dd8` (ra=0x00580dac) — a SPIN/HANG, a new wall class (needs the same
first-divergence-vs-oracle treatment, not another entry_point).

Still latent: the sema id-allocation divergence (hardware signals id 41 vs our dense id 5), and the
`0x5a`/`0x5b` LIMITATIONs (`0x5b79f8`, `0x5b98d0`, `0x80076000`).

---

## R13 spin @0x00580dd8 — static decode (architect, 2026-10-08)

**Verdict in one line:** the EE is a *producer* spinning for a free 0x40-byte command buffer in the pool at
**0x888240**; the buffer is only returned by the **IOP/SIF response path**, which never fires.

### 1. What the spinning function is
`0x00580dd8` is inside `sub_00580cd8` (0x580cd8–0x580fc0) — `ps2_recompiled_functions_40.cpp:67308`
(`sub_00580cd8_0x580cd8`).

The loop at `0x580dd8` (`file 40:67595-67620`) is a **pure register countdown delay**:
```
0x580dcc: v0 = 0x100000 (lui 0x10) ; v1 = -1
0x580dd8: v0 -= 1 ; nop x4 ; bne v0, v1, 0x580dd8
```
It exits on its own after 0x100001 iterations when **`$v0 == $v1 == 0xFFFFFFFF`**. It reads **no memory**
— it is the *delay limb* of a busy-wait. The halt reports "cycling 1 address" because the PC spends nearly
all wall time here, but the actual hang is the **outer poll loop** that re-enters it.

### 2. ra=0x00580dac — the caller
`ra=0x580dac` is the return address of `jal func_5B17D0` at **0x580da4** (`file 40:67508`), *inside the same
function*. The enclosing loop is `label_580d98` (`file 40:67497`):
```
0x580d98: v0 = func_5B17D0(0x874FA8, 0x80000592, 0)   ; a1=0x80000592 = IOP command
0x580dac: if (v0 >= 0)  -> 0x580dfc  (proceed / post)
          else: if (*(0x655ED0) > 0) func_5B0750(0x6CE138)   ; drain pending
                delay(0x100000); goto 0x580d98
```

### 3. What it waits on (the exit condition)
`func_5B17D0` (`file 41:198095`) returns **-1** when `func_5B11F0(0x888240)` returns **NULL**.
`func_5B11F0` (`file 41:195840`) is the pool allocator:
- count = `*(0x888248)` (read 0x5b1208), entries = `*(0x888244)` (read 0x5b1214), stride **0x40** (0x5b1274)
- in-use flag = **bit0 of `*(entry + 0x10)`** (read 0x5b1220, `andi 0x1` 0x5b1224)
- returns NULL when `count <= 0` **or** every entry has bit0 set.

**The word that must change: `0x888240 + n*0x40 + 0x10`, bit0 must become 0** (one entry freed).

### 4. Who should write it
`func_5B1298` (`file 41:196122`) frees an entry: `*(entry+0x10) &= 0xFFFFFFFE` (store at **0x5b12b0**).
Its callers are the reclaim/completion side:
- `sub_005b1508` — `jal func_5B1298` at 0x5b15a4 / 0x5b15dc / 0x5b163c
- `sub_005b1328` — at 0x5b13d0
- `sub_005b19b0` — at 0x5b1b18 / 0x5b1b5c

`sub_005b1508` is called from **exactly one** site: `jal` at **0x520708** inside `sub_00520698`
(0x520698–0x520738, `file 37:30118`). `sub_00520698` is called only from `sub_0051f0f0`
(0x51f0f0–0x51f8b8, `file 37:19000`) at 0x51f43c/0x51f4e8/0x51f580/0x51f680/0x51f768. **`sub_0051f0f0` has
no direct `jal` caller anywhere in the recompiled output** — it is reached indirectly (registered handler /
interrupt vector). That is the IOP/SIF response callback chain:
```
IOP response → sub_0051f0f0 → sub_00520698 (0x520708) → sub_005b1508 → func_5B1298  (frees pool entry)
```

**Conclusion:** `sub_00580cd8` posts a 0x40-byte command packet to the IOP (command `0x80000592`) and spins
for a free pool buffer. The buffer is only released when the IOP completes the request and the SIF/response
handler frees it via `func_5B1298`. If the IOP response (SIF RPC reply / DMA-done interrupt) never arrives,
the pool stays full and the EE spins forever — this is the likely divergence to chase against the oracle
(PCSX2: does the IOP ever ack command `0x80000592` at this point?).

---

## R13 ORACLE PASS — hardware's value at the spin's exit condition vs ours (measurer, 2026-10-08T16:12Z)

Oracle: fresh `pcsx2-qt -debugger -fastboot /home/or/tidy/disk-images/gt.iso` (GT4 SCUS-97328), 4 breakpoints:
`0x00580dd8` (the spin), `0x00580dac[cond v0<0]` (the exit condition word), `0x005b1280` (the pool-exhaustion
NULL return), `0x005b1298` (the pool release).

### The side-by-side

| | HARDWARE (PCSX2 oracle) | OURS (boot_w275r13.log) |
|---|---|---|
| exit-condition word `a1 := v0` at `0x00580dac` | `0x00000000` (twice: Cycles 1,643,360,223 and 1,796,240,744) | negative (`-1`) on the 33rd call |
| `0x00580db0 bgezl a1` | **TAKEN** → `pc = 0x00580dfc` | not taken → `0x580db8` |
| `0x00580dd8` (the spin) | **never executed** over a run to Cycle 3,166,640,005 | 4096 dispatches → halt `guest_cycle_no_progress`, `ee_cycle=1405775538` |
| `0x005b1280` (pool exhaustion) | **never fires** | reached |
| pool descriptor `[0x00888240]` (alloc counter) | `0x00008d88` = **36,232** allocations through **32** slots ⇒ heavy reuse | 32 allocations, all 32 slots, **zero reuse** |

**First disagreeing instruction: `0x00580db0 bgezl a1`.** Hardware `(int)a1 >= 0` → taken → `0x00580dfc`.
Ours falls through to the `0x100000`-iteration delay loop at `0x00580dd8`. Everything after that is a symptom.

### What our runtime isn't delivering — hardware-measured

`0x005b1298` (the pool release) **fired on hardware** at Cycle 3,166,640,005 with:

- `a0 = 0x20886A80` — the pool entry being freed
- `ra = 0x005B13D8`
- `sp = 0x00081EE0` — the **EE kernel stack** ⇒ **interrupt context**, not thread context
- `s2 = 0x00081F20` — a kernel-stack event struct

The handler is **`sub_005B1328`** (`0x5B1328`–`0x5B13E0`), entered with `a0` = that kernel-stack struct. Its
`[a0+0x1C]` points at the request entry `0x00873F00`, which contains:

```
+0x00 = 0x20886A80   pool-entry back-pointer
+0x04 = 0x00008D88   allocation-counter snapshot (== [0x00888240])
+0x08 = 0x0000002E   SEMA ID (46)  -- positive, so SignalSema IS called
+0x14 = 0x00113E70
+0x18 = 0x006D5DF0   guest callback's gp
+0x1C = 0x00000000   guest callback (NULL for this request)
```

Handler body, verbatim from PCSX2's native disassembler:

```
0x5B1348  lw v1,0x20(s2) ; lui v0,0x8000 | ori 0xA
0x5B134C  beq v1,0x8000000A -> 0x5B1374        ; status OK
0x5B1354  bnez (sltu v1) -> 0x5B13BC           ; other status: free only
0x5B1364  beq v1,0x80000009 -> 0x5B13AC        ; status 9: copy fields
0x5B1374  lw s1,0x1C(s2)                       ; the request entry
0x5B1384  lw v0,0x18(s1) ; gp = v0             ; relocate gp for the callback
0x5B1390  lw v0,0x1C(s1) ; jalr v0 ; a0=[s1+0x20]   ; call the guest callback
0x5B13BC  lw a0,0x8(s1)                        ; SEMA ID
0x5B13C0  bltz a0 -> 0x5B13D0                  ; skip if negative
0x5B13C8  jal 0x005ADCD0                       ; <-- SignalSema(46)      **THE MISSING STEP**
0x5B13D0  jal 0x005B1298 ; a0 = [s1]           ; <-- release the pool entry **THE MISSING STEP**
```

### Ours, from `boot_w275r13.log` (proves the release never happens)

- 34 `[sceSifSetDma:DESC]` lines, all `ra=0x5b0d8c`; the `src` values march
  `0x20886A40 → 0x20887200` (`31 * 0x40`) — **all 32 pool entries, each used exactly once, zero reuse**.
- Line 67017 `sce_SignalSema calls=16 ra_count=0x01009cd0x4,0x0100b2c4x2,0x0100b1a0x2,0x01000b74x2,
  0x01000df0x2,0x005b2e7cx2,0x0101b674x1,0x005bec9cx1` — **no `ra=0x5b13c8`**: the completion handler
  never runs on our side.
- Line 67029 `sce_WaitSema ra_count=0x005b18b4x32`; line 67025 `sce_DeleteSema ra_count=0x005b18bcx32`;
  line 67013 `sce_CreateSema ra_count=0x005b1854x32` ⇒ exactly **32** successful `0x5B17D0` cycles; the
  **33rd** returns `-1` at `0x5B1810` (`0x5B11F0` found no free slot).
- Spin report line: `inv_by_kind=[intr=185,dmac=0,override=0,other=0]` — our runtime dispatched **zero**
  DMA handlers, although GT4 registered one for **channel 5 (SIF0)** at `0x005B0E30`
  (`sce_AddDmacHandler a0=0x00000005 a1=0x005b0e30`) and enabled it (`sce_EnableDmac a1=0x005b0e30`).
- The cause is already declared in the log, lines 65472–65487: `VULCAN 4 LIMITATION: EE syscall 0x0B
  AddSbusIntcHandler(cause=0..15) registered with id N -- SBUS interrupts are IOP-side and are never
  delivered to a recompiled EE; the handler will not run and the cause will not be dispatched.`

### Named chain (hardware)

```
SIF0 DMA completion interrupt
  → dmac/SBUS dispatch (channel 5 handler 0x005B0E30 / 16 SBUS causes)
    → sub_0051f0f0 → sub_00520698 (jal at 0x520708)
      → sub_005b1508
        → sub_005B1328  · 0x5B13C8 SignalSema(46)  · 0x5B13D0 sub_005B1298 (release pool entry)
```

Ours: `sceSifSetDma` is HLE'd and signals the wait sema itself, so `WaitSema` succeeds 32× — but the guest's
release at `0x5B13D0` never executes, because the guest-side completion chain above is never entered. The
32-slot pool at `0x00888240` fills, allocation #33 returns NULL, `0x5B17D0` returns `-1`, `0x580db0` is not
taken, and the guest enters the delay loop whose exit condition hardware never even evaluates.

### Instrument notes (both owned)

1. `pcsx2_get_backtrace` while paused at `0x5B1298` **killed the emulator** (DebugServer `ECONNREFUSED`,
   no `pcsx2` process). Relaunched cleanly: `pcsx2-qt -debugger -fastboot
   /home/or/tidy/disk-images/gt.iso`, pid 981105, port 21512 listening. Do not call `get_backtrace` at a
   high-iteration PC in this build.
2. Harness `maxCycleRepeats=4096` is ~256× below the guest's legitimate 1,048,576-iteration delay loop
   (`0x580dcc lui v0,0x0010` / `0x580dd8 addiu v0,-1` / `0x580dec bne v0,v1`), and
   `EeScheduler::checkpointDue()`'s slice/priority branch yields on every back-edge (+32 cycles each), so
   even a *correct* entry into that delay loop is misreported as `guest_cycle_no_progress`. Log line 66887
   `VULCAN4 CYCLESUM loads=0 branches=0` confirms the harness never regained control anywhere but `0x580dd8`.

**No fix written** — the architect's static pass owns it.

---

## R14 — SIF0 completion dispatch + maxCycleRepeats: spin cleared, guest runs full 45s (architect, 2026-10-08)

**Fix (two parts):**
1. `SIF.cpp sceSifSetDma`: after the EE→IOP (SIF1) copy, queue an `Interrupt` invocation for the SIF0
   completion handler `sub_005B1328` (0x5b1328) with `a0 = xfer.src` per posted transfer, so the pool
   entry is freed and `SignalSema(46)` fires (the hardware DMAC ch5 completion path).
2. `vulcan4_harness.cpp`: `maxCycleRepeats` 4096 → `0x400000` (4,194,304), clearing the guest's legit
   1,048,576-iteration delay loop that was misreported as `guest_cycle_no_progress`.

**Boot result** (45s `boot_w275r14.log`):

| | R13 | R14 |
|---|---|---|
| functions_entered | 32804 | **56119** |
| ee_cycle | 1.40B | 2.92B |
| vblanks_processed | 336 | 643 |
| frames | 1349 | 2495 |
| intr_run | 197 | 251 |
| halt | guest_cycle_no_progress @0x580dd8 | **wallclock_deadline** (full 45s) |

The 0x580dd8 spin is gone; the guest runs the whole 45s (`wallclock_deadline`, no wall), top PC
`0x005b2980`.

**Caveat:** `SignalSema ra=0x5b13c8` is still 0 in the tally — the `sub_005B1328` invocation's `a0`
(the IOP→EE ack buffer the handler reads at +0x1C/+0x20) is likely wrong. The `maxCycleRepeats` raise is
the definite half; the SIF0 completion `a0` needs oracle verification (the handler checks command ids
0x8000000A/0x80000009 against `a0[0x20]`).

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r14-sif0-completion.patch` (SIF.cpp).

---

## R14 follow-up — sub_005B1328 a0 decompiled; picture still the disclaimer (architect, 2026-10-08)

**a0 correction (decompiled, not yet fixed):** `sub_005B1328` (0x5b1328-0x5b13f8) reads:
```
s2 = a0
v1 = a0[0x20]          ; command id, == 0x8000000A or 0x80000009
s1 = a0[0x1C]          ; pool entry pointer
a0[0x24] -> s1[0x24]   ; free-list update
a0[0x28] -> s1[0x14]   ; free-list update
if s1[0x8] >= 0: jal func_5ADCD0 (SignalSema) with a0 = s1[0x8]   ; sema 46
```
So `a0` is the **SIF command header** (a ~0x2C-byte ack buffer with the pool entry at +0x1C and the command
id at +0x20), NOT `xfer.src` (the 20-byte transfer payload). My R14 `a0 = xfer.src` is wrong: the invocation
runs, reads garbage at `xfer.src+0x20`, and bails — `SignalSema ra=0x5b13c8` is still 0. The exact ack buffer
address is the guest-allocated pool slot (the `0x00888240` region, returned by the guest's own
`jal 0x005B17D0` allocator); nailing it needs the oracle.

**Picture (R5 gate):** `bash .auto/verify-menu.sh` → `STRUCTURAL MATCH` to the disclaimer (1 colour,
non-black 0.1085 vs reference 0.1150). The screen is STILL the disclaimer — the GS has not drawn GT4's
frames yet. That is the R5 enabler E3/GS work, not a boot blocker.

Net: the R14 `maxCycleRepeats` raise is the definite half (spin cleared, full-45s boot); the SIF0
completion `a0` still needs the oracle-correct buffer before `SignalSema ra=0x5b13c8` fires and the pool
reuses.

---

## R5/E3 — GS static + runtime: the wall is (a) the game never reaches per-frame draw (architect, 2026-10-08)

**Question:** why is `verify-menu.sh` still the 2005 disclaimer (1 colour, non-black 0.1085) when the
guest runs the full 45s (functions_entered=56119, 0x005b2980 main loop, frames_presented=2495)?

**Evidence (boot_w275r14.log, /mnt/ssd/vulcan4-build/run/):**

1. **`gs_packets=15` is byte-identical across r10/r11/r12/r13/r14.** The game's GS work has not advanced
   at all since the disclaimer era — nothing new is being drawn.
2. **All 15 GIF packets are the one-time reset-graph init**, all at `guestPc=0x100a434` (log lines
   ~54400-54577): `PRIM=0x8005` + `RGBAQ`, four BITBLTBUF/TRXPOS/TRXREG/TRXDIR IMAGE texture uploads
   (114688 / 2048 / 1024 / 256 bytes), two nloop=23 context dumps (SCISSOR `0x1bf0..0x27f0`,
   XYOFFSET `0x7208..`), and one single-vertex textured triangle-fan (`packet#10` PRIM=`0x11e` +
   XYZ2=`0x89709250`). **Zero sustained per-frame draw** — no XYZ2/XYZ3/SPRITE/TRIANGLE vertex streams.
3. **Last `[gs:gif]` at line 54577; first `[frame:upload]` at line 60331.** ~5700 further log lines with
   **zero** GS/GIF activity across the whole 45s (2495 host present ticks). `halt=wallclock_deadline`,
   top PC `0x005b2980` = the guest is spinning in a wait, not drawing.
4. **`gs_frame_reg_writes=2`** — `DISPFB1=DISPFB2=0x1400` (fbp=0, fbw=10) and
   `DISPLAY1=0x1bf27f00000000` (640x448), written twice and **never updated again**. The first-frame
   draw-and-swap cycle never completed: after 45s DISPFB still points at VRAM page 0, the "front" buffer.
5. **The present path is NOT the wall.** `GSCpuBackend::PresentFromLocalMemory`
   (tools/PS2Recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp:2274) decodes DISPFB (line 2281), finds page 0
   all-black, and falls back to the draw context's framebuffer (`ctx0.fbp=160`) via the
   `displayFrame.fbp==0 && all-black` candidate loop (gs_cpu_backend.cpp:2316-2331). Hence every
   `[frame:upload]` line reads `displayFbp=0 sourceFbp=160`. It faithfully re-decodes the SAME stale VRAM
   content every tick; the game has drawn nothing new into ANY buffer.

**Named wall — (a) blocked upstream of the render routine.** The game configures the GS once and then
issues no draw commands. The suspect (already on the board): the SIF0 completion invocation still passes
the wrong `a0` (`xfer.src` instead of the SIF command header — pool entry @+0x1C, command id @+0x20),
so `SignalSema ra=0x5b13c8` never fires and the main loop at `0x005b2980` never proceeds into the render
path. Fixing the GS present/decode (hypotheses b and c) cannot change the picture until the guest emits
per-frame GIF draw packets.

**What must happen for GT4 to draw:** correct the SIF0 completion `a0` (SIF command header, not
`xfer.src`), so `SignalSema(46)` fires and the loop past `0x005b2980` reaches the render routine — at
which point `gs_packets` should jump from 15 to thousands and DISPFB should be re-pointed at the drawn
buffer.

No fix written (static read-only pass).

## R4.8 follow-up — oracle measurement of sub_005B1328's a0

Oracle: real GT4 USA v2.00 (SCUS-97328) on PCSX2 `-debugger -fastboot`, disc
`/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso`. The handler entry (PC=0x005b1328) was caught twice;
a0 and the header were identical on both hits.

### The exact a0
**a0 = 0x00081F20 — the caller's stack pointer (sp), not a pool/heap allocation.**
Set by `0x005b0f40: daddu a0, sp, zero`, immediately before `0x005b0f48: jalr a2`
(a2 = 0x005b1328, loaded from the dispatch table record at 0x008868A0).

### Command-header layout at a0 (0x2C bytes, u32)
| off | value | meaning |
|---|---|---|
| +0x00 | 0x00008000 | |
| +0x04 | 0x00873dc0 | -> [0x00873dc0] = 0x00FFFFFF |
| +0x08 | 0x80000008 | SIF cmd id (other/stale) |
| +0x0C | 0x00000000 | |
| +0x10 | 0x00000000 | |
| +0x14 | 0x00000000 | |
| +0x18 | 0x00000000 | |
| +0x1C | **0x008735C4** | pool entry pointer -> s1 |
| +0x20 | **0x8000000A** | command id (0x80000009 also seen in the driver) |
| +0x24 | 0x00000000 | |
| +0x28 | 0x00000000 | |

Pool entry s1 = 0x008735C4: +0x08 = 0xFFFFFFFF (negative, so the handler tail's `bltz` skips
func_5ADCD0 on this path); +0x18 = 0x006DDDF0 (gp); +0x1C = **0x005780F8** (callback, jalr'd at
ra=0x5b13c8 — the SignalSema frame the wall report names); +0x20 = 0x008735C0 (= 0x00000106).

### Divergences from the R14 premise
1. a0 is the dispatcher's own stack buffer (0x00081F20 == sp) — not a `jal 0x005B17D0` return value.
2. The 0x00888240 region the premise attributed to a0 is actually in **a1** at entry.
3. The pool entry is 0x008735C4, outside the 0x00888240 region.

### Effect
With a0 = header, `a0[0x20] == 0x8000000A` matches and the handler reaches `jalr s1[0x1C]` =
0x005780F8 with ra = 0x5b13c8 — the exact SignalSema site the wall names. So `a0 = xfer.src` is the
reason the handler bails. The runtime must synthesize a 0x2C-byte header on its stack with
+0x1C = a pool entry and +0x20 = 0x8000000A (or 0x80000009). No fix written (measurement only).

---

## R14 corrected a0 — sub_005B1328 RUNS (iSignalSema 31× ra=0x5b13d0); sema id 0 not 46 (architect, 2026-10-08)

**Fix:** synthesized the SIF0 ack header (oracle layout) and passed it as the handler's a0 instead of
`xfer.src`:
```cpp
// RDRAM 0x00081F20, 0x2C bytes: +0x1C = 0x008735C4 (pool entry), +0x20 = 0x8000000A (SIF_CMD_END)
// a0 = 0x00081F20 in the queued sub_005B1328 Interrupt invocation
```

**Boot result** (45s `boot_w275r15.log`):
- `sub_005B1328` **runs**: `0xffffffbd sce_iSignalSema calls=31 ra=0x005b13d0` — the `SignalSema` site inside
  the handler fires (31 completions).
- functions_entered=56954 (was 56119), distinct_pcs=**748** (was 222) — the pool reuses and the guest
  progresses through the 0x580dd8 delay loop (halt=`wallclock_deadline`, full 45s).
- **But** `iSignalSema a0=0` (not 46): the hard-coded pool entry `0x008735C4` has `[0x8]=0` in this boot —
  it is the wrong/empty entry for the current command. The real per-command pool entry is the guest
  allocator's return (`jal 0x005B17D0`), which needs tracing.
- `gs_packets` still 15 (no per-frame draw); `verify-menu.sh` still STRUCTURAL MATCH to the disclaimer
  (1 colour, non-black 0.1085) — the GS has not drawn GT4's frames (E3/GS work).

**Verdict:** the a0-header mechanism is correct (the handler reaches its SignalSema site); the remaining gap
is deriving the actual pool-entry pointer per command instead of the hard-coded 0x008735C4, so
`SignalSema(46)` (not 0) fires. The GS/E3 is the separate picture blocker.

Patch: `/home/or/vulcan4/tools/patches/ps2recomp-linux-r14-sif0-completion.patch` (updated SIF.cpp).
