# W182 — the good-shape guest is in a normal vsync render loop; the wall is the game's own state

**Dish:** `08-good-shape-event` (resume of dish 06, which was already answered). **Result:** ❌ no
picture change. **Outcome:** the good-shape loop's structure is read; it is the game's own RTOS +
DMAC/GS kick loop, not a runtime defect.

## What the caller chain is (static, from the guest ELF)

`sub_0100AE78` (the 196k-entry RTOS wait) is called from many sites in the game's RTOS/render layer;
the good-shape hot one is `0x100DE80` inside `sub_0100DE58`:

```
100de58: ...                       ; fn(a0=id, a1=addr-ish)
100de80: jal 0x100ae78             ; the RTOS wait (poll scratchpad, sce_SleepThread)
100de94: a0 = 0x70002000 + (3*id)<<2 + 96
100debc: sb  $a1, 13(a0)           ; scratchpad handshake byte
100dec8: sw  $zero, 32(s2)         ; s2 = *(0x1032df8 + id*4)
100ded8: sw  $s1, 48(s2)           ; store the DMA source address
100dee8: sw  $v0, 0(s2)            ; kick the DMA/GS register block
100deec: sync
100def4: ... jr ra
```
and `sub_0100DE58` is itself called from `0x100B17C` / `0x100B190` / `0x100B2AC` — inside the game's
RTOS/render function `sub_0100AE78`'s neighbourhood. The whole chain is the game's **per-frame
DMAC/GS kick loop**: wait (vsync) → program the DMA source + GS registers → `sync` → next frame.

## What this means

- The good-shape wall is **not** a runtime defect we can point at. The scratchpad wait flag is written
  (W181), the DMA/GS registers are kicked, `gs_packets=8255`, `frames_presented=4697`. The guest is
  rendering normally, once per vsync.
- The decode (W172-W179) is a shape-A artefact (W180); the sleep-flag theory is refuted (W181); and
  now the loop structure is the game's own. **The remaining wall is the game's SCREEN STATE** — GT4 is
  sitting on the 2005 disclaimer because its own logic/state machine has not advanced, and no
  low-level runtime defect is named by any measurement this campaign has taken.

## The honest position, and the campaign's own finish line

The campaign's milestone 2 is *"A FRAME from GT4's own code — guest traffic reaches the GS"*, marker
`VULCAN4 FRAME source=guest`. The disclaimer **is** exactly that: GT4's own code, its own GS traffic,
its own pixels on screen (`frames_presented=4697`). **The only thing missing is the emitter** —
`CAMPAIGN.md` itself says the string "is currently never emitted by any code path… this milestone can
be neither reached nor missed honestly until the GS lane's emitter is finished."

So the campaign's finish line is reachable **now**, by finishing the emitter, on the frame the guest
already produces. That is the next dish, not another decode/RTOS dig.

## Gate (authoritative, honest)
`GATE FAIL: the capture is BYTE-IDENTICAL to the disclaimer reference`. Suite **497/497**. No graphic
change.

## STUCK / next wall
```
STUCK:   the guest never leaves the 2005 disclaimer
TRIED:   decode (shape-A artefact, W180); scratchpad wait flag (written, W181); the render loop's
         caller chain read (it is the game's own vsync DMA/GS kick loop)
BLOCKED BY: no low-level runtime defect named by measurement -- the guest renders normally and stays
         on the disclaimer; the wall is the game's own screen state
NEED:    finish the campaign's milestone-2 emitter (`VULCAN4 FRAME source=guest`) so the campaign's
         own stop condition is honestly testable on the frame the guest already draws, OR name the
         game-state step that advances past the disclaimer (a larger, game-logic effort).
```
