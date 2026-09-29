# PRIOR-ART — what the shipped recompilers actually did

**Goal X1.** Read the projects that got there first, and say what it means for our GS and VU1 path.

**How to read this.** Every factual claim carries a URL that was actually fetched, or a path in
the pinned `ran-j/PS2Recomp` clone at commit `75d729c` that was actually read. Anything I could
not verify is marked **unverified**. Licences were read from each repository, not assumed.

**Headline finding, stated once:** *nobody* in this ecosystem links an emulator's GPU core into
their recompiler. Every one either translates draw calls to a modern API or writes their own
graphics core. Emulators (Xenia, PCSX2, Ares) are used as *reading material*, never as a linked
dependency. That settles our G2 question in favour of what we already chose.

---

## 1. XenonRecomp — the tool people point at (Xbox 360)

Source: <https://github.com/hedge-dev/XenonRecomp> (README fetched from
`raw.githubusercontent.com/hedge-dev/XenonRecomp/master/README.md`).
Licence: **MIT** (queried via `https://api.github.com/repos/hedge-dev/XenonRecomp/license`).

**What it is.** Converts Xbox 360 (PowerPC) executables to C++ for recompilation. It is
explicitly *not* a runtime:

> "**DISCLAIMER:** This project does not provide a runtime implementation. It only converts the
> game code to C++, which is not going to function correctly without a runtime backing it.
> **Making the game work is your responsibility.**"

That split is the single most important structural fact in this document, and we are on the wrong
side of it: our `ps2xRuntime` *is* the runtime, and we inherited it rather than writing it.

**Graphics: none at all.** XenonRecomp does not touch graphics. It is a CPU translator. The
graphics half of that project is [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (**MIT**),
which converts Xenos *shader binaries* to HLSL, then to DXIL/SPIR-V via DXC. The runtime side is
[hedge-dev/UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) (**GPL-3.0**).

XenosRecomp states the philosophy in one line:

> "The current implementation is designed around Unleashed Recompiled, a recompilation project
> that implements a **translation layer for the renderer rather than emulating the Xbox 360 GPU**."

and UnleashedRecomp's README says the same from the other side:

> "A new renderer was written from scratch to translate the game's draw calls to modern APIs in a
> highly efficient way... As emulation of the Xbox 360's GPU is not required in a recompilation,
> many decisions were made to **skip quirks of the original hardware that are not required in a PC
> port**, resulting in great improvements in performance."

**Timing / vblank.** UnleashedRecomp raises the cap and unblocks it:

> "The game's frame rate cap has been increased by default to 60 FPS, with support for higher
> targets and unlocked frame rate being available from the options menu. A vast amount of glitches
> that usually occur at higher frame rates have been fixed."

It is honest that this is a deep hole: "fixing *all* of them is too big of a task without more
knowledge of how the game works", and some game bugs are "intentionally preserved".

**Unimplemented behaviour: loud, and specific.** XenonRecomp:

> "When a missing case is encountered, a warning is generated, or a debug break is inserted into
> the converted C++ code."

XenosRecomp carries a long explicit "Other Unimplemented Features" list (memory export, point size,
1D textures, integer constants, dynamic register indexing) and warns up front: **"Do not expect
the recompiler to work out of the box."** MMIO is "currently unimplemented".

**Reproducibility.** CMake 3.20+, Clang 18+, clone recursively. "Compilers other than Clang have not
been tested." Game config is TOML, including addresses of the ABI's register save/restore
functions, which must be hand-supplied per game ("There is currently no mechanism for it").

**Hardest part, in the author's words.** No single statement. The README's honest limits are:
exceptions are unsupported ("challenging due to the use of the link register"), jump tables have
"no fully generic solution", and indirect calls are solved with a runtime "perfect hash table".

---

## 2. N64: Recompiled — the ancestor of all of these (N64)

Source: <https://github.com/N64Recomp/N64Recomp> (README fetched from
`raw.githubusercontent.com/N64Recomp/N64Recomp/master/README.md`). Licence: **MIT**.

PS2Recomp's own README credits it: *"Inspired by N64Recomp"*, and it shares the same third-party
libraries — **rabbitizer** (instruction decoding), **ELFIO**, **toml11**, **fmt**. We inherit
`ps2xAnalyzer` → `ps2_recomp` → `ps2EntryRunner` from this exact lineage.

**Graphics: none.** N64Recomp is a CPU translator with a separate runtime
([N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)). Its graphics story lives in
the *port* project, below.

**The coprocessor (our VU1 analogue) is recompiled, not modelled:**

> "RSP microcode can also be recompiled with this tool. Currently there is no support for
> recompiling RSP overlays, but it may be added in the future."

This is the key precedent: a project's co-processor microcode is treated as *code to be
recompiled*, sitting in the same tool as the main CPU.

**Unimplemented behaviour: not documented in the README.** Hooks in the TOML are declared and then
marked "this is currently unimplemented". **unverified** whether the tool fails loudly on unknown
instructions.

**Timing / vblank: not addressed in the README.** **unverified** from this source.

**Notable, and we already use it:** "single file output" mode exists specifically so a patched
version of one function can be linked over the original by the normal linker symbol-resolution
order. `ps2xRecomp`'s `single_file_output` is the same idea, and it is how G0.4's output fix was
exercised.

---

## 3. Zelda 64: Recompiled — a purpose-built graphics core (N64)

Source: <https://github.com/Zelda64Recomp/Zelda64Recomp> (README fetched from
`raw.githubusercontent.com/Mr-Wiseguy/Zelda64Recomp/dev/README.md`). Licence: **GPL-3.0** —
the same licence as us.

**Graphics: a dedicated, accuracy-first N64 graphics engine — [RT64](https://github.com/rt64/rt64)
(MIT)** — not a draw-call translator and not an emulator's core:

> "A lot of care was put into RT64 to make sure all graphical effects were rendered exactly as they
> did originally on the N64. **No workarounds or "hacks"** were made to replicate these effects,
> with the only modifications to them being made for enhancement purposes such as widescreen
> support."

It plugs into a modern API, but the *N64 behaviour* is reimplemented faithfully underneath. This is
the opposite pole to UnleashedRecomp, and both shipped.

**The coprocessor uses an emulator as a reference, and recompiles the microcode:**

> "[Ares emulator](https://github.com/ares-emulator/ares) for RSP vector instruction reference
> implementations, **used in RSP recompilation**"

So: borrow the *understanding*, recompile the *code*. That is precisely the hybrid our G2 plan
chose, arrived at independently.

**Timing / vblank.** Handled by the graphics core, and decoupled from gameplay:

> "Play at any framerate you want thanks to functionality provided by RT64! ... By default, this
> project is configured to run at your monitor's refresh rate. ... **Changing framerate has no
> effect on gameplay.**"

That last sentence is the goal. UnleashedRecomp, by contrast, documents pages of high-framerate
glitches. Same generation of hardware, opposite outcomes.

**Reproducibility.** "Building is not required to play this project, as prebuilt binaries (which do
not contain game assets) can be found in the Releases section." The user supplies their own disc;
the project is not an emulator and "cannot run any arbitrary ROM".

**Worth flagging for us:** this project has an explicit **no-AI policy** — "contributions that use
any GenAI tooling in any capacity are blanket banned". Irrelevant to reading their code, but it
would rule out any collaboration or code contribution flowing that way.

**Hardest part, in the author's words.** Not stated in the README. **unverified.**

---

## 4. PS1Recomp (`ps1-recomp`) — the closest structural analogue to us (PS1)

Source: <https://github.com/PS1Recomp/ps1-recomp>, design doc
<https://github.com/PS1Recomp/ps1-recomp/blob/main/ARCHITECTURE.md> (fetched from
`raw.githubusercontent.com/PS1Recomp/ps1-recomp/main/ARCHITECTURE.md`). Licence: **GPL-3.0-or-later**
(`https://raw.githubusercontent.com/PS1Recomp/ps1-recomp/main/LICENSE`; the GitHub licence API
returns `NOASSERTION` only because the file carries a project-specific copyright header).

**It says outright that it copied our structure.** This is the most directly useful source in the
document:

> "The architectural lineage is explicit: N64Recomp pioneered this technique for N64 titles,
> **PS2Recomp adapted it for the PS2's R5900**, and `ps1-recomp` is the PS1 analog. The three-binary
> layout (Analyzer + Recompiler + Runtime) **follows PS2Recomp's model**; the in-tree runtime +
> TOML configs follow both."

**Graphics: re-implement the console's command stream, translate primitives onward.** Not a
borrow, not a draw-call translator:

> "GPU | `gpu/` | GP0/GP1 command stream, 1MB VRAM, OpenGL renderer"

The PS1 GPU is a packet/command-stream device with its own VRAM and pixel formats. **The PS2 GS is
the same shape of thing.** This is the strongest single piece of evidence that our approach —
keep our own GS, feed it GIF packets, read VRAM back — is the industry-standard answer and not a
quirk.

**Timing / vblank: a host timer thread, not emulated hardware.**

> "`main_host.cpp` wires it all together: it loads the disc, hands control to the recompiled entry
> point, and runs a **VBlank timer thread that increments `psyq_state().vsyncCounter` at 60Hz**."

> "VBlank, CD-ROM IRQs and audio callbacks all run on separate threads and publish to
> `recomp_context` through `psyq_state()` atomics -- never via direct shared variables."

A guest waiting for vblank waits on a counter a host thread increments. Cycle-accurate timing is
simply not attempted at this stage.

**Coprocessor (GTE): modelled in the runtime, not recompiled** — the opposite of N64Recomp.
"Every recompiled function takes `(uint8_t* rdram, recomp_context* ctx)`... GTE coprocessor
instructions are emitted by `gte_emitter.cpp` against the runtime's GTE state."

**Unimplemented behaviour: stubbed, and listed.** "MDEC / FMV: the decoder stubs out — full-motion
video is skipped, not decoded." "SPU accuracy: ADPCM envelope, reverb and pitch modulation are
**approximations**."

**Reproducibility: the practice we should copy.** The generated translation unit is *not* tracked:

> "`recompiled_out.cpp` is **gitignored** -- it is regenerated per game from the TOML config. CI
> builds use `recompiled_out_stub.cpp` (a no-op placeholder) so **the build is green without any
> ROM present**."

**Honest limitations, quoted:**

> "**One title fully validated**: Rayman (USA) ran end-to-end at ~59fps... The supporting imperative
> patch script was retired during open-source preparation; the regen-fresh path now relies entirely
> on the PsyQ HLE coverage and **has not been re-validated headed since**."
> "**Crash Bandicoot 1**: experimental. Boots through PsyQ init, then stalls in a game-side hash
> table walk before the title screen."

**A genuinely better HLE idea than ours.** Instead of hand-annotating per game, it fingerprints
middleware: "3463 SHA-256 hashes covering 14 PsyQ SDK releases", matched at analysis time so "the
same runtime stubs work across games that happen to link the same SDK version."

**Hardest part, in their words.** Not stated directly, but the honest limitations section *is* the
answer: one title validated, and a stall traced to "a signature collision in short libcd wrappers".

---

## 5. PSXRecomp (`mstan/psxrecomp`) — the most shipped PS1 effort (PS1)

Source: <https://github.com/mstan/psxrecomp> (README fetched from
`raw.githubusercontent.com/mstan/psxrecomp/master/README.md`).
**Licence: PolyForm Noncommercial 1.0.0** — see the licence section, this one matters.

Six playable titles: Tomba!, Tomba! 2, Ape Escape, Mega Man X4/5/6, Tsumu Light, plus a community
Xenogears port.

**Graphics: three backends, and the software one is the reference.**

> "**Software** | CPU rasterizer — **the reference look, and the most portable fallback**."
> "**OpenGL** | **Default.** GPU-authoritative VRAM/FBO renderer"

This is our exact G2.0/G2.1 shape — a CPU rasteriser as the trustworthy baseline with a frame on
disk, plus faster backends later. They reached it independently and call it the reference.

**Enhancements that keep the guest honest** — a pattern worth stealing wholesale:

> "Both are visual-only: **the GTE's guest-visible screen coordinates stay integer and fully
> faithful, so game logic and culling are unaffected.**"

i.e. fix the *renderer*, never the numbers the game can see. Their widescreen is "computed at
recompile time by widening the game's own projection and culling maths" rather than a crop.

**LLE-first, and it is an explicit ordering rule:**

> "that low-level (LLE) recompiled BIOS is the foundation and the correctness oracle. Everything is
> architected **LLE-first: accuracy comes first, and convenience is layered on top, opt-in, never
> underneath**."

> "**The worst case is always performance, never correctness** — anything not yet native simply
> runs interpreted, correctly."

**Timing: faithful by default, fast only if you ask.** "The runtime models **authentic 1× CD-ROM
timing by default**... On top of that faithful baseline, load-time acceleration is **opt-in**, per
game, so the accurate path is never compromised." And they are honest that exactness is *later*
work: "Interrupts, COP0, timers, GTE | Working; **cycle-accuracy foundation is an active
depth-phase focus**."

**Unimplemented behaviour: a correctness net, not a stub.** A small MIPS interpreter runs anything
not yet recompiled, and is explicitly meant to be compiled away: "The more a game runs, the less
the interpreter is doing."

**Reproducibility: honest about fresh clones, and pins disc revisions.** "the recompiled BIOS C is
build output, not tracked, so a fresh clone has none and the runtime configure fails with 'No
recompiled BIOS backend available'." Mods carry a `disc_sha256` and "fail closed on the wrong
revision". And a rule we should adopt verbatim: "**generated code is never hand-edited** (fix the
recompiler and regenerate)".

**Hardest part, in the author's words** — and it is about visibility, not about the GPU:

> "PS1 games make that goal hard in one specific way: **overlays.** Games stream code off the disc
> into RAM at runtime and execute it, then overwrite it with the next overlay. That code does not
> exist in the executable at build time, so a pure ahead-of-time recompiler cannot see it."

**Licence warning.** PolyForm Noncommercial 1.0.0 is not an OSI-approved licence and is
**incompatible with our GPL-3.0 project**. We may read it for ideas — copyright protects expression,
not ideas — but **no code may be taken from it**, and nothing derived from it may enter our tree.
Note it also distributes OpenBIOS under MIT "that we're allowed to distribute", which is the
licence-compatible part.

---

## 6. PS2Recomp — the base we inherited

Source: the pinned clone at `tools/PS2Recomp`, commit **`75d729c`**, README read locally.
Upstream: <https://github.com/ran-j/PS2Recomp>. Licence: **GPL-3.0** — ours already.

**First, a correction to the brief.** The task described PS2Recomp's author as saying it "does not
work properly". **I could not verify that sentence.** I searched the entire pinned tree for it
(`grep -rn -iE "does not work properly|not work properly|doesn't work properly"` across all `.md`
and `.txt`) and got **zero hits**. It may come from an issue, a Discord, or an older commit.
**Treat it as unverified.** What the README actually says is narrower and more useful:

> "Basic GS/VU/file/system stubs."
> "## Limitations
> * Performance is very bad for VU and GS
> * Hardware emulation is partial and many paths are stubbed."

So the GS and VU were stubs from the start, and remain the named weak point. That matches our
experience exactly, and it is the honest version of the claim.

**The Recommended Iteration Loop, quoted in full** — this is upstream telling us how to work, and
we have been improvising:

> 1. Run with minimal config and no aggressive skipping.
> 2. Fix hard blockers first (`function not found`, syscall TODO, critical IO stubs).
> 3. Use temporary return stubs only to classify call importance.
> 4. Promote temporary fixes to real implementations.
> 5. Move per-game hacks into game overrides keyed by ELF metadata.
> 6. Re-test from cold boot after each batch.

**Game Override Hooks** are "runtime-side, build-scoped patch modules", registered by
`PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)` — keyed on ELF metadata so a
hack cannot leak into a different build of the same game. We have not used this at all.

**Graphics: "Reference for runtime PCSX2".** Upstream names PCSX2 as its reference. Note what that
means: PCSX2 is a *reading source* for the runtime, exactly the role Ares plays for Zelda64Recomp
and Xenia plays for XenonRecomp. Not a linked core.

---

## What WE do differently

Ten items. Each is an action, not an observation.

1. **Keep our own GS. Do not import PCSX2's. This is now evidence-backed, not just taste.**
   Every shipped project either translates draw calls (UnleashedRecomp) or writes its own graphics
   core (RT64, PS1Recomp's GP0/GP1 engine, PSXRecomp's three backends). **Not one links an
   emulator's GPU core.** PCSX2 stays a *reference to read*, on the same footing as Xenia for
   XenonRecomp and Ares for Zelda64Recomp. `docs/GS-PLAN.md` already decided this; this is the
   external evidence for it.

2. **Make the CPU rasteriser the reference, not a fallback.** PSXRecomp calls its software
   rasteriser "the reference look, and the most portable fallback". Our `GSCpuBackend` +
   `tools/gs/vulcan4_gs_probe.cpp` is already that shape. Rule to adopt: **any GS behaviour we are
   unsure of gets validated against the CPU path**, and every frame we claim to have drawn must
   have a non-zero pixel count. Our current frame is blank (`non_background=0`) and that must stay
   visible in the logs until it is not.

3. **Fix the four GS defects in `ps2xRuntime` before writing any new GS code.** G2.0 found that the
   GIFTAG register field is 4 bits so `FRAME_1`/`ZBUF_1`/`SCISSOR`/`FINISH` are unreachable from a
   GIF packet, that the presentation path decodes DISPFB/DISPLAY in a private bit layout, that
   `Present` fails silently, and that our triangle never submits. Every prior project treats its
   graphics layer as the real product. Ours is currently the part with known holes.

4. **Model vblank as a host clock first; do not attempt cycle accuracy now.** PS1Recomp satisfies
   the guest with "a VBlank timer thread that increments `vsyncCounter` at 60Hz"; PSXRecomp puts
   cycle accuracy in an "active depth-phase focus". This is the most likely reason our guest is
   `stuck_in_syscall` — it is probably waiting on a sync or interrupt we never satisfy. Build the
   counter and the CSR signalling it feeds *before* optimising anything.

5. **Make every unimplemented path loud and named, never silently blank.** XenonRecomp inserts "a
   warning ... or a debug break"; XenosRecomp ships an explicit unimplemented list. Our `Present`
   returning an empty frame with no message is the failure mode they both avoid. Standing rule:
   **a wrong result must never look clean.**

6. **Keep a correctness fallback for anything not yet recompiled, and bias toward precision over
   recall.** PSXRecomp: "The worst case is always performance, never correctness." For the VU1 that
   means an interpreter or reference model as a net, never a plausible-looking approximation.
   For the GS it means a blank frame and a named error, never a plausible-looking wrong frame.

7. **Decide the VU1 the way the evidence points, and write it down before coding.** There are two
   proven strategies: N64Recomp and Zelda64Recomp **recompile the microcode** (Zelda using Ares as
   a reading reference), while PS1Recomp **models the coprocessor in the runtime**
   (`gte_emitter.cpp` against runtime GTE state). PS2's VU1 microcode ships *inside the guest
   executable*, which makes recompiling it more feasible than the N64 case. Decision needed at
   G3.1, with the reason recorded either way.

8. **Hash-recognise the SDK instead of hand-annotating it.** PS1Recomp fingerprints 3,463 PsyQ
   functions by SHA-256 across 14 SDK releases so one runtime serves every game on that SDK. Our
   equivalent is the PS2 SDK/EE core libraries. This replaces per-game syscall guesswork with
   something automatic — and it is how PS1Recomp found a real bug ("a signature collision in short
   libcd wrappers").

9. **Gitignore the generated translation unit and let CI build a stub.** PS1Recomp: `recompiled_out`
   is gitignored and CI links a no-op placeholder "so the build is green without any ROM present".
   Our G0.5 proved regeneration is byte-reproducible, so tracking 8.9 MB of generated C buys us
   nothing and costs a stranger a 9-minute recompile.

10. **Adopt upstream's own iteration loop and its game-override mechanism.** PS2Recomp's README
    gives us a six-step loop we have been improvising against, and `PS2_REGISTER_GAME_OVERRIDE`
    keys per-game hacks on ELF name + entry + CRC32 so a hack cannot leak into another build. We
    have never used the override mechanism. Follow the documented loop: minimal config, fix hard
    blockers, temporary stubs only to classify importance, promote to real implementations, re-test
    from cold boot.

**On the two licence traps.** Everything useful to us here is either **MIT** (XenonRecomp,
XenosRecomp, N64Recomp, RT64 — reuse freely, attribution only) or **GPL-3.0 / GPL-3.0-or-later**
(UnleashedRecomp, Zelda64Recomp, PS1Recomp, PS2Recomp — compatible with us; copying obliges us to
carry notices, state the change, and offer source). The one to watch is **psxrecomp at
PolyForm Noncommercial 1.0.0**: ideas are fine, code is not, and nothing derived from it may enter
this tree.

**One cultural note.** Zelda64Recomp bans GenAI contributions outright, and psxrecomp is explicitly
an AI-assisted project. We are GPL-3.0, which permits us to use all of the above; it does not
oblige us to agree with either position, but it does mean we should not assume any of these
projects will accept a patch from us.
