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
