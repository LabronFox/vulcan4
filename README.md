# VULCAN 4

**A native recompilation of Gran Turismo 4.**

Not an emulator. GT4's own PlayStation 2 machine code is translated **once**, ahead of time,
into C++ — then compiled for the machine you are actually sitting at. The game's own logic,
physics, race rules, AI, menus and event structure run as native code, because it *is* the
game, just natively compiled.

## Who this is for — the captain's words, 2026-09-29

> *"i wanna be early. i want to make sure all the fellow gt4 lovers just like me are gonna
> enjoy this game."*

That is the point of the project, and it is the test every dish is judged against. Being early
is not a vanity goal — **the two walls in this file are the reason nobody has done it yet**, and
they are also the reason a working native GT4 would matter to people who have played this game
for twenty years. When there is something to show, it gets shown to them.

Sibling project: [`vulcan6`](https://github.com/LabronFox/Vulcan-6) (Gran Turismo 6 → modern
hardware, now **private** and on hold) while PS3-side recompilation tooling matures; Vulcan 4 is
the project that can move today, because the PS2 recompilation path is real and public.

## Why 4 first

- A recomp does **not** reverse-engineer file formats. The executable already works. The work
  is translation + a hardware runtime, not archaeology.
- GT4 is the most-requested "native port" in the Gran Turismo community, and it is the game
  this project's captain actually plays.
- The PS2 toolchain is public: [`PS2Recomp`](https://github.com/ran-j/PS2Recomp) (GPL-3.0),
  the same family as N64Recomp / PS1Recomp. Community practice is to **fork it per game**.

## The two walls (stated up front, honestly)

1. **VU1.** PS2 games run their vertex/lighting/transform math as *microcode* uploaded to a
   vector unit. Recompilers treat VU1 as the hard part, and PS2Recomp's support is explicitly
   limited. Nothing here gets faked: an unhandled VU1 path reports itself.
2. **The GS (Graphics Synthesizer).** Fixed-function hardware. It cannot be recompiled — a
   renderer must be supplied. That renderer is a **dependency**, not a detail.

Between the two walls sits every pixel this project will ever draw. The ladder in
[`docs/GOALS.md`](docs/GOALS.md) is ordered accordingly.

## Status

**M0 — foundations.** The toolchain is being brought up on Linux and GT4's own executable is
on the slab. Nothing plays yet, and this file will say so until something does.

**Where the work physically lives — read this before building anything:**
[`docs/WHERE-THINGS-LIVE.md`](docs/WHERE-THINGS-LIVE.md). Code lives on the root filesystem;
**every build tree, generated file and scratch dump lives on `/mnt/ssd`**. A full root disk on
this box once masqueraded as a code bug for a whole dish.

## The rules

- **No game data in this repository, ever.** No ISO, no extracted assets, no disc images.
  The runtime requires **your own dump** of the retail disc, legally obtained.
- **No faking.** Unimplemented behaviour emits an explicit `VULCAN 4 LIMITATION: <what> — <why>`.
  A black screen with an honest limitation beats a plausible-looking lie.
- **Refused, not guessed at.** An undecoded structure is reported, not approximated.
- Every piece of work declares a **goal id** from `docs/GOALS.md` — no goal, no dish.

## Legal

GPL-3.0 (the recompiler this builds on is GPL-3.0 and the licence is viral). No game code,
assets or data are redistributed. Research and preservation intent. Not affiliated with Sony
Interactive Entertainment or Polyphony Digital.

## Machines

| Role | Machine |
|---|---|
| Build + analysis + the disc | **Cortex** (Linux, this box) |
| Daily dev / Rust + fast compiles | **Tofu** (Mac mini M4) |
| Target devices | desktop PC · **AYN Odin 2** (Android) |
