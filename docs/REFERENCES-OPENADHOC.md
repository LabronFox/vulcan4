# REFERENCE: OpenAdhoc — GT4's own game logic, in source form

**Handed to this project by the captain, 2026-10-08.** Clone: `/mnt/ssd/vulcan4-ref/OpenAdhoc`
(upstream `https://github.com/Nenkai/OpenAdhoc`, cloned shallow, 40 MB, GPL-v3).
Do not vendor it into this repo — reference it by path.

## Why it matters to VULCAN 4

OpenAdhoc is an **open-source re-implementation of "Adhoc"**, the proprietary scripting language used
by the Gran Turismo games — and **its GT4 coverage is marked ✅ 100 %**: the compiled GT4 scripts have
been re-created as editable source.

The upstream README states the architecture plainly:

> *"Scripts operate nearly as the whole of game logic, while the executable mostly serves as the engine
> and exposes libraries to the script interface."* — and each game mode is a **project folder**
> containing the Adhoc logic script (`.adc`), the UI definition (`.mproject`), and assets (.gpb).

**Consequences for us, in order of usefulness:**

1. **The menu is a script.** GT4's menu flow / event structure is Adhoc logic, not hand-written EE code.
   When our runtime gets past the current boot walls, the code path that produces the menu runs
   *through the script engine* — so the script VM is on the critical path to R5/R6/R7 and is worth
   studying BEFORE we hit it, not after.
2. **It documents the engine interface.** The script library surface (what the executable must expose
   to scripts) is a spec we can check our own syscall/module implementations against — an independent
   oracle for behaviour we have so far reconstructed one wall at a time.
3. **It is a second opinion on the RPC/file path.** The scripts' expectations around project/asset
   loading line up with the DVD-streamer wall we stopped on — useful for naming what the game actually
   wants loaded, rather than inferring it from a syscall stall.
4. **Licensing note:** GPL-v3 with a disclosure requirement. Read/inspect freely; if any code is ever
   reused, the obligation travels with it. Keep it a reference, not a dependency.

## Status of this note

Written while the crew was standing down, so the next session — human or agent — opens with it rather
than rediscovering it. Also cross-linked from `.auto/crew/board.md`.
