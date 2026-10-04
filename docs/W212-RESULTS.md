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

## (c) "Returns" made measured — via `VULCAN4_RETURNS=1` (`PS2X_STRICT_RETURN_DIAGNOSTICS`)

The first attempt (a counter in `dispatchGuestBranch`) read 0 for every node because generated `jr $ra`
returns are emitted **inline** unless `PS2X_STRICT_RETURN_DIAGNOSTICS` is defined. `build_harness.sh`
now takes `VULCAN4_RETURNS=1` (OFF by default, law 12) and compiles the generated unit with that define,
so the return edge routes through `dispatchGuestBranch` and the scoreboard counts it. Measured
(`w217a` `wallclock_deadline` FE=8034, `w217b/c/d`):

```
pc=0x100F8C8 count=1  returns=2
pc=0x1010B10 count=1  returns=1
pc=0x1004308 count=1  returns=1
pc=0x1004500 count=1  returns=0        <-- the one node with no return edge
pc=0x1010BD0 count=2  returns=602
pc=0x100F390 count=11 returns=13
pc=0x100ED78 count=2  returns=34
pc=0x10047C0 count=1  returns=6417
pc=0x101D2A0 count=29 returns=1211
pc=0x1000558 count=1  returns=16547
```

**Every node returns except `0x1004500` (`returns=0`)** — so `sub_01004500` is the one function whose
return has **not** been observed by the time the budget expires. (The return attribution is approximate
— a return from an inner call is credited to the nearest enclosing watched node — so the large counts on
`0x10047C0`/`0x1000558` include their callees' returns; the reliable signal is **returns=0 vs returns>0**.)
`0x1004500` is a caller (`jal 0x1004308` at `0x100451C` then more calls at `0x1004544`), so a plausible
reading is that it is simply still inside a long call chain at halt — the honest statement is that its
return is the one not measured.

**Positive control:** the strict build is the control itself — the same scoreboard that read `returns=0`
for every node in the non-strict build now prints non-zero counts, so the counter can fire.

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

---

## W214 — the answer: `sub_01000558` is inside `sub_010047C0`'s deep call chain at halt, never past it

The dish-16 NEXT asked: why is `0x1000558` entered once and `0x1000608` never dispatched even though
`0x10047C0` returns? **Measured answer: it is not a lost PC — the main thread is still inside the
`0x10047C0` subtree when the budget expires.**

- A targeted probe (`[w214:resume]`, arrivals to `0x10005D8..0x1000624` — the tail of `sub_01000558`
  after the `jal 0x10047C0` at `0x10005D4`) fires **0 times in every shape** (`w219a-d`).
- The XFER histogram contains **none** of `0x10005DC`/`0x1000604`/`0x1000608`/`0x1000610`/`0x1000618`
  as source sites — `sub_01000558` never executes a statement past the `0x10005D4` call.
- At halt, **tid1's parked PC is inside the parse subtree**, never in `sub_01000558`:
  - `w219a` `livelocked_in_syscall`: `pc=0x100F800 ra=0x1010A70 sp=0x1FFC760` (inside `sub_0100F390`)
  - `w219c` `wallclock_deadline`: `pc=0x10089C8 ra=0x10089DC sp=0x1FFFBA0` (the decode spin in `sub_010088E8`)
  - `w219d` `wallclock_deadline`: `pc=0x100F2C0 ra=0x10101B8 sp=0x1FFC1D0`
- The full chain is `sub_01000558 (0x10005D4) → sub_010047C0 → sub_01004500 → sub_01004308 →
  sub_0100F8C8 → sub_0100F390` (parse) and, on some paths, the decode spin `sub_010088E8`. **All of
  that is inside `sub_010047C0`.** tid1 is `status=Ready waitReason=1` (sleeping between slices) at the
  parked PC.

**So the "setup never returns" is a DEPTH problem, not a lost-PC problem:** `sub_01000558` is a single
call whose subtree contains the CORE.GT4 parse and the decode, and the run's budget expires with tid1
still deep inside it. The advance flag `0x1000E00` is never reached because its only caller
(`0x1000618`) is *after* the entire subtree returns.

**This subsumes and corrects the earlier "blocked" framings:** W209 said "blocked in the decompressor"
(retracted); W210 said "the tree completes" (true for the *sampled node entries*, but the sampled
subtree did **not** return past `0x10047C0` within the budget). The two are reconciled by time: the
nodes are entered and re-entered (returns>0 when measured with `VULCAN4_RETURNS`), but the whole
subtree does not finish before the halt.

## The lever this names
Two independent routes to get past it: (1) **speed** — the decode spin `sub_010088E8` (33 % of transfers
in the shape-A class) shares the CPU with the parse; making the run cover more of the subtree per
budget lets it return; (2) **depth** — the subtree's size itself. Either way the deliverable is the same:
get tid1 past `0x10005DC`, which is the first statement that can lead to `0x1000608 → 0x1000E00`.
