# GOAL: W105 — THE INTERRUPT IS DROPPED, NOT MISSED

## WHAT WE ALREADY MEASURED (do not re-derive any of this)

From a 15-second GT4 boot, printed by the harness report:

    vblanks_processed=224   interrupts_raised=0   interrupts_delivered=0

**The guest DOES get vblanks. 224 of them.** The scheduler's deadline queue works,
`processEvent()` is reached, `++m_vsyncTick` runs. **Every single one of those 224
VBlankStart events then calls `dispatchIrq(false, 2u)` and nothing comes out.**

That single line — `vblanks_processed=224` next to `interrupts_raised=0` — is the
wall. GT4's startup ends waiting on a video interrupt, so with 0 interrupts
delivered it parks forever with a Ready thread and never leaves its init. That is
the black screen, and it is why the guest can draw GS packets at ~136 fps and still
never reach a menu: the frame counter and the interrupt counter are unrelated, and
only the second one is what a PS2 boot actually waits on.

## WHERE TO LOOK — EeScheduler.cpp, dispatchIrq, around line 1569

    void EeScheduler::dispatchIrq(bool dmac, uint32_t cause)
    {
        const uint32_t mask = dmac ? m_enabledDmacMask : m_enabledIntcMask;
        if (cause < 32u && (mask & (1u << cause)) == 0u)
        {
            return;                      // <-- SILENT DROP. NO LOG, NO COUNTER, NO RETRY.
        }
        ...
        for (const EeIrqHandler &handler : matching)
        {
            ... queueInvocation(std::move(invocation));   // <-- or matching is EMPTY here
        }
    }

**There are exactly two ways 224 events produce 0 interrupts, and they are
different bugs with different fixes. Establish which one it is FIRST:**

**(A) The mask gate rejects them.** Bit 2 of `m_enabledIntcMask` is clear, because
GT4 has not reached its own `sce_EnableIntc` yet — and a masked interrupt on real
hardware is LATCHED in the INTC, then delivered the moment it is unmasked. If that
is what happens here, the correct fix is to **latch masked causes in a pending set
and drain it when `m_enabledIntcMask` gains the bit**, NOT to delete the mask check.
Deleting the mask check would deliver interrupts the guest never asked for, which is
exactly the kind of plausible-looking wrong answer Law 3 exists to prevent.

**(B) The mask is fine and `matching` is empty.** Then either no handler was
registered for cause 2, or every candidate failed `handler.enabled &&
handler.cause == cause && handler.handler != 0u && m_runtime.hasFunction(...)`.
Note `sce_AddIntcHandler calls=4` in the boot log — four handlers ARE registered.
Find out which cause each one registers for, and whether `hasFunction()` is
rejecting an address that has a generated function.

**A one-line diagnostic answers this and must be the first thing you do:**

In `dispatchIrq`, print on the vblank path (cause==2, dmac==false) the mask value,
`handlers.size()`, and each handler's `enabled`/`cause`/`handler`/`hasFunction`.
One run tells you (A) from (B). Do not guess between them.

## ALREADY TRIED AND REVERTED — DO NOT REDO IT

Removing the `hostDeadline <= pacedNow` term from the gate in
`processDueDeadlines()` (so that an event is due purely on guest cycles) was tried
and **reverted**. It is the wrong fix. Measured: the guest got 3x SLOWER,
287M cycles/s -> 89M cycles/s, and still `interrupts_raised=0`. The reverted code
and its reasoning are in `processDueDeadlines()` — read the W104 comment there
before touching that function. The deadline-gate is not the bug.

## NEW INSTRUMENTATION ALREADY IN YOUR HANDS (use it, do not re-add it)

- `VULCAN4 EVDUE type=N deadline_cy=.. now_cy=.. guest_short=.. host_short=..` —
  prints every 4096 calls from `processDueDeadlines` when nothing is due. `type=1`
  is VBlankStart.
- `VULCAN4 WILDPC dead=.. last_good=.. ra=.. sp=.. v0=.. s0=..` — names the last
  good PC before a guest jumps to an unreachable address.
- `vblanks_processed=` in the BOOT REPORT, next to interrupts_raised/delivered.
- `FRAMES=` on the PROGRESS line is the real guest frame counter (an atomic read),
  not a count of printed lines. Measured ~136 frames/sec.

## SUCCESS, EXACTLY

`interrupts_raised` greater than 0 and `interrupts_delivered` equal to it, from a
real boot log. Then the vblank question is answered and the next wall is whatever
comes after it.

If instead you find the interrupt IS being delivered and the guest still will not
leave startup, that is a SUCCESSFUL negative result and worth more than a guess:
report it as STUCK / TRIED / BLOCKED BY / NEED with the numbers, and name the next
thing to measure.

## HARD RULES FOR THIS DISH

- The captain judges the product. A picture, not a commit. `halts=`, `frames=`,
  a number. If you cannot show it moving, say so.
- No fake speed numbers. W92's "3.6% of PS2 speed" and "28x slower than hardware"
  are BOTH WRONG and are withdrawn. Measured now: **0.93x PS2** (EE cycles per
  wall second against kEeClockHz=294,912,000). Never divide function entries by a
  clock speed — an entry is not a cycle, that mistake produced the 28x.
- Do not touch `docs/` or the harness's public window title. The captain runs the
  game by double-clicking a desktop icon and reads FPS off the title bar.
- Commit as `Or Golan <or024662@gmail.com>`. Do not push.
- Cap builds at -j4, nice -n 10, ionice -c3. A live Minecraft server shares this box.

## STUCK PROTOCOL

First time you hit the wall, report: `STUCK: / TRIED: / BLOCKED BY: / NEED:` — with
numbers attached. Do not spend three hours re-deriving what is listed above; it is
all measured, and the captain has already burned four wrong leads on this exact
question. Measurement first, fix second.