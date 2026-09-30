# VULCAN 4 — THE CAMPAIGN

> **This file is the long-term goal. It does not live in a session, so it cannot die with one.**
> Every dish, every session, every handoff points here. If a session runs out of context, the campaign
> survives — the next session reads this file and continues.

**Opened:** 2026-09-30 · **Status:** ACTIVE · **Controller:** `vulcan4-driver.service` (24/7)

---

## THE FINISH LINE — what this campaign is *for*

> **GT4 draws a frame of its own — on a PC, from the user's own disc, with no emulator and no BIOS.**

That is the first thing worth showing a human, and it is the campaign's stop condition: when a frame
produced by GT4's own code reaches the screen, **the loop stops and pings the captain.**

The full Stage 1 ladder (nobody plans past this yet):

| # | Milestone | Machine-readable marker | Gate |
|---|---|---|---|
| **1** | Guest boots **past its own startup** (its CRT init is done; real game code runs) | `functions_entered` in the boot report | `functions_entered >= 1000` |
| **2** | 🏁 **A FRAME from GT4's own code** — guest traffic reaches the GS | `VULCAN4 FRAME source=guest` in the log | **CAMPAIGN DONE** |
| 3 | 3D — a car and a track through the VU1 path | `VULCAN4 FRAME has3d=true` | later |
| 4 | Input — menus navigable, a pad works | `VULCAN4 INPUT source=player` | later |
| 5 | A race you can drive | `VULCAN4 RACE drivable=true` | later |
| 6 | Audio from the disc's own data | `VULCAN4 AUDIO source=disc` | later |

**Dishes must emit their marker** when they earn it. A milestone nobody can test is a milestone nobody
has reached. After milestone 2 the captain decides what the campaign becomes — Android, mods, or
polish — and this file is rewritten.

---

## THE WALL LEDGER — every wall, how it fell, and what it taught

*A wall is named, not ground against. Each entry: what it was, the evidence, how it fell.*

| Wall | Evidence | Outcome |
|---|---|---|
| **W1 — toolchain would not configure on Linux** | `Build step for toml11 failed: 2` — upstream is MSVC-first | ✅ fell (G0.1). It was a **full disk**, not MSVC — the lesson that became law 6 |
| **W2 — image loaded 16 MB off** | `0x01000000–0x01FFFFFF` is the PS2 **user segment**, not identity-mapped; search window went 2 → 60,951 non-zero words | ✅ fell (G1.7) |
| **W3 — recompiler deadlock** | all 12 threads on futex; `missing index 719`; exit 143 | ✅ fell (G1.5) |
| **W4 — the syscall overrides never landed** | 197 SCE functions with no runtime handler; GT4's own `0x83`/`0x5A` handlers unreachable | ✅ fell (G1.8b) — **+22 functions entered** |
| **W5 — the driver never ran what the guest queued** | `EeDispatcherTransfer` caught by the harness, which re-entered the guest **without letting the scheduler service the queued invocation**; then `hasInvocation()` **latched**, so every later `0x83` fell through to our builtin | ✅ fell (G1.8b, commit `cde9992`). **3 → 25 functions entered** |
| **W6 — the guest restarts from its own entry point** | `pc=0x01000008` `distinct_pcs=1`, 24 repeats; `sce_SetupHeap`/`SetupThread`/`CreateSema` each called **25×** = the guest re-runs its own CRT init; `distinct_mmio_addresses=0` → **not** a hardware wait | ✅ fell (G1.8c). **25 → 95 functions, `distinct_pcs` 1 → 50, and the guest now touches 228 hardware registers** — its CRT init runs **once**. `docs/G1.8c-RED.md` |
| **W7 — the guest spins in a byte-store loop to the GS window** | at `0x0100f800`: `sb $v0,0($t1)` / `addiu $t1,$t1,1` / `bne $t1,$t4` — a memset/copy tail that never terminates. The guest is already writing GS registers (`[gs:gif] nloop=7`, `PRMODE=0x8005`, `PRIM=3`) and 195 distinct `0x7000xxxx` addresses | 🔴 **OPEN — current wall, and the first one on the path to milestone 2.** Dish `33-g18d-gs-store-loop` |

**Reading the ledger is the handoff.** Whoever picks this up starts by reading the last row and the dish
that owns it.

---

## HOW THE LOOP WORKS — why this campaign cannot dry up

```
driver wakes
  ├─ a dish in .auto/queue/?           → run it, judge it by its GOAL gate, commit, mark done
  ├─ queue EMPTY but campaign NOT done → RE-ARM: drop .auto/campaign-loop.template.txt into the queue
  │                                      (that meta-dish reads THIS file, writes the next 1–3 dishes
  │                                       from the wall ledger, then works the first one)
  └─ campaign DONE                     → park, stop, and PING the captain with the frame
```

**A session dying is not a campaign dying.** Each dish runs in its own run; when one hits the context
limit, the driver's gate says "goal not met", the dish is retried, then parked, and the loop moves on.
The campaign only ends at the finish line or when a human stops it.

**Three consecutive dish failures still park the driver loudly** — that is the "the chef is stuck on
something repeating" brake, and it stays. Parking always means *a human should look*, never *try harder*.

---

## LAWS (carried by every dish — the full list is in AGENTS.md)

1. **No game data in the repo. Ever.** The user brings their own disc; that is the only input.
2. **No faking.** An unimplemented path emits `VULCAN 4 LIMITATION: <what> — <why>`.
3. **A commit is not proof** — the dish's `VERIFY:` gate is, measured against the product.
4. **Red test first**, whenever a wall is a behaviour: reproduce it, capture the failure in
   `docs/G<n>-RED.md`, then fix it. (This is what made G1.8b land instead of guess.)
5. **Name the wall, do not grind it.** `STUCK: / TRIED: / BLOCKED BY: / NEED:` — then stop and report.
6. **-j4 and nice only.** A live Minecraft server shares this box (law 11 in AGENTS.md).
7. **Never open an image in context** — report the path; someone outside looks.
8. **Commits author as the captain** (`Or Golan <or024662@gmail.com>`), and only a human pushes.

## THE HANDOFF CONTRACT — how a dead session hands over

Every dish that stops for any reason appends to `docs/HANDOFF.md`:

```
## <timestamp> · <dish name> · <verdict: passed | gate-failed | parked | stuck>
WALL:    <which wall, or "none">
DID:     <what actually changed, one or two lines>
MEASURED:<the numbers — functions_entered, halt, test counts, whatever the dish's gate reads>
NEXT:    <the next wall, and the next dish if you can name it>
```

**Rule: a session that dies without a handoff entry is a session that wasted a turn.** The next dish
reads `docs/HANDOFF.md` first, then `docs/CAMPAIGN.md`, then works.

---

## WHY THIS EXISTS (the captain's words, 2026-09-30)

> *"i need u to make me a longer term goal. untill we get it working. we cant just make a goal and make
> it die like 10 times before we are able to even get the game to show a single frame."*

He is right about the failure mode. A goal that lives in a chat session dies when the context fills; the
work stops, and it looks like progress was never real. **So the goal lives here, in a file, and a driver
keeps feeding it until the frame arrives.** The measure of this campaign is one thing only: **pixels the
game itself produced.**
