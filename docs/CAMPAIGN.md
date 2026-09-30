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
| **W7 — the guest spins in a byte-store loop to the GS window** | at `0x0100f800`: `sb $v0,0($t1)` / `addiu $t1,$t1,1` / `bne $t1,$t4`. **The guest was never stuck — the DETECTOR was.** Instrumentation showed the loop converging normally (`$s1` `0x01051A45`→`0x01051A4A` against `$s2=0x01051A4B`, i.e. six iterations from its bound) and the outer loop advancing `$s1` toward `$s2` across 193 bytes. The no-progress detector called any repeating PC pattern a hang after 24 repeats; GT4's normal work IS a converging loop over one small PC set, so it stopped a boot that was about to make more progress. **Fixed (G1.8f):** the detector now resets on progress the PC stream cannot show (new code entered, new hardware touched) and clears one guest call's internal loops (24 → 4096, W7 measured ~193 passes per call). **Measured: `functions_entered` 95 → 20,000,000.** Four earlier claims are RETRACTED: that `$t1` was pinned, that `0x24`/`$fp` was the end and never moved, that the range was empty by design, and that the function was entered at the loop body | ✅ fell (G1.8f) |
| **W8 — the guest spins in its idle loop: the scheduler never blocks** | after G1.8f the boot exhausts its entry budget: `functions_entered=20000000 halt=entry_budget_exhausted distinct_pcs=112` — but only **119 syscalls** and **469 MMIO accesses** in those 20M entries, and `dispatcher_transfers=15 serviced_invocations=15`. The guest is calling functions in a tight loop that does no work: it called `sce_SleepThread` 11× and then kept entering the same ~112 functions 166k×/sec. `sce_SleepThread` returns immediately instead of blocking, so the main thread never yields and no other thread runs. **This is the first wall that is a real guest-semantics bug, not a measurement artefact.** | 🟡 **MECHANISM FIXED, WALL NOT FALLEN** — three separate defects, all real, all measured. See the row below and `docs/HANDOFF.md` 2026-09-30 19:40 |
| **W8a — `sce_SleepThread` did not block** | `EeScheduler::sleepCurrent()` → `blockCurrent()` was correct: Waiting/Sleep, `m_currentThreadId=0`, throw. **The un-blocking was downstream, in two places.** (1) `EeScheduler::serviceInvocations()` re-established the main thread "so an invocation has somewhere to attach" and did `main->wait = {}` then `makeRunning(*main)` — `main->invocations.empty()` is true for every plain syscall, so the condition was always true and the thread that just asked to sleep was made Running again in the same call. (2) **`EeScheduler::bindMainContextForSyscall()` tested `m_executorThread == std::thread::id{}` to mean "not set up yet" — which is permanently true for any driver that never calls `run()`, and `tools/harness/vulcan4_harness.cpp` never does. So EVERY syscall re-ran `reset()`**: every thread, semaphore, event flag, alarm and queued event the guest had created was destroyed, and the main thread record was rebuilt as `Ready` before being parked again. That is why one boot shows `sce_CreateThread calls=1` and `sce_CreateSema calls=7` alongside `sce_SleepThread calls=11` — the guest's kernel objects did not survive its own sleeps | ✅ **fell (G1.8g)**. Four red-first thread-state tests in `ps2xTest/src/ps2_thread_block_tests.cpp`; the runtime now answers `canDispatchGuest()` so a driver never re-enters a parked frame, and `serviceInvocations()` returns `EeServiceResult` instead of a bool so a driver can tell "ran" from "blocked" |
| **W8b — `serviceInvocations()` slept on the host clock** | it called `processPendingEvents()`, which paces itself to the next VBlank host deadline via `m_eventCv.wait_until` — correct in `run()` (the dispatcher is what models time) and wrong in a service call, where a single call could burn `stepBudget × one VBlank period` of wall clock. | ✅ **fell (G1.8g)**. `processPendingEvents(bool mayWait)` / `processDueDeadlines(bool mayWait)`; `run()` passes true, a service call passes false |
| **W8c — the harness's own watchdog held the boot open for the whole budget** | `std::thread watchdog(...)` slept out the entire deadline and only then called `requestStop()`, and the harness then did `watchdog.join()` — so `join()` blocked for every remaining second. **Measured: a 20 s budget produced a 20.3 s process and a 120 s budget a 120.6 s process, for a guest phase that takes 1.3 s in both.** Every `elapsed_ms` in the boot report was the harness waiting on its own watchdog. The `perf record` reading that pointed at `libgallium` was 99 % of 6 samples — GL context creation, not a present stall. The GS lane also measured, with an `LD_PRELOAD` shim on `pthread_join`, that exactly one join takes 11.0 s in a 12 s run and all 28 others take 0.000 s | ✅ **fell (G1.8g)**. The watchdog now watches a `driverFinished` flag; the report prints `elapsed_ms`, `guest_phase_ms` and `harness_tail_ms` separately so this cannot be misread again. **Measured after: 120 s budget → 1.6 s process, `elapsed_ms=1143 harness_tail_ms=18`** |
| **W9 — the guest's own idle loop, and it has never touched the GS** | with the instrument honest, the whole guest phase is **1.13 s**: `functions_entered=44578`, `distinct_pcs=112`, 119 syscalls, 754 MMIO accesses, 112 distinct PCs. A timing probe showed entries 0→40000 in 179 ms and 42000→44578 in 7 ms, so nothing is slow — the guest simply stops making progress. It stops at guest `0x01000760`, decoded from SCUS_973.28: `move $t0,$zero` / `jal 0x010027F0` / `move $s0,$zero` / `beq $zero,$zero,0x01000760` — an unconditional `while (1) { sub_010027F0(); }` with no syscall, no MMIO and no load in the loop body. **78 % of all guest entries are at `0x0100f800`**, W7's byte-copy loop, reached through `case 0x100f800u` in `sub_0100F390`, and it is converging normally. **The boot log contains no write to `0x1200xxxx` at all** — the GS privileged window is never touched, so milestone 2 cannot be reached by any amount of GS work until the guest gets past its own idle loop | 🟡 **the wall.** Next: establish what `sub_010027F0` is waiting for. It is the guest's main loop, so the candidates are (a) a memory word the created worker thread was supposed to write and has not, (b) an EE timer/vsync interrupt our scheduler only delivers when the guest yields, and it never yields, or (c) a genuinely missing syscall. **Do not patch the loop.** A `while(1)` in a boot log is where the guest is standing, not why |

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

## REPLAN — 2026-09-30, after mining a finished PS2 decomp (`docs/DC-MINING.md`)

Six findings from the Dark Cloud decompilation changed what we *know* about the road. None of them
change the destination; three of them replace a guess with a fact.

**1. G2 is an SDK, not a driver. → the GS lane gets a defined surface.**
A finished PS2 game touches **raw MMIO exactly once** (EE Timer0, 4 hits in 151k lines). Everything
else goes through SDK calls. So "write a GS renderer" becomes **"implement the `sceGs*` calls GT4
actually makes"** — a finite, enumerable list instead of "the GS in general".
- **Next for the GS lane:** produce the **`sceGs*` call inventory** from the boot logs and the ELF,
  ranked by call frequency, and implement in that order.
- **`sceGsSetDBuff` FIRST.** Its defaults are load-bearing — the game inherits TEST/ZBUF state from it,
  and guessing them makes **every** draw wrong. (This is W8's neighbourhood: our depth test was
  deleting GT4's triangles.)

**2. G10 gets a new law: BUILD TWICE.**
MWCC matching compiles are **not reproducible** — the same command can emit different bytes, so
"a matching build may be matching by accident". Any ownership claim that says *byte-exact* must
compare **two clean builds**, not one against a memory of one.

**3. Byte-exactness is compared SECTION-WISE, not symbol-wise.**
Verified today: **GT4's `SCUS_973.28` is stripped** — no `.symtab`, no `.dynstr`, no `.strtab`
(section check: `.shstrtab` only). So the `@<n>` float-literal question cannot be answered from its
symbols, our own function names are *ours* (analysis-derived, not read off the disc), and matching
proof must compare **`.text` and data sections** — which is exactly how DCDecomp proves it.

**4. The no-BIOS backlog is FINITE: nine IRX modules.**
Not "197 SCE functions with no handler" (fuzzy) — **nine modules**, all recoverable **in full** from
`init_all()`. Turn it into a work-list and tick them off.

**5. Audio: the IOP heap RESETS on every init.**
Outstanding pointers go stale. The audio lane must assume nothing survives an init.

**6. G10's shipping mechanism is `mwccgap`.**
MWCC emits a unit as one contiguous `.text`, so a hole cannot be filled from outside: compile twice
and patch nops. **That is how partial ownership ships without breaking the image.**

### THE GUARD RAIL (this is the anti-bad-practice register the captain asked for)

`docs/DC-MINING.md` §5 is a **do-not-carry list** — 12 Dark Cloud specifics (its nine IRX module
names, `My_dma_start0`, `Vu_prog0`, the `name_counter` values, `262`) that must **never** become GT4
rules. §4.8 lists **nine things mining did NOT establish**, so silence is never read as a negative.

**And the honest ceiling:** a byte-exact build is **not** evidence the source is understood. That
matching project ships a wrong-shaped `CDebugFont` stand-in *with a TODO committed*, and **310
functions carry `@unknownret`**. Matching proves *shape*; it does not prove *meaning*. Our `VERIFY:`
gates must test **behaviour**, never just "it compiled identically".

### WHAT DOES NOT CHANGE

Stage 1's order (boot → frame → 3D → input → race → audio), one controller per lane, red-test-first,
no faking, no game data in the repo, `-j4`/`-j2` + nice, commits as the captain, no pushes.

### THE TOP THREE, IN ORDER

1. **Boot keeps moving** — dish 35 (`sce_SleepThread` must block). The road to a frame runs through it.
2. **GS: the `sceGs*` inventory, then `sceGsSetDBuff` defaults.** The drawing path is now defined.
3. **Write the stripped-ELF consequence into the G10 gate** (section-wise compare, build twice) before
   any ownership work starts, so the first claim we make is a claim we can defend.

---

## WHY THIS EXISTS (the captain's words, 2026-09-30)

> *"i need u to make me a longer term goal. untill we get it working. we cant just make a goal and make
> it die like 10 times before we are able to even get the game to show a single frame."*

He is right about the failure mode. A goal that lives in a chat session dies when the context fills; the
work stops, and it looks like progress was never real. **So the goal lives here, in a file, and a driver
keeps feeding it until the frame arrives.** The measure of this campaign is one thing only: **pixels the
game itself produced.**
