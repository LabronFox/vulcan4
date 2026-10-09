# WHAT WE ARE MISSING FOR THE GAME TO BOOT — measured 2026-10-09, Caine

Bottom line: **we are not missing a trick in the interrupt or GS code — we are missing the IOP.**
The game's own IOP modules never execute; we answer them with HLE stand-ins, so the SIF DMA they
would issue never happens and the EE-side dispatcher spins on a tag nobody writes.

## The evidence chain (all measured today, this machine)

1. **The runtime HAS a real R3000A IOP.** `tools/PS2Recomp/ps2xIOP/src/emulator/` contains
   `core/iop_cpu.cpp`, `core/iop_kernel.cpp`, `core/iop_memory.cpp`, `services/iop_module_loader.cpp`,
   `services/iop_rpc.cpp`, `iop_emulator.cpp`. `ps2xIOP/README.md`: *"`ps2xIOP` runs original IRX
   modules on an R3000A interpreter, with a virtual IOP kernel providing imports without a PS2 BIOS…
   A physical IRX RPC server is authoritative for its SID."*

2. **The game's own modules are on the disc.** `isoinfo -f` on
   `/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso` lists `/IRX/` with 27 modules, including the
   Polyphony/PDI set: `LIBPDI.IRX`, `PDISTR.IRX`, `PDISPU2.IRX`, `PDIUSB.IRX`, `PDICDVD.IRX`,
   `USTORAGE.IRX`, `LGDEV.IRX`. They are already extracted to `/mnt/ssd/gt4/work/IRX/`.

3. **Not one of them executes on our boot.** In the 10-minute run I ran today
   (`/mnt/ssd/vulcan4-build/run/boot_dante10.log`) there are **exactly 5** module-load lines and
   **5** `load-emulated` lines, and **0** lines mentioning `R3000`:

   ```
   1079:[SIF module] load-emulated id=1 ref=1 path="cdrom0:\IRX\SIO2MAN.IRX;1"
   1081:[SIF module] load-emulated id=2 ref=1 path="cdrom0:\IRX\MTAPMAN.IRX;1"
   1085:[SIF module] load-emulated id=3 ref=1 path="cdrom0:\IRX\MCMAN.IRX;1"
   1087:[SIF module] load-emulated id=4 ref=1 path="cdrom0:\IRX\MCSERV.IRX;1"
   1089:[SIF module] load-emulated id=5 ref=1 path="cdrom0:\IRX\PADMAN.IRX;1"
   ```

   The PDI/Polyphony modules (`PDISTR`, `PDISPU2`, …) are **never even requested** by the EE side.

4. **The PDI sids are answered by hand-written stubs, not by IOP code.**
   `ps2xIOP/src/module_factories.h`: `createPdiPeripheralService` — *"Remaining PDI services: PDISTR
   (stream), PDISPU2 (audio), PDIUSB (USB host/ext/kb)."* The crew's own record (`docs/W277-RESULTS.md`,
   R30m) states it plainly: *"the producer is the IOP module the stubs replaced: the real PDISTR/PDISPU2
   etc. write the command into the queue via `sceSifSetDma` when they finish; my stubs signal completion
   but never issue that DMA, so the dispatcher spins on a never-written tag."*

5. **The EE-side LOADFILE service is not a loader.** `tools/PS2Recomp/ps2xIOP/src/modules/loadfile.cpp`
   says so in its own header comment: *"This is not a module loader. Nothing here reads an IRX or an ELF
   off the disc"* — it answers sid `0x80000006` function `0xff` (not the documented `00h` SifLoadModule)
   with an oracle-measured word (`0x30303033`) and refuses the documented functions.

## What that means for the wall we are stuck on

`sce_StartThread` parked at `pc=0x005608e0` (tid10, prio 13) is a **symptom**. The dispatcher reads a
command tag at `0x00874304`, never finds `0x010B2400`/`0x010B0000`, and `goto`-loops until
`eeCheckpointDue()`. The tag is dynamic and appears nowhere as a literal in the recompiled engine — it
is written by an IOP producer over SIF DMA. With no IRX executing, there is no producer. The ~900
queued interrupts and `irq_attach=0` ride along with the same stall.

**So the missing subsystem, in one line: real IOP module execution (LLE) instead of HLE stand-ins for
the game's own modules.**

## Concrete first actions (in order, each measurable)

1. **Make the fallback loud.** In `ps2xIOP/src/iop_subsystem.cpp::loadModule` the physical load is tried
   only when `parsed.device != Rom0`; when `emulator.loadModule()` returns `moduleId <= 0` it silently
   drops to `moduleManager.loadHle(path)`. Log *why* the physical load failed (path translation, file
   open, IRX parse, missing kernel import) at the moment it fails. Today the boot log cannot even tell us
   which route each of the 5 loads took.
2. **Prove one real IRX executes.** Take one standard module we already load (`SIO2MAN.IRX`/`LIBSD.IRX`),
   force the physical path, and show a nonzero IOP instruction counter / module-execution line in the
   boot log. That converts "the IOP exists" from a claim into a measurement.
3. **Ask the oracle which module writes the tag.** With PCSX2 live, break on the write to `0x00874304`
   (and on the LOADFILE rpc), and record: which module, what it was asked to load, at which EE cycle,
   and the exact DMA payload. That decides the order in which we must load `PDI*.IRX`.
4. **Then retire the stubs one at a time.** Replace `createPdiPeripheralService` (or the individual
   services) with the real IRX loaded from `cdrom0:\IRX\...`, and let the physical server register its
   own sid — per the README's own rule, a physical IRX RPC server is authoritative for its SID.
5. Only after that should the interrupt/frame pipeline be re-measured: `intr_run`, GS packets and the
   capture will move because a producer exists, not because the scheduler was patched.

## Honest caveats

- I did **not** yet prove which route the game takes for the PDI modules (EE-side `SifLoadModule` vs an
  IOP reboot image whose IOP-side code loads them). Either way, they must execute; the oracle trace in
  action 3 settles it.
- `load-emulated` is a *result* log line, not proof of HLE substitution on its own. What is measured and
  certain: five loads, none of the PDI modules requested, a stub set registered for the PDI sids, and no
  IOP execution evidence in the log.
