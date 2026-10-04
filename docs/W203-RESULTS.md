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
