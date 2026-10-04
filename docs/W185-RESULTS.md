# W185 — the screen dispatcher is DYNAMIC: no static pointer table calls `0x1000BA0`

**Dish:** `11-screen-dispatcher`. **Result:** ❌ no picture change. **Outcome:** the dispatcher's
mechanism is narrowed — it is not a static table, and the screen loop is only reached in the good
shape (which is rare right now).

## Measurement 1 — no static dispatcher table

Scanned the ELF for pointers into the screen-function range `0x01000000-0x01000FFF`:
```
.data   (0x0102DC80..0x01036D67): 1 pointer  -> 0x01036804 = 0x01000000  (the ELF entry, not a screen)
.rodata (0x01036D80..0x01040200): 0
.rdata  (0x01040200..0x01042C80): 0
```
and the disassembly has **no** `addiu ...,0xba0`, and **no** direct `jal 0x1000ba0`. So the call to the
screen function `0x1000BA0` is **computed at runtime** (or reached by fall-through / a table built in
`.bss`/heap by the game's own loader). There is no static index to read.

## Measurement 2 — the screen loop is a good-shape path

The screen fn `0x1000BA0` is only reached once the guest is far along. This session the dominant shape
is the **early barrier** (`top 0x0100d908`, `FE~2000`), and prior sessions the **decode** (`top
0x01005890`); the good shape (`FE=37898`, top `0x0100afa0`) is rare. So the screen-state probe
(`VULCAN4_W186_SCREEN`) only produced the 22 setup writes (`0x1047A80=9`, `0x1047A84=0`,
`0x1047A88=0`, table `0x102DCA8` entries), never a full screen advance -- because the boot stalls
earlier.

## What is named (dish P2, partial)

- **No static dispatcher** exists: the call into `0x1000BA0` is dynamic. Candidate mechanism: the game
  builds a screen-vtable in `.bss`/heap at runtime (its own resource/overlay loader), so the "current
  screen" pointer cannot be read from the image.
- The screen advance gate and branch are known (W184): loop while table entry `>= 0`; exit when an
  entry is negative or `[0x1047A84] != 0`; state words `0x1047A84`/`0x1047A88`.
- `GT4.VOL` (2.29 GiB) is **not** extracted in `/mnt/ssd/gt4/work/` — only `vol_head.bin` (8 MiB). If a
  screen advance depends on data from `GT4.VOL`, that input is absent (a resource question, out of
  reach without the disc's full extraction).

## Gate (authoritative, honest)
`GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference`. Suite **497/497**. No graphic
change.

## NEXT
Two prerequisites before the screen dispatcher is even reachable this session:
1. the **early barrier** (`0x100D908`, dominant now) must be passed -- dish `12-barrier-invocation`;
2. once the guest reaches the good shape reliably, read the dynamic screen pointer (watch `jalr` sites
   once the screen loop runs) and the `0x102DCA8` table index at halt. Also check whether the screen
   advance needs `GT4.VOL` data that is not extracted.
