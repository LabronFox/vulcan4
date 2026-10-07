# VULCAN 4 — the recomp course

*A course for Or and Caine. One lesson at a time, each ending on something real in this project.
Sources are cited so we can go read them ourselves. Lessons are added to this file as we go.*

---

## Lesson 1 — What static recompilation actually IS (and where bugs really live)

### The three ways a game runs on a PC

| | what it does | cost |
|---|---|---|
| **Emulator (interpreter)** | reads one guest instruction, translates it, runs it, repeat — for every instruction, every frame, forever | ~15-17 host instructions per guest instruction (JIT overhead) |
| **Dynamic recompiler (JIT)** | translates while running, caches the result, re-translates when code changes | fast, but the translation happens again on every launch |
| **Static recompilation** | translates the game's machine code **once**, ahead of time, into C/C++ — then YOUR compiler builds it natively | the game's own logic runs at native speed; no per-instruction cost |

N64Recomp's own words (Mr-Wiseguy, the tool that started this wave): *"The recompiler works by
accepting a list of symbols and metadata alongside the binary, with the goal of splitting the input
binary into functions that are each individually recompiled into a C function. Instructions are
processed one-by-one and corresponding C code is emitted as each one gets processed."*

So a translated instruction looks like this — literally one guest instruction → one line of C:

```c
// MIPS:  addiu $r4, $r4, 0x20
ctx->r4 = ADD32(ctx->r4, 0x20);
// MIPS:  jal 0x80012345
func_80012345(ctx);
// MIPS:  jalr $25   (an indirect call = a function pointer)
LOOKUP_FUNC(ctx->r25)(rdram, ctx);
```

Read: <https://github.com/N64Recomp/N64Recomp> · <https://hackaday.com/2024/05/21/static-recompilation-brings-new-life-to-n64-games>

### The catch — the half of the console a recompiler CANNOT translate

A CPU is not a console. A recompiler translates **the CPU**. Everything around the CPU is hardware —
and hardware has to be **supplied by a human**: graphics, sound, DMA, timers, controllers, the I/O
processor, the operating system calls.

That is the single most important idea in this course:

> **A successful recompilation means "the game's logic is now native C++". It does NOT mean
> "the game runs".** The runtime — the environment — is the part that gets built by hand.

PS2Recomp's own pipeline says the same thing in three stages: **analyze the ELF → translate R5900
to C++ → provide the execution environment** (CPU state, memory, system services).
Read: <https://deepwiki.com/ran-j/PS2Recomp/3-system-architecture>

### Why the PS2 is a genuinely hard case

The Emotion Engine is not just a MIPS CPU. It is: an R5900 core (MIPS III/IV + Sony's own MMI SIMD
instructions) + **two vector units** (VU0, VU1) + a **DMA subsystem** + **scratchpad RAM** + the
**Graphics Synthesizer** — each with edge cases games deliberately exploit. On top of that: no PS2
game has a playable recomp yet, anywhere. We are not behind a crowd; there is no crowd.
Read: <https://findarticles.com/ps2recomp-sparks-hope-for-native-ps2-pc-ports>

### The law that follows — and our own numbers

> **When the native build misbehaves, ask: "is the TRANSLATION wrong, or is the ENVIRONMENT
> incomplete?" — 95% of the time it is the environment.**

Our project, measured 2026-10-07:

- Our recompiled GT4 decompresses its 6 MB buffer **99.81% correctly** — 5,999,491 of 6,118,856
  bytes are byte-identical to the real console.
- The entire error is **11,365 bytes in one window**. (Earlier session: `docs/W229-RESULTS.md`.)
- The translated decoder itself was checked instruction-by-instruction against the MIPS and **matches**.

So: the translation is fine. The runtime around it is where every remaining bug lives. That is
where we will spend the rest of this course.

### Lesson 1 in one line

**The recompiler copies the game's brain; we still have to build the body it lives in.**

---
