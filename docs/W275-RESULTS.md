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

### T1 — builder — Angle A (runtime) — OPEN
Instrument the invocation queue: print every enqueue and every service with `kind`, `pc`, drain path;
boot; diff `Interrupt` vs INTC counts; name the exact function that drops / never drains the DMAC one.
Fix NOT yet. Owner: builder.

### T2 — measurer — Angle B (oracle) — OPEN
Fresh `pcsx2-qt -debugger` boot; break at `0x100dae0`; confirm the real machine DOES enter the DMAC
handler for this same VIF0 ch0 transfer; trace `AddDmacHandler` → completion → `dispatchIrq` →
handler-entry. Owner: measurer.

### T3 — architect — Angle C (static, competes with T1) — OPEN
Read `EeScheduler::dispatchIrq` (`EeScheduler.cpp:1697`) → enqueue → the drain/service loop; name the
code path that services INTC but not DMAC, independently of T1. Owner: architect.

### T4 — reviewer — falsify — OPEN
(a) Re-verify the W274 claim from `boot_w274b.log` + source; (b) falsify T1/T2/T3 as they land.
Owner: reviewer. Must falsify ≥2 findings.

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
