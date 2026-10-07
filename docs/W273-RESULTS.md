# W273 — the engine image IS placed. New wall: `ExecPS2` never fires.

**Date 2026-10-08.** Commits `5774b00` (the placement + A/B) and `7ab5285` (the gates, raw).
Predecessor `a94cda1` (W253: the four loader probes relinked into the harness). Author of all three:
`Or Golan <or024662@gmail.com>`.

## What changed

1. **A new env-gated probe** `VULCAN4_RDRAM_DUMP=<hexaddr>:<hexlen>[:<path>]`
   (`tools/harness/vulcan4_harness.cpp`, runs after `watchdog.join()`; **unset = the block never
   executes**, today's behaviour byte for byte). It makes the *finished* RDRAM image measurable, so the
   engine image can be compared to the disc's own decoder instead of to a symptom.
2. **A driver fix** in `EeScheduler` — `applyPendingPreemption` promotes a successor — so
   `currentThreadId != 0` whenever a ready queue is non-empty, and the harness fallback no longer reads
   the stale shadow `runtime.cpu()`.
3. **The fix exported per law 8:** `tools/patches/ps2recomp-linux-w273-preempt-successor.patch`
   (`EeScheduler.cpp`; the other four modified subrepo files are already carried by the W253 patch).

## What was measured

Ground truth is the disc's own decoder: `zlib.decompressobj(-15)` over `CORE.GT4[6:]` → **6,119,116 B**;
engine image byte *i* == decode byte `0x134 + i`. Dump `0x00100000` len `0x517A14` = **5,339,668 B**:

| build | halt | functions_entered | gs_packets | frames | framereg | engine image |
|---|---|---|---|---|---|---|
| true baseline (no fixes) | `stuck_in_syscall` | 3794 | 30 | 1117 | 5 | **ALL ZEROS — 0 / 5,339,668 placed** |
| harness `liveFrame` fix only | `guest_blocked` | 611 | 40 | 339 | 7 | **0 mismatches** |
| both fixes | `guest_blocked` | 723 | 130 | 350 | 25 | **0 mismatches** |

- Baseline at engine `0x00100008` reads `00`; the disc says `28` (`0x70000c28 padduw $at,$zero,$zero` —
  the engine's **first instruction**). Non-zero bytes in the region: **0 of 5,339,668**.
- So the image was **not damaged — it was absent**, and the harness `liveFrame` fix **alone** makes it
  byte-perfect. **The defect was the driver's frame selection** (a frozen shadow), not the decode and
  not the copy.
- Placement site, measured:
  `VULCAN4 W30BIGCOPY seq=27 tid=2 op=memcpy src=0x12bf234 dst=0x100000 size=5339668 pc=0x10048c8`.
- Restore verified: `cmp -s` of the rebuilt binary vs the pre-experiment binary = **byte-identical**.

### Gates (raw)

- `bash .auto/verify-dish.sh` → **exit 0**, `MECHANICAL CLAIMS HOLD` — suite 497/497,
  captain-authored, tree clean.
- `bash .auto/verify-menu.sh` → **exit 1** on a **fresh** capture taken on the fixed binary
  (`run_capture.sh w273fix`, `halt=guest_blocked`, `frames_presented=350`), not the stale W252 PNG:
  **14 colours / non-black 0.1173** vs the disclaimer's **14 / 0.1150**. Still the disclaimer.
  **No picture, and this dish claims none.**

## What is unknown — the new wall

The image is placed; the loader still never reaches its hand-off.

- **`ExecPS2` NEVER FIRES** in either fixed build (the loader's own re-exec into the engine at entry
  `0x100008`; handler at `vulcan4_harness.cpp:2706`).
- **The loader's generated code contains ZERO `dispatchGuestBranch` targets in
  `0x00100000..0x00617A14`** (1887 `DirectCall` + 38 `DirectJump`, all loader-range) and the constant
  `0x00100008` does not occur in it — a static fact. **The hand-off is `ExecPS2`; nothing else can
  enter the engine.**
- **Main thread parked on `sema#7`** at `pc=0x0101f468`, `woken=0`; tid2 on `sleep#0` at `0x0101f348`.
  `sce_SignalSema` `a0` values observed: **5, 6, 6, 2, 4, 5 — never 7.** Nothing posts it.
- The only limitation printed is `VULCAN 4 LIMITATION: syscall 0x5b override handler 0x80075000 has no
  generated function in either image` — **6 times**. Best lead.
- Interrupt shape to chase alongside it: `intr_queued=93 intr_run=97 irq_attach=0 irq_done=0
  pending_hi=1`.

**STUCK: engine entry is 0 because the loader parks before `ExecPS2`, not because the image is
missing. BLOCKED BY: `sema#7` is created and waited on but never signalled, and no other thread is
runnable. NEED: a decision on where the wakeup should come from — interrupt/`intc` delivery, or a
`SignalSema` we are dropping.**

## Corrections to earlier docs (recorded, not tidied away)

- **W252's headline is SUPERSEDED.** W252 concluded the decode was "the sole loader blocker" from a
  byte divergence at payload `0xD521C` (engine `0x001D51EC`). W273's whole-image A/B shows the baseline
  engine region is **all zeros** and that a **driver** frame-selection fix alone yields **0 mismatches**
  vs the disc's own zlib decode. The defect was the driver's frame selection; the buffer W252's probes
  diffed was not the finished image. `docs/W252-RESULTS.md` carries a banner pointing here.
- W252's `halt=` caution stands and now has a mechanism: `blockCurrent` throws
  `EeDispatcherTransfer{}`, unwinding past both `m_activeSyscallId` restores
  (`ps2_runtime.cpp:5146/5152`), so `0x44` latches. Compare by hardware events or a picture.

## Exact next step for a stranger

1. Reproduce the placement: build the harness with the W253+W273 patches, run it, then
   `VULCAN4_RDRAM_DUMP=00100000:517A14:/tmp/img.bin` and
   `cmp` `/tmp/img.bin` against `zlib.decompressobj(-15).decompress(CORE.GT4[6:])[0x134:]`.
   Expect 0 mismatches.
2. Then chase the **wakeup**, not the image: find who should `SignalSema(7)`, or whether the wakeup is
   an interrupt. Two concrete leads — the `0x5b` override handler `0x80075000` (6 hits, no generated
   function) and `intr_queued=93 intr_run=97 pending_hi=1 irq_attach=0`.
3. Success is unchanged: `ExecPS2` reaches `0x00100008`, ≥1 `[Dispatch]` target inside
   `0x00100000..0x00617A14` with `bios_files=0`, then `verify-menu.sh` exit 0.
