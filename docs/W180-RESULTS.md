# W180 — the decode is a SHAPE-A ARTEFACT: it never runs in the good shape

**Dish:** `06-is-the-decode-the-wall`. **Result:** ❌ no picture change (gate fails). **Outcome:** a
**discovery that redirects the campaign** — the decode `sub_010088E8`, which W172-W179 spent four
dishes on, **is never entered in the good shape.**

## The measurement (4 runs, `VULCAN4_W162_DECODE`, budget 300000/90)

| run | halt | top XFER | `functions_entered` | `gs_packets` | `[w162:dec]` entries |
|---|---|---|---|---|---|
| 1 | `wallclock_deadline` | `0x0100afa0` | **21218** | 2635 | **0** |
| 2 | `pc_outside_generated_table` | `0x0100d380` | 2610 | 590 | 0 |
| 3 | `wallclock_deadline` | `0x0100afa0` | **20980** | 2575 | **0** |
| 4 | `pc_outside_generated_table` | `0x0100d380` | 2179 | 385 | 0 |

In both good-shape runs (`functions_entered >= 20000`, top XFER `0x0100afa0`) the decode probe printed
**nothing** — `sub_010088E8` is entered **0** times. The decode only ever fires in the pathological
**shape A** (top XFER `0x01005890`/`0x010089c8/0x010089d4`, `halt=livelocked_in_syscall`), which none
of these runs hit.

## What this means

- **The decode is a shape-A artefact, not the good-shape wall.** The campaign's four decode dishes
  (W172-W179) were chasing a loop that the best-shaped boot never visits. Their evidence stands (the
  decode *is* fed an empty node when it runs), but **no picture will change by fixing the decode** —
  it would only change the pathological shape, not the run that reaches `functions_entered=37898` and
  draws 8255 GS packets.
- **The good-shape wall is the RTOS sleep loop.** Top XFER `0x0100afa0` = the `sce_SleepThread` call
  inside `sub_0100AE78` (52.25% of all transfers). The guest is alive, both threads scheduled (W173),
  drawing frames, and spinning in this loop until `halt=wallclock_deadline`.

## Gate (authoritative, honest)
`GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference`. Suite **497/497**. Probe OFF
by default. No graphic change.

## NEXT WALL (named)
**The good-shape RTOS wait.** `sub_0100AE78` (called from `0x0100DE80`) polls a scratchpad byte in
`0x700020xx` and calls `sce_SleepThread` when it is not ready; the guest re-enters it 196k times per
90 s. The next dish instruments that wait: which scratchpad flag it polls, who is supposed to set it,
and why it never does in the good shape. Redirect delivery: the campaign's decode track is closed as
a shape-A artefact (recorded here), and the next dish is `07-good-shape-wait.txt`.
