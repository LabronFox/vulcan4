# WHAT WE ACTUALLY NEED — VULCAN 4 gap audit, 2026-10-09 (Caine)

Written to answer one question honestly: *if the game's own code is 100% available, what is still missing?*
Everything below is measured on this machine, or names a file that exists on this machine.

## What we already own (measured, not claimed)

| Piece | Evidence |
|---|---|
| Recompiled EE code executes | 701,966 functions entered in the 10-minute `dante10` run |
| GS rasteriser | `ps2xRuntime/src/lib/gs/{gs_frontend,gs_cpu_backend,ps2_gif_arbiter,ps2_gs_memory}` + the triangle oracle passed exactly: `vulcan4-build/gs/triangle.png`, 39,936 non-background px = the analytic prediction |
| IOP (R3000A) interpreter | `ps2xIOP/src/emulator/{iop_emulator,core/iop_cpu,core/iop_kernel,services/iop_module_loader}` — 95.5M IOP instructions executed (`R30u`) |
| Game's IOP drivers | extracted from the disc: `gt4/work/IRX/` (27 modules incl. LIBPDI, PDISTR, PDISPU2, PDIUSB, PDICDVD, USTORAGE, LGDEV) |
| Game logic | `/mnt/ssd/vulcan4-ref/OpenAdhoc` — 100% of GT4's own code in readable source (boot + menus included) |

## What is missing (the actual wall)

**The data path between the two CPUs.** Not the game, not the CPU, not the renderer — the plumbing:
SIF0/SIF1 DMA, the SIFCMD/SIFRPC command queues, IOP→EE interrupt delivery, and cross-CPU
scheduling. Evidence from our own runs:

- the EE dispatcher at `0x5608e0` **busy-polls** a command tag at `0x00874304` that is never written (`R30ac`)
- the producer thread (`tid1`) is **starved** — our scheduler never gives it the CPU (`R30ac`)
- GS received only **15 packets** in 10 minutes; ~180 interrupts queued with `irq_attach=0`
- `loadfile.cpp` answers the module-load RPC with an oracle-measured word and states in its own comment: *"This is not a module loader"*

## We do not need to invent any of it — it already exists on disk

`/mnt/ssd/tools/pcsx2-src` (483 MB, full source tree) contains a proven implementation of every one
of those layers, written by people with a working oracle:

| Missing layer | PCSX2 reference file(s) |
|---|---|
| SIF DMA (both directions) | `pcsx2/Sif.cpp`, `Sif0.cpp`, `Sif1.cpp`, `sif2.cpp`, `Sif.h`, `Sifcmd.h` |
| EE ↔ IOP DMAC | `pcsx2/Dmac.cpp` / `Dmac.h`, `pcsx2/IopDma.cpp` / `IopDma.h` |
| IOP register + IRQ model | `pcsx2/IopHw.cpp`, `IopIrq.cpp`, `IopCounters.cpp`, `IopMem.cpp` |
| IOP kernel/RPC semantics | `pcsx2/IopBios.cpp` / `IopBios.h`, `IopModuleNames.cpp` |
| VBlank ordering (the 1-frame-freeze class of bug) | `pcsx2/Counters.cpp`, `GS.cpp` |
| GIF → GS packet path | `pcsx2/Gif.cpp`, `Gif_Unit.cpp`, `ps2/Gif` |

**Licensing is clean:** PCSX2 is LGPL-3.0, VULCAN 4 is GPL-3.0 — combining them is permitted.
The rule is **port the semantics, not the code**: read how the mechanism behaves, implement it in
`ps2xIOP`/`ps2xRuntime`, and cite the PCSX2 file + function in the commit message so the next reader
can check the claim instead of trusting it.

## The rule this audit implies for the crew

1. Before inventing a model of any hardware mechanic, open the PCSX2 file above and read it.
2. Every wall must name the mechanism (`SIF1 DMA completion`, `IOP IRQ line 9`, `VBlank → event flag`)
   — not just the address where the guest spins.
3. No new hand-written HLE stub for a service that a real IRX can provide: LLE first, HLE only as a
   documented fallback (this is what the `pdiperiph` stub path got wrong).
4. A wall that survives more than two turns moves to the "invented vs measured" question: is the
   mechanism modelled at all, or guessed at?
