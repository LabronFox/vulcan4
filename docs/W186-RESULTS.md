# W186/W189 — the early barrier is NOT the dominant wall; the dominant wall is a wild jump

**Dish:** `12-barrier-invocation`. **Result:** ❌ no picture change. **Outcome:** the barrier is named
and measured (P2), and it is shown to be **not** the dominant stall this session — a wild jump off the
render thread is. **Probes:** `VULCAN4_W160_BARRIER`, `VULCAN4_W173_SWITCH` (both OFF by default).

## Shape distribution — measured first, because the dish's premise is stale

Four 45 s boots (`boot_w189a..d`, entries=3,000,000):

| run | `functions_entered` | `halt` | top XFER site |
|---|---|---|---|
| w189a | 2145 | `pc_outside_generated_table` dead=`0x1003A020` | `0x100d380`=16368 (46%) |
| w189b | 2247 | `pc_outside_generated_table` dead=`0x1003FC00` | `0x100d380`=16368 (46%) |
| w189c | 5982 | `livelocked_in_syscall` | `0x1005890/0x10089c8/0x10089d4` = 33.32% each |
| w189d | 2422 | `pc_outside_generated_table` dead=`0x00000000` | `0x100d380`=16368 (41%) |

**3/4 boots derail with `pc_outside_generated_table`; 1/4 is the shape-A decode spin. The barrier at
`0x100D908` is a *minor* site (`w189b`: 991 = 2.77%).** So the "dominant early barrier" of the dish
brief is not what this session produces.

## P2 — the barrier, named and measured

`[w160:bar]` on every entry to the barrier's inner `jal` at `0x100D908`:

```
s0=0x1045970 s1=0x70002088 s2=0x0 *s1=0x1045970 [node=0x1045970 next=0x0 tid=0x2]
```

- The handler (`sub_0100D838`, the VSync/GS handler) walks a **one-node list** at `0x1045970`; the
  queue head is the scratchpad word `0x70002088` (`*s1 == s0`). `next=0`, so the inner `bnez s0`
  **falls through and the walk terminates** — the handler returns.
- The callee `0x10202E8` compares `sce_GetThreadId()` (`0x2F`) with the node's `tid` (`=2`). The
  running thread **is** tid2 (`runnable_threads=tid1@prio3:pc=0x100f800(ready),tid2@prio2:...(running)`),
  so the compare matches and the barrier releases.
- The handler is **re-entered once per VSync**, not stuck: `inv_by_kind=[intr=3336,...]`,
  `intr_run=3340`, `vblanks_processed=333`.
- W173 confirms **both threads run**: `[w173:switch]` shows 154 switches to tid1 and 146 to tid2.

So the barrier is a real gate but it **works** here (it terminates). The W122 "never returns / target
field is 0" reading belonged to a *different shape* (node `0x1047B4C`, field `0x1047B50` written 0 by
the node-reset `0x1011588`). In this session's barrier shape the node is `0x1045970` with `tid=2`.

## The dominant wall — a wild jump off the render thread

`VULCAN4 WILDPC` on the derailing runs:

```
w189a dead=0x1003A020 last_good=0x0100afa8 ra=0x010294b4 sp=0x01ffbf60 v0=0x1003a020 s0=0xa5555803
w189b dead=0x1003FC00 last_good=0x0100f800 ra=0x1003fc00 sp=0x010459e0 v0=0xffffffff s0=0x6b5d03b5
w189d dead=0x00000000 last_good=0x0100afa8 ra=0x00000000 sp=0x01ffbff0 v0=0x00000001 s0=0x00000000
```

- `0x1003FC00` and `0x1003A020` lie in **`.rodata`** (`.rodata` = `0x1036D80..0x10401F8`), i.e. the
  guest jumped into read-only **data**; `w189d` jumped to **NULL**. All three have `ra` garbage.
- The jump is the **`jr $ra` at `0x100B044`** (epilogue of `sub_0100AE78`, the RTOS/render wait that
  W181/W182 named). `ra` is restored from `sp+104` at `0x100B040`, so the saved `ra` is corrupt.
- The registers at the jump are garbage (`s0=0x6B5D03B5`, `a0=0x637508B0` in w189b), and the harness
  reports the derailing thread is **tid2** (`tid2:pc=0x1003fc00(running)`), the disclaimer/render
  thread (entry `0x1000BA0`).
- `missing_functions=0` — no generated function is missing; the PC is simply outside the table.

So the wall is **not** a wait and **not** a missing stub: the render thread returns to a corrupted
`$ra` and jumps into `.rodata`/NULL. That is the thing to chase.

## Gate (authoritative, honest)
`GATE FAIL` (v3). Suite **497/497**. No graphic change.

## NEXT
Instrument the `$ra` save/restore of `sub_0100AE78` (`sd ra,104(sp)` on entry, `ld ra,104(sp)` at
`0x100B040`) across a derailing run and name who overwrites the slot — the parse/decrypt chain
(`0x100F390`, `0x100D380` XOR loop) or the scheduler's resume of the RTOS wait. The barrier (dish 12)
is closed as **not dominant**.
