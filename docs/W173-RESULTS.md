# W173 — resume: starvation refuted, the MC is not the lever, the decode wall stands

**Dish:** resume of `02-finish-the-core-decode` after the gate failed. **Result:** ❌ the gate still
fails (screen unchanged). Two candidate root causes were tested and **refuted**, narrowing the wall.

## Test 1 — the "tid1 never runs" story is REFUTED

W171/W172 left the possibility that the guest is thread-starved. A new read-only probe in
`EeScheduler::makeRunning` (`VULCAN4_W173_SWITCH`, off by default) logs every thread switch:

```
[w173:switch] n=1  id=1 prio=0 pc=0x1000008 eeCycle=132544
[w173:switch] n=2  id=1 prio=1 pc=0x101f348
[w173:switch] n=3  id=2 prio=2 pc=0x1000ba0
...
histogram over the first 300 switches:  id=1 x154,  id=2 x146
```

**Both threads run and rotate.** The scheduler is not starving anybody; the "tid1 ready, never runs"
claim does not survive this measurement. Do not re-chase thread starvation.

## Test 2 — the memory card is not the lever

Every boot ends `[MC] Open '/BASCUS-97328GAMEDATA/core.gt4' ... result=-4`. Providing the save the
guest probes for (a symlink `mc0/BASCUS-97328GAMEDATA/core.gt4 -> ../../CORE.GT4`, reverted after)
changes the open to `result=0` — the guest genuinely loads it now — **and the picture does not
change**: a capture is still `colors=16 mean=4193.67`. So the MC is not what holds the disclaimer.
(The symlinks were removed to restore the environment.)

## What stands

- The present path is faithful (W171): the framebuffer hash changes every frame; the guest redraws
  the disclaimer.
- The decode wall (W172): `sub_01008C50` is handed an empty node because its source `+8` reads 0 at
  the branch test; the translation at `0x100811c`/`0x1008134` and the `sub_01008080` call setup at
  `0x10082c0` were re-verified faithful this pass. No codegen bug.
- New this pass: the guest is **not** starved and the MC is not the gate.

## Gate (authoritative, honest)

```
newest capture : /mnt/ssd/vulcan4-build/run/disclaimer-break-MC-174433.png
functions_entered=37898 halt=wallclock_deadline bios_files=0
GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference — the screen did not change.
```
Suite **497/497**. New probe off by default. Capture path reported, not opened.

## STUCK
```
STUCK:   picture that is not the disclaimer
TRIED:   Q1/Q2 (W172); thread-switch census (both threads run); MC save present (result=0, no change);
         re-verified sub_01008080 call setup + sub_01008C50 branch are faithful
BLOCKED BY: the guest is alive, both threads scheduled, drawing the disclaimer, but never leaves it;
         the one named defect (sub_01008C50 decoding an empty node) is guest ordering, and no
         runtime/game.toml/override change showed a picture change
NEED:    the exact writer that should size the decoder's source node (+8) BEFORE sub_01008C50 tests
         it -- trace it in one run with a watch that keys on the node pointer captured at the branch,
         not fixed addresses (heap reuse made fixed-address watches ambiguous in W172)
```
