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

---

## W187 — RETRACTION: `0x1000BA0` is never entered; it is not the disclaimer's driver

Good shapes returned this run (`FE=38090 / 24559 / 39105`, top XFER `0x0100afa0`), and a probe on
**every dispatch whose `targetPc == 0x1000BA0`** (`VULCAN4_W187_DISP`, OFF by default) fired
**0 times** across all six runs. `0x1000BA0` is a real generated function
(`g_ps2RecompiledFunctionTable[...] = sub_01000BA0_0x1000ba0`), and the only callers of the render fn
`0x1000A48` are `0x1000CEC`/`0x1000D44` *inside* it -- yet it is **never dispatched**.

**So W184's "screen loop at 0x1000BA0" is NOT what drives the 2005 disclaimer.** Retracted as the
driver (the loop body is real, but the function does not run in these boots). The `VULCAN4_W186_SCREEN`
state probe likewise only ever saw the **22 setup writes** (`0x1047A80=9`, `0x1047A84=0`,
`0x1047A88=0`, table `0x102DCA8` setup) in every run -- consistent with the function never running.

**What that leaves:** the disclaimer is drawn by the game's running path (the `0x100bxxx` DMA/GS kick
loop, W182) and its advance is driven by some *other* screen/state code, not `0x1000BA0`. The next
measurement is to find the actual disclaimer driver: watch which functions are entered in the good
shape near frame 0, or scan for the screen state variable the running code branches on. No static
pointer table exists (W185). Suite 497/497. Gate v3 FAILS.

---

## W188 — the "dispatcher" is a THREAD CREATION; the advance flag never gets set (P2)

**Dish:** `11-screen-dispatcher` (re-run). **Result:** ❌ no picture change, but the mechanism is
named with a measurement. **Probe:** `VULCAN4_W188_DISP` (runtime + harness, OFF by default).

### 1. The disclaimer is not a function loop — it is a dedicated EE thread

Static: `sub_01000940` builds an `ee_thread_t` on its stack and calls `sce_CreateThread`:

```
10009c8: lui   v1,0x100
10009e4: addiu v1,v1,2976      ; v1 = 0x01000BA0   <-- the thread entry
10009ec: sw    v1,36(sp)       ; ee_thread_t.func  = 0x1000BA0
10009f4: sw    v0,40(sp)       ; stack             = 0x1041A80
10009f8: sw    a2,44(sp)       ; stack_size        = 0x4000
10009fc: sw    a3,52(sp)       ; priority          = 2
1000a00: jal   0x101F220       ; syscall 0x20 = sce_CreateThread
```
`sub_01000940` is called by `sub_01000558` (`0x100057C`), which `main` calls at `0x1000210`
(`jal 0x1000558`). **Measured:** `VULCAN4 THREADS` reports `id=2 entry=0x1000ba0 prio=2`; the syscall
census reports `sce_CreateThread calls=1`. So `0x1000BA0` is **thread 2's entry**, not a dispatch
target — which is exactly why W187's `targetPc==0x1000BA0` probe fired 0 times while its loop body
(`0x1000CEC`) ran. **W187's "0x1000BA0 never runs" is RETRACTED**: it runs the whole time, entered
through the function table's per-resume slots (`0x1000BE0`, `0x1000CFC`, …) by the harness/scheduler,
never through `dispatchGuestBranch`. A `[w188:ba0]`/`[w188:top]` probe confirms 0 dispatch targets.

### 2. The advance branch and the flag

`0x1000BA0` fades the table at `0x102DCA8` (`entry0=0`, `entry1=-1`) and exits the fade when
`[0x1047A84] != 0` (`0x1000C34`) or a table entry `< 0` (`0x1000C48`/`0x1000D34`). `[0x1047A84]` is
set by `sub_01000DB0`, whose **only** caller is `sub_01000E00` (`0x1000E08`), whose **only** caller is
`sub_01000558` (`0x1000618`).

### 3. Why it is never taken — the screen setup blocks in the resource parse

`[w188:setup]` traces every call `sub_01000558` (and its callee `0x10047C0`) makes. Across **4 runs
including a good shape (`FE=23776`)**, the sequence stops at the same place:

```
n=6 0x10005AC -> 0x1003B90   (virtual call)
n=7 0x10005B8 -> 0x101ED00
n=8 0x10005D4 -> 0x10047C0   <-- last call sub_01000558 itself makes
n=9  0x10047E0 -> 0x100B6F8
n=10 0x10047EC -> 0x1004500
n=11 0x100451C -> 0x1004308
n=12 0x1004360 -> 0x1000FD8  ([MEMSET] len=6,119,116 at 0x12BF100)
n=13 0x1004368 -> 0x1010B10
n=14 0x100438C -> 0x100ED78
n=15 0x10043A4 -> 0x100F8C8  (calls 0x100F390 = the CORE.GT4 byte-parse, W176-W179)
```
**`0x1000DC0`, `0x1000E00` and `0x1000E30` are entered ZERO times in a full 60 s run** (`[w188:fn]`
shows only `0x1000558`), and `[0x1047A84]` is `0` at every screen event. So the setup never reaches
its `0x1000610`/`0x1000618` calls, the advance flag is never set, and thread 2 re-fades the same
disclaimer forever. The main thread (tid 1) is measured deep in that chain at halt —
`pc=0x100F800 ra=0x1010A70 sp=0x1FFC760` (inside `sub_0100F390`, whose only caller is `0x1010A68`
inside the `0x100F8C8` function reached at n=15) — consistent with the stack grown from the
`0x1000558` frame at `sp=0x1FFFFB0`.

### What is named (dish P2)

- **The "screen dispatcher" is a thread creation, not a pointer table.** `main(0x1000210) →
  sub_01000558(0x100057C) → sub_01000940 → sce_CreateThread(func=0x1000BA0, prio=2)`.
- **Current screen:** thread id 2, entry `0x1000BA0`, fading table `0x102DCA8`.
- **Advance branch:** `0x1000C34` (`[0x1047A84] != 0`) / `0x1000C48`,`0x1000D34` (table entry `< 0`).
- **Why not taken:** `sub_01000558` never completes; it is blocked in the resource-parse chain
  `0x10047C0 → 0x1004500 → 0x1004308 → 0x1000FD8/0x1010B10/0x100ED78 → 0x100F8C8 → 0x100F390`.
- The indirect vtable the dish asked for is the object at `[0x10002C8()+8]`, slot +16; the one
  resolved call in these runs is `0x10005AC → 0x1003B90` (a string-uppercase helper).

### Gate (authoritative, honest)
`GATE FAIL: functions_entered=12217 below 20000 — short nondeterministic shape` (newest capture is
still `disclaimer-break-W174.png`). Suite **497/497**. No graphic change.

### NEXT
The wall is now a **named block inside the screen setup**: the parse chain `0x1004308 → 0x100F8C8 →
0x100F390` does not return. Next dish: instrument `0x100F8C8`/`0x100F390` on the *main* thread and
name why the 6.1 MB parse (`0x1000FD8` memset + `0x100F390`) does not terminate — a spin, a blocked
file read, or a bound that never reaches its end. This re-opens the parse as the wall, but now with a
concrete consumer: the disclaimer cannot advance until it returns.

