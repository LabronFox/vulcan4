# W204 — Track B refuted for tid2; the async-callback pool overlaps the MAIN thread's stack

**Dish:** `14-corrupted-ra-race-deterministic`. **Result:** ❌ no picture change; gate still fails.
**Outcome (P2):** the runtime-overlap hypothesis is **settled with numbers** — for tid2 it is **refuted**,
and a **different** overlap is found: the invocation-stack pool overlaps the **main thread's** stack.

## Track B — the numbers (`VULCAN4_W204_STACK`)

`EeScheduler::invocationStackTop()` and `PS2Runtime::reserveAsyncCallbackStack()` now log their regions:

```
[w204:pool]    allocSize=0x4000 topBefore=0x2000000 base=0x1ffc000 floor=0x1f00000
               nextTop=0x1ffc000 ret=0x1fffff0
[w204:pool]    allocSize=0x4000 topBefore=0x1ffc000 base=0x1ff8000 ... ret=0x1ffbff0
[w204:invstack] tid=1 depth=0 top=0x1fffff0 (range [0x1ffbff0,0x1fffff0])
[w204:invstack] tid=2 depth=0 top=0x1ffbff0 (range [0x1ff7ff0,0x1ffbff0])
```

- **tid2's live frame** is `[0x01045970, 0x01045A80)` (`sp=0x010459E0`, saved-`$ra` slot `0x010459D8`;
  the thread was created with `stack=0x1041A80, size=0x4000`).
- **The invocation stacks** are `0x1FF7FF0–0x1FFFFF0` — about **250 MB above** tid2's frame. **No
  overlap.** Only two stacks are ever allocated (keys `(tid,depth)` with depth 0), so the pool never
  grows down toward tid2.

**Hypothesis dead for tid2: the writer of `0x010459D8` is not our async-callback stack pool.**

## But the pool DOES overlap the MAIN thread's stack

The harness sets the main guest `$sp` to `PS2_RAM_SIZE - 0x10 = 0x1FFFFFF0`
(`vulcan4_harness.cpp:1579`), and the main frame runs down to `~0x1FFC760` (the parse's frame). The
pool's **first** stack is `[0x1FFBFF0, 0x1FFFFF0]` — **inside that range**. So a VSync handler that
runs on the **main** thread writes its frame into the main thread's live frame.

- This is a real runtime defect: the callback-stack pool and the main guest stack are both allocated
  downward from `0x2000000` with no separation.
- It is **on tid1, not tid2**, so it is **not** the writer of tid2's slot — but it is a corruption of the
  parse thread's frame and a strong candidate for the parse going wrong.

**What would have to change (fix not yet landed, deliberately):** reserve the main guest stack's size
below `0x1FFFFFF0` and start `m_asyncCallbackStackTop` under it (e.g. `m_asyncCallbackStackTop =
0x1FFFFFF0 - mainStackReserve`), or place the pool in a region the guest provably does not use. The
safe reserve size is unmeasured, so this was not landed blind.

## Track A — the inline slot-watch perturbs the race too

A cheap inline check was added to the store macros (`VULCAN4_W205_WATCH`, 32- **and** 64-bit stores):

```
if (::vulcan4W205WatchOn() && ((addr & 0x1FFFFFFF) == 0x010459D8) && (value > 0x02000000)) log
```

It costs three compares per store — far less than the 9x observer — and it still removes the event:

| build | runs | derails |
|---|---|---|
| `VULCAN4_W205_WATCH` **off** | 8 | **3** (`pc_outside_generated_table`, FE~2000–2400) |
| `VULCAN4_W205_WATCH` **on** | 16 | **0** |

So the race is **microsecond-sensitive**: even a three-compare branch on the store path changes which
window is chosen. That closes the last observation route: the writer cannot be caught by slowing the run.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
Fix the **main-thread stack / callback-pool overlap** (runtime layer) and re-measure the parse and the
derail rate; if the parse is the true victim, that is the lever. The tid2 writer remains uncatchable by
observation; a deterministic scheduler (`--switchpoint=<n>`) is the only route left for it.
