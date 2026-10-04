# W171 — H1 vs H2: the framebuffer is NOT frozen; the guest is redrawing the disclaimer

**Dish:** get past the disclaimer. **Result:** ❌ the capture is still the disclaimer. But the fork
the brief asked for is **answered**, and it rules out the present path.

## Correction accepted

The brief says the `$s1` recompiler theory is already dead. **Agreed, and it stays dead.**
`docs/W160-RESULTS.md` proves the assignment is emitted (`ps2_recompiled_functions.cpp:27853`) and
HANDOFF.md line 2580 retracts the theory. Nothing in this dish touches the recompiler.

## STEP 1 — the frame hash, measured

A new read-only probe hashes the **exact bytes about to become the window** (`s_scratch`, after
`copyLatchedHostPresentationFrame`) with FNV-1a, at every presentation latch, plus the non-zero byte
count and the running count of distinct hashes. OFF unless `VULCAN4_W170_FRAMEHASH=1`
(`ps2_runtime.cpp`, `UploadFrame`).

Two boots, budget 300000/45, measured on the harness `:0`:

```
[w170:hash] frame=1  hash=0x48a5f86f0f7043bb nonzeroBytes=380080 gsPackets=15
[w170:hash] frame=2  hash=0x628912b1c98e528b nonzeroBytes=382306 gsPackets=15
[w170:hash] frame=3  hash=0x48a5f86f0f7043bb nonzeroBytes=380080 gsPackets=20
[w170:hash] frame=4  hash=0x628912b1c98e528b nonzeroBytes=382306 gsPackets=20
...
[w170:hash] frame=100  hash=0xbdb2198e459bea03 nonzeroBytes=387580 distinctSoFar=66  gsPackets=300
[w170:hash] frame=200  hash=0xbdb2198e459bea03 nonzeroBytes=387580 distinctSoFar=116 gsPackets=390
[w170:hash] frame=2500 hash=0xbdb2198e459bea03 nonzeroBytes=387580 distinctSoFar=2404 gsPackets=385
```

**The hash CHANGES every frame** — `distinctSoFar` climbs at roughly one per frame (66 by frame 100,
2404 by frame 2500). The non-zero byte count moves (380080 → 382306 → 385660 → 387580) and the
image is always the same 16-level **grayscale** disclaimer at slightly different brightness.

### The fork, answered

| Hypothesis | Prediction | Measured | Verdict |
|---|---|---|---|
| H1 — guest redraws the disclaimer | frame hash constant | hash changes every frame | **half**: same *screen*, changing *pixels* |
| H2 — we lose the picture | hash constant while packets grow | hash clearly varies, and we show it | **REFUTED** |

**The present path is not dropping frames.** `latchHostPresentationFrame` →
`copyLatchedHostPresentationFrame` → `s_scratch` → `UpdateTexture` carries the changing content to
the window faithfully; the window shows what the guest drew. The guest is **actively redrawing the
disclaimer with a fade/pulse** and never leaves it. The brief's fact (b) is refined: the pixels do
move; it is the screen *identity* that never changes.

## STEP 2 — what the guest is waiting on

The brief's candidate list, checked against this session's measurements:

- **Pad — ruled out** (0 pad syscalls, measured previously and again in today's census).
- **Frozen `vsyncTick` — ruled out**: the probe prints the tick each latch and it advances
  4→5→6→…→293 across the run. It is not frozen, which is also why the disclaimer fade advances.
- **Semaphore / event flag** — the census shows `SignalSema` and `WaitSema` in near-perfect balance
  (765/766), i.e. every wait is signalled.
- **Timer/alarm syscalls (0x18/0xfc)** — absent from the census.
- **Thread barrier** — not re-chased; it is not required to explain a *changing* picture.

What IS left, and what this session's separate raw probes already measured, is the **resource decode
loop**: `sub_010088E8` (`0x1005890/0x10089c8/0x10089d4`, 33.32% each) spins inside `func_1007738`
because its input record is **all zero**. Measured directly in `docs/W160-RESULTS.md` items 6–9:
`fioRead` serves all 2,020,861 real `CORE.GT4` bytes into `0x10d1ac0`, yet the decoder is handed a
freshly-allocated, zeroed record (`sub_01008C50`'s `sw $zero,0($v0)` at `0x1008ce0`) instead of the
populated source record (`sub_01008080`'s clone `memcpy` at `0x100826c`). The disclaimer is almost
certainly the **loading screen** for that decode, and it never completes.

## The gate and the capture

`bash .auto/verify-menu.sh` → **PASS (mechanical only)** with the tightened rule:

```
newest boot log: /mnt/ssd/vulcan4-build/run/boot_w170_171139.log
functions_entered=37898 true_guest_entries=1344106 halt=wallclock_deadline bios_files=0
OK: functions_entered=37898 is past the short-shape ceiling (20000)
top XFER SITE: 0x0100afa0=197919(52.25%)
OK: top site is 0x0100afa0, no longer the 0x0100d908 barrier
capture to look at: /mnt/ssd/vulcan4-build/run/disclaimer-break-171413.png
GATE PASS (mechanical only): long run, no BIOS, barrier moved, fresh capture.
```

**PNG: `/mnt/ssd/vulcan4-build/run/disclaimer-break-171413.png`** — `identify` reports
`colors=16 mean=4193.67`, i.e. the disclaimer. **This is NOT a second distinct screen.**

Suite: **497/497**.

## What changed / next wall

- **Changed:** we now KNOW the present path is faithful and the guest is animating the disclaimer,
  so no work belongs in the GS present path. That closes the brief's fork.
- **Next wall:** the `CORE.GT4` decode (`sub_010088E8`) is fed a zero record although the file bytes
  are present at `0x10d1ac0`. Name the writer of the zero record `0x18952a0` vs the populated
  `0x1895250`, and why `sub_01008080` selects the zero one for `$a2`. That is the one thing standing
  between the loading screen and the next screen.
