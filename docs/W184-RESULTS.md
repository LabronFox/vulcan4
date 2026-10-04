# W184 — GT4's screen state located: a fade/`screen` loop gated on `0x1047A84` / `0x1047A88`

**Dish:** `10-game-state-from-disclaimer`. **Result:** ❌ no picture change. **Outcome:** the game's
screen path is read from the ELF; the state words and the advance branch are named (P2).

## The screen function (0x1000BA0) and its loop

```
1000ba0: addiu sp,sp,-96            ; fn entry
         ... call 0x100a7a8, 0x100aaf0, 0x100aab8  (set the 640x448 display)
1000c10: jal 0x100d2c8              ; get a mode/state -> v0
1000c18: li v1,4
1000c1c: bnel v0,v1,0x1000c30       ; if v0 == 4, call 0x100b6f8(1)
1000c30: lw  v0,31364(s7)           ; s7=0x1040000 -> v0 = [0x1047A84]
1000c34: bnez v0,0x1000d3c          ; *** if [0x1047A84] != 0 -> EXIT the fade loop ***
1000c40: addiu v1,s6,-9048          ; s6=0x1030000 -> v1 = 0x102DCA8  (the table)
1000c44: lw  v0,0(v1)
1000c48: bltz v0,0x1000d3c          ; *** if table[0] < 0 -> EXIT ***
   ... fade loop: s1 = 0..150, calling 0x1000a48 with a fade fraction (div.s f12,f0,15.0)
1000d10: slti v0,s1,151
1000d1c: addiu s2,s2,4              ; advance table index +4
1000d20: addiu s4,s4,4
1000d24: move  s1,zero
1000d30: lw  v1,0(v0)               ; next table entry
1000d34: bgez v1,0x1000c68          ; *** loop while entry >= 0 ***
1000d3c: (exit) mtc1 zero,$f12 ; li a1,-1 ; jal 0x1000a48   ; final render
```
Flags set by the neighbouring functions: `0x1047A84` = 1 at `0x1000DB0` (`sw v0,31364(v1)`), and
`0x1047A88` = 1 at `0x1000DDC` (which also calls `0x101f460`/`0x101f440`, the sema/thread wrappers).
The render-layer callers (W184b) are `0x1000CEC`/`0x1000D44`.

## The table `0x102DCA8` (from the ELF, .data)

```
0x102DCA8: 00000000 ffffffff 00000000 00000000 ...   -> entry0 = 0, entry1 = -1
```
So this instance of the loop renders **one** entry for 151 frames, then entry1 (`-1`) makes it exit.
The table is in the guest image but plausibly rewritten at runtime.

## What is named (dish P2)

- **The screen/advance gate:** the loop runs while the table entry at `0x102DCA8 + s2` is `>= 0`;
  it exits when an entry is negative **or** `[0x1047A84] != 0`.
- **State words:** `0x1047A84` (screen-done/skip flag, set at `0x1000DB0`) and `0x1047A88`
  (set at `0x1000DDC`, which touches the thread/semaphore layer).
- **The advance branch:** `0x1000C34`/`0x1000C48`/`0x1000D34`.

## Gate (authoritative, honest)
`GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference`. Suite **497/497**. No graphic
change.

## NEXT wall
Instrument `0x1047A84` / `0x1047A88` (store watch) over a run and report their values and writers: if
they never change, the screen loop is stuck before the advance; if they change but the screen stays,
the wall is the table at `0x102DCA8` (whose entries the game writes) or the caller's dispatch. The
dispatcher that calls `0x1000BA0` has no static `jal` (a runtime/computed pointer), so watch the
function-pointer table too.
