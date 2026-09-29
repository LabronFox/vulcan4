# VULCAN 4 — THE GOAL LEDGER

> **Rule zero: no goal, no dish.** Every dish, commit and report carries its goal id.
> Every dish may declare a `VERIFY: <shell cmd>` gate; **a commit is not proof.**

---

## ⭐ THE GOAL

**Play Gran Turismo 4 — your disc, your cars, your tracks, your rules — running natively on a
modern machine, built out of GT4's own code.**

Eventually the captain's Odin 2. Never an emulator.

---

## What "done" means — countable

1. It loads a **retail GT4 disc**, no conversion step needed by the user
2. It reaches **the real menu**, drawn by a real GS layer
3. A car and a track render in 3D (VU1 path alive)
4. A **race can be driven** with a gamepad
5. Audio plays from the disc's own data
6. Save/progression works
7. Runs on desktop **and** Android (Odin 2)
8. Every remaining gap is listed in `LIMITATIONS.md` with an honest verdict

**Progress: 0 / 11 goals complete.**

---

## G0 — GROUND TRUTH ✅/🟡 *(active)*

### 🟡 G0.1 — The toolchain builds on Linux
- **DONE WHEN:** `ps2xRecomp` and `ps2xRuntime` configure and build on Linux (measured today:
  upstream `cmake -S . -B build` **fails** fetching `toml11` — it is an MSVC-first project),
  and a scratch ELF can be pushed through the recompiler to produce C++.
- **PROOF:** a build log + generated C++ on disk.
- **STATUS:** 🟡 after the park of Vulcan 6 this is the first dish fired.

### ⬜ G0.2 — GT4's executable is on the slab
- **DONE WHEN:** we can state, with numbers, what the disc contains: the ISO's volume label,
  the game's `SCUS_***.**` executable, its sections, its entry point, and a function count
  from the analyzer — written into `docs/DISC-MAP.md`.
- **PROOF:** the doc's numbers + the commands that produced them.

### ⬜ G0.3 — Read one function as text
- **DONE WHEN:** a single GT4 function is disassembled *and* appears as generated C++ next to
  it, so the translation can be read side by side rather than trusted.

---

## G1 — FIRST BOOT

### ⬜ G1.1 — Recompiled code executes
- **DONE WHEN:** the runtime loads the recompiled image and executes real GT4 code, with every
  unimplemented call **named** (not silently stubbed): a log showing N functions entered and
  which hardware path stopped it.
- **PROOF:** the log + the halt reason.

### ⬜ G1.2 — The first observable milestone
- **DONE WHEN:** GT4 gets further than the loader — whatever the game does before it needs a
  GPU is observed (a string, a loaded resource, a state), with the evidence attached.

---

## G2 — A PICTURE (the GS)

### ⬜ G2.1 — A GS layer exists and draws
- **DONE WHEN:** an actual frame comes out — even the boot logo — as a PNG.
- **NOTE:** this is the first of the two walls. Nothing downstream is possible without it.

---

## G3 — 3D (the VU1)

### ⬜ G3.1 — VU1 microcode path
- **DONE WHEN:** GT4's own vertex microcode is handled and a 3D scene renders (a car, a track).

### ⬜ G3.2 — A real car on a real track, natively

---

## G4 — PLAYABLE

### ⬜ G4.1 — Menus navigable (gamepad)
### ⬜ G4.2 — A race you can drive
### ⬜ G4.3 — Audio from the disc's own data
### ⬜ G4.4 — Save / progression

---

## G5 — THE CAPTAIN'S BUILD + POCKET

### ⬜ G5.1 — **Spec II** support
- The captain plays GT4 **Spec II** (community mod) on the Odin 2. A recomp targets one specific
  binary, so the retail US release comes first and Spec II is a **second target**, not a
  distraction.

### ⬜ G5.2 — Android / Odin 2
- **DONE WHEN:** it runs on the Odin 2 at a playable frame rate. Snapdragon only — the
  captain's hardware rule.

---

## The discipline

1. Every dish opens with `GOAL: <id>`; the driver prints it in the banner.
2. Every commit ends with `[GOAL <id>]`.
3. Every report opens with the captain's picture or number, never with a commit list.
4. **"It's implemented" is not done.** Proof is him seeing it or counting it.
5. **No game data in the repo. No faking. Refused, not guessed at.**
6. Nothing new is dispatched before the captain has seen the previous result.

*Declared 2026-09-29, the day the captain said: "lets work on Vulcan 4! a recomp of gt4."*
