# W176 — the file→parse link EXISTS: the parse reads CORE.GT4; P2 is refuted

**Dish:** `04-decode-arg2-source` (campaign re-arm). **Result:** ❌ no picture change (gate fails).
**Outcome:** **P2 is REFUTED with a positive control.** The parse DOES read the `CORE.GT4` buffer, so
the empty decode work buffer is **parse-internal**, not a missing file link.

## The probe (new, read-only, OFF by default: `VULCAN4_W176_LOAD`)

A guest **load** observer (`ps2SetGuestLoadObserver` → `ps2R…TraceGuestRead`, hit by every `READ8/16/32/64`)
watching the `CORE.GT4` read buffer `[0x10d1ac0, 0x10d1ac0+0x1ed4c5)` (the real 2,020,861-byte file).

**Positive control (observer proven):** the same run also counts reads of `0x12bf0c4` (a heap/tree node).
It fired; the observer is live. Earlier the control had to be widened because the first run's 60 hits
were all one heap address — the "a zero you cannot invalidate" trap, avoided by re-running with the
file's own byte pattern as the check.

## The measurement (one run, `halt=livelocked_in_syscall`, `functions_entered=18261`)

```
[w176:load] FILE addr=0x10d1ac4 size=4 val=0xbdec005d pc=0x100ed88
[w176:load] FILE addr=0x10d1ac8 size=4 val=0xc55b7c0f pc=0x100f968
[w176:load] FILE addr=0x10d1acc size=4 val=0x747e2f95 pc=0x1010048
...
[w176:load] FILE maxoff=0x40000 pc=0x100f6a8
[w176:load] FILE maxoff=0x80000 pc=0x100f748
[w176:load] FILE maxoff=0xc0000 pc=0x100f748
```

- The values are **CORE.GT4's own bytes** (`0x10d1ac4` = `cc 5e 5d 00`…), so it is reading the file,
  not a coincidental heap address.
- It advances **sequentially** to at least **offset 0xC0000 (768 KB)** before the run ends, at pcs
  `0x100f4c0`, `0x100f748`, `0x100f6a8` — all inside **`sub_0100F390`** (the byte-copy loop itself,
  `0x100f390`-`0x100f8c8`), plus the header readers `0x100ed88`/`0x100f968`/`0x1010048`/`0x101010c`.

**So `docs/W175-RESULTS.md`'s `NEED` is answered: the file→parse link is NOT missing.** P2 does not
hold. The parse consumes the file; the decode's empty `arg2` is built **after** that, inside the
parse/serializer.

## Gate (authoritative, honest)
```
newest capture : /mnt/ssd/vulcan4-build/run/disclaimer-break-W174.png  (byte-identical disclaimer)
GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference — the screen did not change.
```
Suite **497/497**. Probe OFF by default. No picture change.

## NEXT WALL (named)
The parse reads the file (via `sub_0100F390`'s copy loop) but the decode's `arg2` node (data
`0x18953c0`) is born zero and never copied. **Trace the parse's node construction between the file
read and the decode** — the byte-copy destinations in `sub_0100F390` are the thread to pull: watch
where the parse writes the bytes it just read, and which of those buffers should have become `arg2`.
Next dish: `05-trace-parse-node.txt`.
