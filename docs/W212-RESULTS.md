# W212/W213 — the waits are released (frame pacing); W209 is RETRACTED; the return counter is inconclusive

**Dish:** `15-setup-subtree-and-the-spin` (re-run). **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** (a) the two waits' writers are **named and they run** — the waits are ordinary frame
pacing, not the wall; (b) the W209-vs-W210 contradiction is **reconciled and W209 is retracted**.

## (a) Who releases the two waits (`VULCAN4_W212_WAIT`)

Both words are written, every frame, by guest code — nothing is frozen:

**`sub_0100B628`'s wait on `[0x70002050]`** — the writer is `0x100D8B4` inside the VSync/GS handler
`sub_0100D838`, and it **increments the counter 1→2→3→4→5** across frames:

```
[w212:wait] addr=0x70002050 size=8 val=0x1 writerPc=0x100d8b4 op=WRITE64
[w212:wait] addr=0x70002050 size=8 val=0x2 writerPc=0x100d8b4
[w212:wait] addr=0x70002050 size=8 val=0x3 writerPc=0x100d8b4
[w212:wait] addr=0x70002050 size=8 val=0x4 writerPc=0x100d8b4
[w212:wait] addr=0x70002050 size=8 val=0x5 writerPc=0x100d8b4
```

So `sub_0100B628`'s `bne s1,v1` exit is reached — **the wait is released**, ~once per VSync.

**`0x100AFA0`'s poll byte `[s0]=0x7000206D`** (`(3*id)<<2 + 0x50 + 0x1d`, id=0) — written by two
sites that toggle it:

```
[w212:wait] addr=0x7000206d val=0x1 writerPc=0x100debc   (the DMA/GS kick re-arms it)
[w212:wait] addr=0x7000206d val=0x0 writerPc=0x100dcf0   (the consumer clears it)
[w212:wait] addr=0x7000206d val=0x0 writerPc=0x100ac78
```

and the positive controls `0x70002064` (`0x100D8A4`, the frame counter, 33×1/16×0) and the two
per-thread flags `0x70002079`/`0x70002085` (`0x100DEBC` set / `0x100DCF0` clear) also toggle. **Both
waits have a live writer that executes every frame.**

**Verdict: these waits are ordinary frame pacing, not the wall.** A wait whose writer never runs would
be the wall; these writers run tens of times per second.

## (b) W209 vs W210 — RETRACTED, with the shapes on one table

| claim | shape | FE | halt | 0x10047C0 | 0x100F8C8 | 0x100F390 | 0x1010BD0 |
|---|---|---|---|---|---|---|---|
| W209 (cd7327d) | `w209c` | 23989 (60 s) | `wallclock_deadline` | — | — | — | — |
| W210 (b53a842) | `w210j` | 7920 | `livelocked_in_syscall` | count=1 last=26 | count=1 last=26 | count=49 last=719 | count=8 last=725 |
| W212 | `w216b` | 7798 | `livelocked_in_syscall` | count=1 last=26 | count=1 last=26 | count=49 last=742 | count=8 last=750 |

**They are the same shape, and W209's "the chain is blocked inside `sub_0100F8C8`/`sub_0100F390`" is
WRONG.** The direct measurement is `[w210:node]`: `0x1010BD0` (the call that follows `0x100F8C8`, at
`0x1004424`) is entered **8 times with `last_ms=725`**, and `0x100F390` 49 times `last_ms=719`. So
`0x1004308`'s next call **does** happen — it is `0x1010BD0 count=8` in the same scoreboard.

**What W209 actually observed, and why it was misread.** W209's evidence was `[w203:parse]` (fired only
on ENTRY to `sub_0100F390`, which is the decoder's inner-loop re-entry — 47 entries in `w209c`, all
printed) and `[w206:copy]` (the copy loop `0x100F800`, still running between yields). Neither of those
observes a **return**. W209 read "the last `[0x20]` is unchanged and the copy loop is still running" as
"the chain is blocked", but the copy loop is the decoder's *work* — it runs, the decoder returns, and
`0x1010BD0` is entered 8 times. **W209 confused "the last sample is inside the decode" with "the decode
never returns", and is retracted.** A probe on ENTRY can never prove a non-return; only a return edge
can, and (c) below shows this build cannot measure one.

What is genuinely true and unchanged: `sub_01000558` (`0x1000558`) is entered **once** (`count=1`), its
call `sub_010047C0` (`0x10047C0`) is entered **once**, and the statement after it (`0x1000608`, leading
to `0x1000E00`) is never dispatched. But this is **not** "blocked inside the decompressor" — the
decompressor and its caller both return (count 49/8 with advancing `last_ms`).

## (c) "Returns" made measured — the counter is INCONCLUSIVE (honest)

A `returns` counter was added, keyed on `GuestBranchKind::Return` in `dispatchGuestBranch`. It reads
**0 for every node** — because the generated code's `jr $ra` returns happen **inline inside the
generated function** and only call `dispatchGuestBranch` when `PS2X_STRICT_RETURN_DIAGNOSTICS` is
defined; on this build they return directly, so the runtime never sees the return edge. **So "returns"
is still inferred, not measured, and this probe does not close the gap.** The count>1 nodes
(`0x100F390=49`, `0x1010BD0=8`) are re-entered with advancing `last_ms`, which is the honest evidence
that they progress; the `count=1` leaves (e.g. `0x1000558`, `0x10047C0`) remain **return-UNPROVEN**.

## Corrected wall, in one line
The two frame-pump waits are **released every frame** by live guest writers (`0x100D8B4` increments
`[0x70002050]`; `0x100DEBC`/`0x100DCF0` toggle `0x7000206D`); the setup subtree runs (`0x1010BD0`
count=8, `0x100F390` count=49) and is **not** blocked in the decompressor. W209 is retracted. The
unresolved fact is that `sub_01000558` is entered **once** and its post-`0x10047C0` statement
(`0x1000608`) never runs.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
Why `0x1000558` is entered once and never re-entered, and why `0x1000608` never dispatches even though
`0x10047C0` returns — trace the main frame's PC across the `0x10047C0` → return boundary. A measured
`jr $ra` capture needs `PS2X_STRICT_RETURN_DIAGNOSTICS`, which this build does not set.
