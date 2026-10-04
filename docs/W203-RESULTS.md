# W203 — the screen setup waits on a slow GZIP decompressor (`sub_0100F390`)

**Dish:** `14-screen-setup-parse-return`. **Result:** ❌ no picture change; gate still fails.
**Outcome (P2):** the block is named — `sub_01000558` never returns because its parse chain ends in
`sub_0100F390`, a **gzip decompressor** that progresses but does not finish the 6.1 MB buffer within
the budget, so the advance flag is never set.

## What `sub_0100F390` is

Its only caller is `0x1010A68` inside the function reached at `0x100F8C8` (the parse chain from
`sub_01000558`). Disassembly shows a bit-stream Huffman decoder with a callback `jalr` at `0x100F498`
through a table at `0x1036AC0`. The table's entries are `{0, 0x0102CDA8}`, `{0, 0x01010BD0}`, … and
the stream it is fed (`[s0+0x10]`) is a **gzip stream**: `0x1036D7F` holds
`1f 8b 08 08 … "in.notice2005.img"` — the 2005 disclaimer image, gzipped.

## It progresses but is slow (`VULCAN4_W203_PARSE`)

`[w203:parse]` logs each entry with the decompressor struct:

```
entry n=1  [0x10]=0x1036d7f [0x20]=0x1051a40
entry n=7  [0x10]=0x12bf0bd [0x20]=0x12bf100     <- the 6.1 MB memset buffer
entry n=23 [0x20]=0x16bb384
entry n=25 [0x20]=0x16f3f30
entry n=38 [0x20]=0x170fb99
```

The **output pointer advances** (`0x1051A40 → 0x106DAEC`, then `0x12BF100 → 0x170FB99`), so the
decompressor is **not stuck** — it is working through the 6.1 MB buffer. But the rate is tiny:
**~39 entries in 60 s, ~8.8 KB of output per entry ≈ 5.7 KB/s**, i.e. only ~28 % of the 6.1 MB buffer
after a full 60 s run. A PS2 gzip decode runs at MB/s, so this is ~1000× slow, not the known ~4×
(0.26×) overall factor — the decompressor's inner loop is the pole.

## The advance flag is never reached

`[w203:flag]` counts entries to `0x1000DC0`/`0x1000E00`/`0x1000E30` — **0 in every run** (60 s and
180 s). `0x1000E00` is the only path that sets `[0x1047A84]=1`, the disclaimer thread `0x1000BA0`'s
exit flag. So the disclaimer cannot advance: `sub_01000558` is still inside the decompression when the
budget expires.

## And the derail truncates the long runs

The runs that would give the decompressor enough time are exactly the ones that **derail**
(`pc_outside_generated_table`, `functions_entered≈2100–2500`) — the W196/W202 corrupted-`$ra` race.
The good shape (`functions_entered=37898`) does not derail but is rare and still only reaches ~28 % of
the buffer in 90 s.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
The decompressor's throughput: is the inner bit-decode loop the bottleneck, or is it starved by the
decode spin `sub_010088E8` (33 % of all transfers in the common shape)? Measure the guest's instruction
split between `sub_0100F390`'s loop and `sub_010088E8`, then either speed the inner loop or find why it
runs at 5.7 KB/s. And the derail (W202) still truncates the long runs.

---

## W207/W208 — CORRECTION: the parse is FAST; the earlier "5.7 KB/s" was the entry rate, not the decode rate

The W203 conclusion above is **wrong on the rate** and must be corrected. Adding wall-clock stamps
(`VULCAN4_W203_PARSE`, `[w203:parse] entry n=.. wall_ms=..`) shows the decompressor is **not slow**:

```
entry n=1  wall_ms=0    [0x20]=0x1051a40
entry n=3  wall_ms=2    [0x20]=0x105cb32
entry n=30 wall_ms=720  [0x20]=0x187a5ec
entry n=49 wall_ms=731  [0x20]=0x189313a     <- ~99% of the 6.1 MB buffer (end 0x18953CC)
```

**~49 blocks in ~730 ms.** The earlier "5.7 KB/s" divided 60 s by the number of *entries*, but the
guest does other work between entries, so it measured the schedule, not the decompressor. The parse
itself is fast.

`VULCAN4_W206_COPY` on the inner copy loop `0x100F800` confirms it **converges**: the length `s3` is
small (5–0x102) and the source `s1` advances run to run (`0x010601E6 → 0x0156391B`), with `s2 = s1 +
len`.

The parse's caller `sub_0100F8C8` (`[w203:f8c8]`) is a loop that runs **~48 iterations**
(`0x10101B0 → 0x100EDC8`, `0x10105D8 → 0x100EDC8`, `0x1010A40 → 0x100EDC8`, `0x1010A68 → 0x100F390`)
and then returns.

## Where the time actually goes

`VULCAN4_W188_DISP` shows the chain **does unwind** past the parse:

```
n=15 from=0x10043A4 -> 0x100F8C8     (the parse caller)
n=16 from=0x1004424 -> 0x1010BD0     (sub_01004308 continues AFTER 0x100F8C8 returns)
n=17 from=0x1004530 -> 0x101D2A0     (sub_01004500 continues)
```

so `sub_01000558` is waiting on a **long sequence of setup calls** (`0x10047C0 → 0x1004500 →
0x1004308 → 0x100F8C8 → 0x100F390 → 0x1010BD0 → 0x101D2A0 → …`), not on the decompressor.

And the good-shape XFER (w208b, `FE=14797`, `halt=wallclock_deadline`) is dominated by the **render
thread**, not the parse:

```
top: 0x0100afa0=59163(44.99%) 0x0100b678=22394(17.03%) 0x0100d380=16368(12.45%)
THREAD id=1 status=Ready pc=0x100f800   (the parse/setup, prio 3)
THREAD id=2 status=Running pc=0x101f348 (the render, prio 2)
```

So the setup chain (tid1, prio3) runs against a render thread (tid2, prio2) that spends **45 % of all
transfers in its RTOS vsync wait `0x100AFA0`**; `[w173:switch]` shows the two threads alternating
slices (~65536 eeCycles each). The setup chain does not finish inside the budget because it is a long
call tree sharing the CPU with a spinning render loop.

## Corrected NEXT
Instrument the **whole setup subtree** (`sub_010047C0`'s call tree) to find the one call that is
entered most / never returns, rather than the parse. And decide whether the RTOS wait `0x100AFA0`
should block (yield the CPU) instead of spinning, since it is the single biggest transfer site.

---

## W209 — the block is inside the decompressor's LAST block: the copy loop runs on while the stream pointer stalls

`VULCAN4_W209_SUB` (setup-subtree dispatches) in a good shape (`w209c`, `FE=52099`,
`halt=wallclock_deadline`) shows the chain reach the parse caller and stop:

```
n=17 0x10047E0 -> 0x100B6F8    (sub_010047C0)
n=18 0x10047EC -> 0x1004500
n=19 0x100451C -> 0x1004308
n=20 0x1004360 -> 0x1000FD8    (6.1 MB memset)
n=21 0x1004368 -> 0x1010B10
n=22 0x100438C -> 0x100ED78
n=23 0x10043A4 -> 0x100F8C8    (the parse caller)  <-- and no continuation
```

`0x1004308`'s next call after `0x100F8C8` (`0x1004424 -> 0x1010BD0`) **never happens** — so the chain is
blocked inside `sub_0100F8C8`/`sub_0100F390`, not after it.

`[w203:parse]` in two good shapes stops at the **same block**:

```
n=46 wall_ms=647 [0x20]=0x13945E3  [0x34]=0 [0x3c]=0   (a NEW stream: output pointer jumps back)
n=47 wall_ms=663 [0x20]=0x139478E  [0x34]=0 [0x3c]=0
n=48 wall_ms=730 [0x20]=0x139478E  [0x38]=0xff [0x3c]=0   <-- SAME output pointer
```

and `[w203:parse]` then stops (47–48 entries total; the run goes on for another 100 s). `[w206:copy]`
on the inner loop `0x100F800` shows it is **still running** — `s1` advances (`0x015F206A → 0x01776C05`,
~1.6 MB) with small `s3` (3–0x6D) per pass — while the decompressor's stream/output pointer **stalls**
at `0x139478E`. So the decompressor is grinding through one block whose byte-copy runs long without the
stream advancing.

## Corrected wall, in one line
`sub_01000558` is blocked inside the gzip decompressor `sub_0100F390` (via `sub_0100F8C8`): after ~46
fast blocks it hits a block whose copy loop `0x100F800` runs on (s1 advancing ~1.6 MB) while the
stream pointer stalls at `0x139478E`, so `0x1000E00` is never reached and the disclaimer never advances.

## NEXT
Dump the block's input bytes at `[s0+0x10]`/the stalled stream pointer and the loop's exit condition in
`sub_0100F390`; decide whether the input is a corrupt stream (from the XOR/decrypt path) or a genuine
long block. That is the next measurement.
