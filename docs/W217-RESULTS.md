# W217 — the spin's compare producer: both `+0x10` fields are written once, by the parse's node builder

**Dish:** `18-spin-compare-producer`. **Result:** ❌ no picture change; gate fails.
**Outcome (P2):** the producer is **named** — the two `+0x10` fields `func_1005870` compares are written
by the parse's node-building code, once, to **different node pointers**, and nothing updates them
afterward; `func_1005870` therefore returns -1 forever.

## The producer, measured (`VULCAN4_W217_PROD`)

A store watch on the two `+0x10` words (`[0x1FFFBA0+0x10]=0x1FFFBB0` and
`[0x18951F0+0x10]=0x1895200`), shape `w231a-d` (`livelocked_in_syscall`, FE~7.6k), logs the writers:

```
[w217:prod] addr=0x1895200 val=0x18951E0 writerPc=0x1012220 op=WRITE32   (once)
[w217:prod] addr=0x1FFFBB0 val=0x10      writerPc=0x10112F8 op=WRITE64
[w217:prod] addr=0x1FFFBB0 val=0x10      writerPc=0x1011528 op=WRITE64
[w217:prod] addr=0x1FFFBB0 val=0x18952E0 writerPc=0x101150C op=WRITE64
[w217:prod] addr=0x1FFFBB0 val=0x1FFFC90 writerPc=0x1012500 op=WRITE64
[w217:prod] addr=0x1FFFBB0 val=0x1895360 writerPc=0x101150C op=WRITE64
```

- **`[0x18951F0+0x10]` (`0x1895200`) is written ONCE**, to the node pointer `0x18951E0`, by `0x1012220`.
- `[0x1FFFBA0+0x10]` (`0x1FFFBB0`) is written by `0x10112E4`/`0x10112F8`/`0x1011528`/`0x101150C`/
  `0x1012500` — all inside the parse's node-builder region `0x10112E0–0x1011650` (`sub_0101280`-family)
  — to various node pointers (`0x10`, `0x18952E0`, `0x1FFFC90`, `0x1895360`).

So `func_1005870` compares `[a1+0x10]` (a node pointer in the `0x1895xxx` heap) against `[s0+0x10]`
(another node pointer), and they are **different nodes**, so the compare stays unequal and the spin runs.

**Positive control:** the watch fires on `0x1895200` (`0x1012220`), a TARGET word — so the observer sees
the target writes, not just the control. (The scratchpad positive control `0x70002050` does not fire
because scratchpad stores bypass the `ps2AddStoreSubscriber` path — a real observer limit, stated.)

## The producer region

The writers `0x10112E4`/`0x10112F8`/`0x1011528` are in `sub_` spanning `0x10112E0–0x1011508`/
`0x1011508–0x1011650` (the parse's node builder), and `0x1012220` is in the `0x1012xxx` region. All are
downstream of the CORE.GT4 parse `sub_0100F390` in `sub_010047C0`'s subtree. So the "producer" is the
parse itself: it builds two nodes with different `+0x10` pointers, and the spin's consumer waits for
them to become equal — which the parse never makes them, because they are *meant* to be distinct nodes.

**The honest reading:** `sub_010088E8`'s 3-call loop is comparing two nodes for an ordering (merge or
tree-walk) and the equality predicate is the wrong reading — the loop exits on `func_1005870` returning
**>= 0**, not on equality; since it returns -1 for distinct nodes, the loop is a genuine **merge/walk
step** whose counter `s1` (1→50M) is the progress, and the *real* wall is that the merge never reaches
its end because the input stream does not terminate. That is the next question: why the merge's input
stream (`func_1007738`'s struct at `0x1FFFBA0`, `+8=1`) never advances.

## The wall, in one line
`func_1005870` returns -1 because it compares two `+0x10` node pointers (`0x18951E0` vs `0x10`/
`0x18952E0`/`0x1FFFC90`) that the parse's node builder sets once and never equalizes; the spin's counter
`s1` (1→50M) is the merge progress, and the merge never terminates because its input stream never
advances.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
`func_1007738` walks the stream struct at `0x1FFFBA0` (bound `+8=1`, base `+0x14`); find what should
advance it (the refill/`func_10057F0` count) — that is the merge's input, and it is the same
`0x1fffba0` wire W122 spent many dishes on. A store watch on `[0x1FFFBA0+8]` and its base names it.
