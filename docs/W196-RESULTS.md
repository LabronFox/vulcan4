# W196/W197/W201 — the corrupted `$ra` is a WRITTEN stack slot (mechanism 3), not a wrong read

**Dish:** corrupted return address in `sub_0100AE78`. **Result:** ❌ no picture change; gate still
fails. **Outcome (P2):** the mechanism is named with measurements — it is candidate **3 (the saved
`$ra` slot is written by something else)**, and candidates 1 and 2 are ruled out for this function.
The exact store is not yet pinned because every per-store instrument perturbs the race away.

## The derail, measured

Repeated `pc_outside_generated_table` derails on render thread **tid2**, all with the same shape:

```
VULCAN4 WILDPC dead=0x1003FC00 last_good=0x0100F800 ra=0x1003FC00 sp=0x010459E0 v0=0xFFFFFFFF
VULCAN4 WILDPC dead=0x0240302D last_good=0x0100F800 ra=0x0240302D sp=0x010459E0 v0=0xFFFFFFFF
VULCAN4 WILDPC dead=0xA303C50C last_good=0x0100F800 ra=0xA303C50C sp=0x010459E0 v0=0xFFFFFFFF
```

`ra == dead` (a return through a bad `$ra`), `sp=0x010459E0` (tid2's stack top), and the harness
`thread_state` confirms the derailing thread is `tid2` (entry `0x1000BA0`), while `tid1` sits Ready at
`0x100F800`.

## Mechanism 3 — the slot was WRITTEN (not a wrong-slot read)

The harness now dumps the stack window around the slot at the derail (`VULCAN4 W195 slotwin`):

```
W195 slotwin sp=0x010459e0 [sp-16]=0xb10048df [sp-12]=0xb20050df [sp-8]=0xb30058df
             [sp-4]=0xbf0060df [sp]=0xb40068df [sp+4]=0xe00008c7 [sp+8]=0xbd007003
             | ra==[sp-8]? YES (slot written)
```

**`ra == [sp-8]` in every derail.** The epilogue's `ld ra,104(sp)` read exactly the slot the prologue
`sd ra,104(sp)` saved into, and that slot holds garbage. So the restore is NOT reading the wrong
address — the slot was overwritten. The whole window `[sp-16, sp+8]` is packet-like data (note the
`0xXX0048df/0xXX0050df/0xXX0058df/…` incrementing pattern in the run above, and a repeating
half-word pattern in others), and the garbage `s0` also appears in the window — a **bulk write of a
data buffer over the frame**, not a single field clobber.

## Candidates 1 and 2 are ruled out for `sub_0100AE78`

From `mips-linux-gnu-objdump -d SCUS_973.28`, the whole function `0x100AE78-0x100B048`:

```
100ae78: addiu sp,sp,-112        ; frame
100aec8: sd    ra,104(sp)        ; the ONLY save of $ra
100b040: ld    ra,104(sp)        ; the ONLY restore of $ra
100b044: jr    ra                ; the ONLY exit
100b048: addiu sp,sp,112         ; (delay slot) -- frame is symmetric
```

- **1 (callee clobbers `$ra` without saving):** the function saves `$ra` before any nested `jal`, and
  its callees (`0x101F310`, `0x10284D8` = `Di`, `0x1028528` = `Ei`, `0x101F340` = `SleepThread`,
  `0x101F370`) are leaf syscall/`di`/`ei` wrappers with no stack writes.
- **2 (frame imbalance):** one entry, one exit, matching `-112`/`+112`. No early return.

So the remaining candidate is **3**, confirmed by the `ra == [sp-8]` measurement.

## The writer runs between SleepThread and the VSync handler

A per-dispatch slot check (`VULCAN4_W197_SLOT`) reports the value changing exactly in the interval

```
prevDispatch = 0x100afa0 -> 0x101f340   (sub_0100AE78's SleepThread call)
thisDispatch = 0x100d908 -> 0x10202e8   (the VSync/GS handler's GetThreadId call)
```

and the new value is the wild `$ra` (`0x1003FC00` / `0x0240302D` / `0xA303C50C`). So on tid2 the slot
is overwritten across the `SleepThread` → VSync-handler transition. (The handler itself runs on a
fresh `reserveAsyncCallbackStack` stack — `SET_GPR_U32(&invocation.context, 29, 0u)` at
`EeScheduler.cpp:1796` then `invocationStackTop()` — so it is not simply the handler's own frame.)

`VULCAN4_W201_RESUME` shows `sub_0100AE78` **is** re-entered at its resume labels (`0x100AFA8`,
`0x100AF50`) with the frame base intact (`sp=0x01045970`), so the resume path preserves `sp` correctly.

## The instrument problem (why the exact store is not yet pinned)

The corruption is a **rare race**: without any store watch, 3 of 8 boots derail; with a per-store
watch (the `W122` subscriber, or the harness `VULCAN4_WATCH_LO/HI` observer, ~9x) **0 of 6–8 boots
derail**. Every per-store instrument perturbs the timing enough to prevent the event. That is the
same observer-effect family this project has hit before, and it is why the writer pc is not in hand.

## What would pin it (the change needed)

A **non-perturbing capture** of the store to `0x010459D8`: a hardware watchpoint on the host RDRAM
address (`rdram + 0x010459D8`) under gdb, or a guard page mapped over the slot, so the exact
generated pc is recorded without a per-store hook. Then the same store can be reproduced and fixed in
the correct layer (runtime if it is our code, game override if it is one guest function, recompiler
if it is a codegen class).

## Gate (authoritative, honest)
`GATE FAIL` (v3). Suite **497/497**. No graphic change.

## NEXT
Non-perturbing watchpoint on `0x010459D8`; the writer runs on tid2 in the `SleepThread` → VSync-handler
window. If it proves to be runtime code writing the guest stack, that is the fix; if guest code, a game
override.
