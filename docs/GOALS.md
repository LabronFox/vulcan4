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

### ✅ G0.1 — The toolchain builds on Linux
- **DONE WHEN:** `ps2xRecomp` and `ps2xRuntime` configure and build on Linux, and a scratch ELF
  can be pushed through the recompiler to produce C++.
- **PROOF:** `docs/TOOLCHAIN.md` + `ps2_recomp` / `ps2_analyzer` / `ps2EntryRunner` running.
- **RESULT (2026-09-29):** ✅ `BUILD_EXIT=0` for all three. Two upstream Linux defects fixed with
  a 24-line patch (`tools/patches/ps2recomp-linux.patch`): toml11's `FetchContent` missing
  `GIT_SHALLOW`, and the runtime's unguarded SSE4.1 intrinsics. Recorded in `docs/TOOLCHAIN.md`.

### ✅ G0.2 — GT4's executable is on the slab
- **DONE WHEN:** the ISO's identity, the executable's SHA-256, sections, entry point and a
  function count from the analyzer, written into `docs/DISC-MAP.md`.
- **PROOF:** `docs/DISC-MAP.md` + `/mnt/ssd/gt4/work/gt4.toml`.
- **RESULT (2026-09-29):** ✅ retail US v2.00, SHA-256 `f8f10823…8019fa`. `SCUS_973.28` is
  273,020 B, **little-endian** R5900, entry `0x01000008`, `.text` 187,408 B = 46,852 insns,
  **707 functions**, 336 SCE symbols. **It is not a boot stub.** The bulk of the disc is not
  code: `GT4.VOL` (2.29 GiB) is an indexed container with an opaque payload, `CORE.GT4` (2 MB)
  is 7.96 bits/byte with no ELF inside. See §5.6 of that doc for the falsifiable answer.

### ✅ G0.3 — Read one function as text
- **DONE WHEN:** a single GT4 function is disassembled *and* appears as generated C++ next to it.
- **PROOF:** `docs/FUNCTION-ANATOMY.md` + 8.9 MB of generated C++ on the SSD.
- **RESULT (2026-09-29):** ✅ `sub_01000558` @ `0x01000558`, 328 B / 82 instructions, called from
  the entry block at `0x01000210`. Six spot-checks — sign extension, delay slots, branch-target
  arithmetic, indirect calls, the spin-trap loop — all verified against raw bytes. **The
  translation is faithful.** But the run **failed**: exit 1, 2 of 721 function bodies lost.

### ✅ G0.4 — The recompiler's output is complete and linkable
- **DONE WHEN:** a full `ps2_recomp` run over `SCUS_973.28` exits 0, writes all 721 function
  bodies, produces the artefacts it used to die before writing, and the generated `.cpp` compiles.
- **RESULT (2026-09-29):** ✅ `RECOMP_EXIT=0`, `errors: 0`, **721/721 bodies**, **0 declared-but-
  undefined**, all four artefacts written (`functions.cpp` 8,899,740 B, `functions.h`, `stubs.h`,
  `register_functions.cpp` 804,210 B). Compiles to a 10,652,680 B object; all 721 functions are
  global symbols in it; the only undefined symbols are `PS2Runtime::`/`ps2_stubs::` — **zero GT4
  functions** — which is G1.1's work, not a translation gap.
- **TWO defects, both in the combined-output writer**, fixed in
  `tools/patches/ps2recomp-linux-outputfix.patch` (1 file, +43/-3):
  1. **Race on the staging queue.** The termination check tested `completedCode.empty()` but never
     `readyCode.empty()`, so it declared work lost while finished bodies sat uncollected in
     `readyCode` — aborting the run and dropping the tail every time. Now re-drains before
     concluding, and **names the missing function** instead of a bare index.
  2. **Deadlock in the throttle.** The combined path throttled on
     `outstandingWork + completedCode.size()`, so out-of-order results filling the buffer behind a
     gap stopped it scheduling the very index that would fill that gap — 12 threads in `futex_wait`
     at 0% CPU, stuck at 320/721. Now throttles on `outstandingWork` alone, matching the
     per-file writer, which was already correct.
- **NOTE:** third time this toolchain reported a clean number while something was wrong. The
  third is the most serious — a *silent* truncation would have been worse than G0.3's loud one.
  Watch for more.


---

## G1 — FIRST BOOT

### ⬜ G1.0 — **NO BIOS REQUIRED** *(the captain's bucket-list item, 2026-09-29)*
- **Captain's words:** *"one thing to add to the bucket list. making it run without a bios."*
- **DONE WHEN:** Vulcan 4 boots and runs with **no BIOS file anywhere on the machine** — every
  call GT4 makes into the console's own OS is served by our runtime and **named in a log**,
  never silently stubbed. A `BIOS` path search that finds nothing is a *pass*, not an error.
- **WHY IT'S THE NATURAL SHAPE:** an emulator has to *be* the console, so it needs the real
  BIOS dump — the PS2's OS, legally awkward and a setup step for every user. A recomp is
  different: the game's code is native, and the handful of things it asks of the OS are
  **syscalls**. Our runtime answers them itself (high-level emulation). PS2Recomp's runtime is
  already built around exactly that — a syscall dispatcher and named stubs.
- **THE HONEST WORK:** the dispatcher existing is not the same as *covering what GT4 calls*.
  This goal is done when GT4's own call list is satisfied by our handlers, and the ones we have
  not implemented announce themselves. Memory card services, DVD access and the IOP modules
  (`IRX/` on the disc) are the parts most likely to need real work.
- **PAYOFF:** nothing for the user to supply but their own disc, no BIOS distribution question,
  a deterministic boot, and one less thing that can differ between machines.
- **STATUS:** ⬜ waiting on G1.1 — a first boot with no BIOS is the *first* boot worth having.
- **⚠️ CAPTAIN'S GUIDANCE (2026-09-29):** *"if the bios thing is an issue. i dont mind having it but
  eventually i want to not need it."*
  → **A BIOS-derived stopgap is permitted if it ever unblocks a boot. The goal itself does not
  move: v1.0 must run with no BIOS.**
  → **Mechanism, stated plainly, because "have a BIOS" is not a checkbox for us:** the PS2 BIOS is
  itself R5900 code — it would have to be *recompiled or emulated* to be used, which is more work
  and legal noise than implementing the handful of calls GT4 actually makes. So the natural path is
  and stays **HLE**: implement the calls, name the ones we have not, and let the list drive.
  → G1.1's boot report produces exactly that list. Nobody has to decide anything today.

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

### 🟡 G5.1 — **Spec II** support *(parked for later — the captain's call, 2026-09-29:
"we will figure it out later. first of all lets keep going")*
- The captain plays GT4 **Spec II** (community mod) on the Odin 2, and wants it.
- **🔴 FINDING (researched, from the mod author's own pages): Spec II is NOT based on the retail
  US release.** *"Spec II is based on the NTSC version of **Gran Turismo 4 Online Public Beta**
  and is distributed as an xDelta patch **requiring this version**."* It reports as serial
  **`SCUS-97436`** / CRC **`4CE521F2`**, where vanilla US retail is **`SCUS-97328` / `77E61C8A`** —
  different discs. The patch is downloadable on its own, but it needs that base image
  (MD5 `3306538778dda2ded87ceaf52c944a98`). Its FAQ also states it changes *"the game's disc image
  **and executable**"*, and its ELF has 480p + GT3 chase cam + trigger sensitivity + widescreen
  baked in — so **Spec II patches the executable**, it is not a data-only mod.
- **CONSEQUENCE (not blocking anything):** recompiling retail `SCUS_973.28` produces *vanilla*
  GT4, not the captain's build. Spec II support = recompiling the **Online Public Beta** build +
  its patch — same machinery, different target binary. **That same disc also carries the lost
  Online mode** (G5.3). Both of the captain's wishes live on one disc, and we will come back to it.
- **STATUS:** 🟡 later. Vanilla first. Nothing here blocks G0/G1.

### ⬜ G5.2 — Android / Odin 2
- **DONE WHEN:** it runs on the Odin 2 at a playable frame rate. Snapdragon only — the
  captain's hardware rule.

---

### ⬜ G5.3 — Regional + special builds
- **The captain's question (2026-09-29):** *"u think we should get all the different country
  roms or it dont matter?"*
- **Answer, measured:** a recompilation is **per build** — each region has its own code addresses
  and its own differences, so region support means repeating the process against that build, not
  "one recompile runs all". And the regions are **not** the same game in the details:
  - the NA/PAL builds carry **~10 cars the JP build does not**, and JP has one the others lack
  - **prize cars differ** (e.g. an endurance prize is the Sauber in JP, the Auto Union elsewhere)
  - the S-licence final test uses **a different car** per region
  - the **AI is more aggressive on its tyres in PAL than in NA** — a real gameplay-code difference
  - driving missions carry **different handicaps**, and NA has a well-known **100%-completion
    glitch** if DM1 is not done first
  - **PAL is a 50 Hz conversion** where NTSC is 60 Hz — timing, and therefore physics and licence
    targets, genuinely differ
  These differences are exactly what a recomp lets us **study**: diffing two regional builds is
  an X-ray of the game's own logic, which is how decompilation communities locate interesting code.
- **ALSO WORTH KNOWING — `Gran Turismo 4 Online`.** A separate build (US public beta `SCUS-97436`,
  plus a JP *Online Test Version*) shipped to ~4,700 Japanese and 300 Korean test players, with an
  **Online mode the retail game never shipped**: Online Home, Quick Race, Tuned Car Race, Private
  Race, Time Attack. Services ran 2006-06-01 → 2006-09-01 and died. If the lost mode is ever to be
  studied or restored, **that build is the one to read** — and it is the kind of thing this project
  exists for.
- **PLAN:** the **US v2.00 build stays the only target until something boots and draws.** Additional
  builds get collected **when there is a reason to diff them**, not now. Storage is cheap (~5 GB
  each), attention is not.
- **STATUS:** ⬜ none collected beyond US v2.00 (already on `/mnt/ssd/gt4`).

## The discipline

1. Every dish opens with `GOAL: <id>`; the driver prints it in the banner.
2. Every commit ends with `[GOAL <id>]`.
3. Every report opens with the captain's picture or number, never with a commit list.
4. **"It's implemented" is not done.** Proof is him seeing it or counting it.
5. **No game data in the repo. No faking. Refused, not guessed at.**
6. Nothing new is dispatched before the captain has seen the previous result.

*Declared 2026-09-29, the day the captain said: "lets work on Vulcan 4! a recomp of gt4."*
