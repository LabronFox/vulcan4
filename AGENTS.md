# VULCAN 4 — the project brief for the cook

*You are working on this project. Read this every turn: it is the WHY behind the dish you were
handed. A dish tells you what to build; this tells you what you are building it **for**.*

## THE GOAL — the captain's own words

> **"lets work on Vulcan 4! a recomp of gt4"**
>
> **"i wanna be early. i want to make sure all the fellow gt4 lovers just like me are gonna enjoy
> this game."**

**VULCAN 4 is a native static recompilation of Gran Turismo 4.** Not an emulator: GT4's own PS2
machine code is translated **once**, ahead of time, into C++, and compiled for the machine the user
is actually sitting at. The game's own logic, physics, AI, menus and event structure run natively —
**because it is the game**, natively compiled.

## 🎯 STAGE 1 — what you are actually building toward, right now

> **"Get Gran Turismo 4 running on PC — natively, no emulator, from the user's own disc."**

**x86-64 first.** Not Android, not mods, not another platform. Stage 1 is done when all six of these
are true, with **no BIOS anywhere in the path**:

1. GT4 boots **past its own startup** (today it enters 1–3 of its 707 functions)
2. **A picture** — GT4's own screen, drawn by our GS layer
3. **3D** — a car and a track through the VU1 path
4. **Input** — menus navigable, a gamepad works
5. **A race you can drive** — the whole loop, physics and all
6. **Audio** from the disc's own data

Then, in order: **Stage 2 Android (Odin 2)** → **Stage 3 mods (add a car · custom music)** →
**Stage 4 the community gets it.**

**Every dish serves Stage 1, or it is explicit Stage 2/3 preparation. If a dish serves none of them,
it should not have been dispatched — and if you think that is what happened, say so.**

## What done looks like (the ladder — where your dish sits)

| | Goal | State |
|---|---|---|
| **G0** | Ground truth: toolchain builds, disc mapped, function readable, recompiler completes, clean room reproduces | ✅ done |
| **G1** | **First boot**: GT4's own code EXECUTES, no BIOS, blockers named one by one | ⏳ the current wall |
| **G2** | **The GS** (the GPU): a real renderer — it cannot be recompiled, it must be supplied | ⏳ in progress |
| **G3** | **The VU1** (vector unit): microcode and 3D | ⬜ plan exists |
| **G4** | **Playable**: menus, a race you can drive, audio from the disc, saves | ⬜ |
| **G5** | The captain's build (**Spec II**), **Android/Odin 2**, other regions | ⬜ |
| **G6** | **Mods**: new cars (never possible before — swaps only), custom music | ⬜ registered |
| **G7** | **Distribution**: code only, assets from the user's own disc | ✅ in force |

**Platform order (captain's call):** x86-64 PC first → ARM64 (Odin 2) second → PSP only with a
direction check.

## ⚠️ BEFORE YOUR FIRST TOOL CALL — THE PS2 RECOMP MANUAL IS ON THIS BOX

A 52 MB PS2/recompiler corpus is installed here. **Read it before you improvise; do not guess PS2
behaviour from intuition.** Measured 2026-10-07: the previous 691-turn session referenced it **zero**
times and spent hours probing things the corpus already answers.

- Entry point: `~/.config/opencode/skills/ps2-recomp-Agent-SKILL/SKILL.md` — read §1 DECISION ROUTER,
  then load ONLY the 1–2 files it points at for your situation. Do not bulk-read.
- Runtime debugging / A-B against real hardware → `resources/12-pcsx2-mcp-playbook.md`
- Any crash or stuck bug → `resources/10-agent-guardrails.md` §3 (fix taxonomy, root-cause protocol)
- "I don't know why" → `resources/13-decisional-brain.md`
- MIPS / PS2 hardware truth → `resources/02-mips-r5900-isa.md`, `resources/09-ps2tek.md`,
  `resources/db-ps2-index.md` (master router)
- **GT4 boot / menus / "the game does nothing" / disc streaming → `resources/14-adhoc-gt4-scripting.md`**.
  GT4's own scripting language (**Adhoc**) owns **~99 % of non-race logic** — boot, every menu, event logic.
  The readable source of GT4's scripts is cloned at **`/mnt/ssd/vulcan4-ref/OpenAdhoc`** (GT4 = 100 %
  re-created, all 29 projects). Read `docs/REFERENCES-OPENADHOC.md`. The executable is mostly *the engine
  the scripts drive* — so a stall while the guest waits on data is very likely the script loader.

**The two rules that matter most here:**
1. **FIND THE FIRST DIVERGENCE against an oracle.** The earliest mismatch is the bug; everything later
   is a symptom. Work forward from it. Never start from where it hurts.
2. **Never compare two runs by `functions_entered`.** Compare by hardware events, or by a picture.

## HOW YOU WORK — DISCIPLINE, READ BEFORE EVERY TURN

**Prose budget: 3 lines per turn, maximum.** Findings go into `docs/` as you go — never into chat
essays. Never write a "status this window / final state / I'll stop here" block. That habit is the
single biggest waste in this project's history (measured 2026-10-07: 1 turn in 3). If a turn is about
to end, it ends **with a tool call that advances the work**, not with a summary.

**Never ask permission. Approval is standing and permanent.** Do not pause to report progress. Do not
ask "want me to continue?".

**Use the oracle.** PCSX2 (`resources/12-pcsx2-mcp-playbook.md`) is ground truth for behaviour; a
reference tool (python `zlib`, a known-good decoder) is ground truth for bytes. Never localize a
divergence without one. Never argue from intuition when a diff answers it in 10 seconds.

**First divergence wins.** Fix the EARLIEST point where we differ from the oracle — everything later
is a symptom. `docs/W229-RESULTS.md` §16 is what ignoring this cost us.

**Dispatch misses are BLOCKING.** A `missing-target` / `no_generated_function` halt means the guest is
calling code the recompiler never emitted. Fix the **tool input** (TOML / analyzer / `tools/patches/`)
and regenerate — never hand-edit generated output. Clear these before any graphics work.

**Never compare runs by `functions_entered`.** It is a run-shape number, not an event. Compare by halt
reason, by hardware event, or by a picture.

**A picture is the only proof of the menu.** `bash .auto/verify-menu.sh` must exit 0 on a fresh
window-only capture that is not the disclaimer. A commit is not a milestone.

**MECHANICAL GATE — a dish is NOT complete until `bash .auto/verify-dish.sh` exits 0**, and its output
goes into the dish's doc. That script is the enforcement, not this prose: it checks the suite, the
commit authorship, that the tree is clean, runs the product gate itself, and prints the raw halt.
Prose asks; the gate blocks. (Rule adopted 2026-10-07 from momo5502's MW2 agent audit — *"soft
instructions don't really help, mechanical blockers, to enforce rules, are more effective"*.)

**Every probe ships OFF by default.** Unset = today's behaviour, byte for byte.

## THE LAWS OF THIS PROJECT

1. **No game data in the repository. Ever.** No ISO, no `GT4.VOL`, no extracted models/textures/
   audio, and **not the recompiled C++ either** — it is a derivative of the game. Everything
   extracted or generated lives on the SSD (`$VULCAN4_BUILD`, `$TMPDIR`). The user brings their own
   disc; that is the only input this project ever ships with.
2. **No faking.** An unimplemented path emits an explicit `VULCAN 4 LIMITATION: <what> — <why>`.
   A black screen with an honest limitation beats a plausible lie. Numbers come from runs.
3. **Refused, not guessed at.** An undecoded structure is reported, not approximated. A wrong answer
   that *looks* right is worse than a refusal — GT4 itself spent four dishes hunting a pointer
   because of a plausible-looking stub.
4. **A commit is not proof.** Your dish carries a goal gate; the driver runs it against the product.
   If the gate fails, say so plainly and commit what you learned — that is a good outcome, not a
   shameful one. Half the best findings in this project came out of a failed gate.
5. **Say when you are stuck, with the shape:** `STUCK: / TRIED: / BLOCKED BY: / NEED:` — the first
   time you hit a wall, not the fifth. A named blocker is worth more than an hour of poking.
6. **Never let the disk fill.** Root `/` is small; a full disk once masqueraded as a code bug and
   cost a whole dish. Build on the SSD, temp on the SSD, `df -h` before big builds.
7. **Write it down as you go.** Long sessions die mid-turn; only what is on disk survives. Every
   dish appends to its doc.
8. **Patch upstream, don't fork blindly.** Changes to `tools/PS2Recomp` go in `tools/patches/` with
   the reason, so a future `git pull` stays sane.
   **AND NOTHING MAY LIVE ONLY IN THE WORKING TREE** (added 2026-10-08 after an audit found **1,065
   uncommitted lines** — `ps2_runtime.cpp +921`, `ps2_runtime_macros.h +90`,
   `instruction_translator.cpp +32`, `control_flow_emitter.cpp +22` — that NO patch carried: the w253
   patch held only 567 of the 921 runtime lines, the w273 patch only 18 lines of `EeScheduler.cpp`).
   Work that exists only in a working tree is work that a `git checkout`, a re-clone or a crash
   deletes. So **before any dish may be called done**, the scribe runs the capture check and pastes
   its RAW output into the dish doc:
   ```bash
   cd tools/PS2Recomp && git diff --numstat      # must be EMPTY, or every line must be in a patch
   ls -la ../patches/ | tail -3                  # the patch carrying it, timestamped today
   ```
   Every change gets a patch under `tools/patches/`, verified **per file** against `--numstat` (a patch
   carrying fewer added lines than the live diff has not captured it), committed in the outer repo.
   **A dish whose gate exits 0 but whose source sits uncaptured is a FAILED dish.** The gate is the
   deliverable; the record is what lets the next seat stand on your work instead of rediscovering it.
9. **Never open an image in context.** Report the path; someone outside looks at it. (Reading images
   into a session breaks it permanently — there is a 20-image limit upstream.)
10. **Commits author as the captain:** `Or Golan <or024662@gmail.com>`. Do not push.
11. **Do not starve the house.** This box also runs a live Minecraft server that real people play on.
    Cap builds at **`-j4`** and wrap heavy work in **`nice -n 10` / `ionice -c3`** — never `-j12`.
    Verified 2026-09-30: a `-j12` compile produced the world's only server stall (`Can't keep up!
    4824 ms behind`) at the exact minute the compile ran, and the player felt it. `nproc` is 6, not 12:
    the game gets a core or two, the build gets the rest.

12. **Everything we add is OFF by default.** The game as it shipped is the default state. New cars, custom
    music, chimes, camera and speed-feel changes, quality-of-life fixes — all **opt-in**, and `0`/`OFF` must
    mean the untouched original behaviour, not "a subtle amount". The captain's reason, verbatim (2026-10-01):
    *"everything i add and change is off by default. cant know if they want an authentic experience."* A
    feature that ships enabled has changed somebody's game without being asked, and the authentic one is the
    whole reason this project exists. This applies to *every* entry in `G6`.

## HOW YOUR WORK IS JUDGED

- The captain judges the **product**: a picture, a number, a sound — not a commit log.
- The project's own `docs/STATUS.md` is the public summary; if your dish changes what it claims,
  update it.
- `docs/GOALS.md` is the ledger. Your dish's `GOAL:` id refers to it. Read the entry if you need the
  bigger picture — and read `docs/LIMITATIONS.md` before claiming anything is complete.

## THE SPIRIT OF IT

The people who want this have played GT4 for twenty years and never been able to add a car to it —
only swap one out. You are helping build the first thing that changes that. **Cook it like you mean
it, and never serve something you would not eat yourself.**
