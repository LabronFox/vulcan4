# W283 — THE SIF-INTERRUPT BRICK WAS WRONG; THE WALL WAS CDVDFSV CdInit (2026-10-10)

*Method: law 13 — every claim is a value from a run, and the fix was measured before and after. The
new probes ship OFF by default; the CdInit routing change is the correct per-SID behaviour.*

## TL;DR

W282's "next brick" said *raise the IOP's SIF interrupt on an EE→IOP transfer, then ask whether
threads 1/3 wake*. That brick is **refuted by measurement**: the IOP's SIF1 DMA interrupt (INTC line
`0x2B`) has **no handler registered**, so raising it dispatches nothing (`dispatched=0` × 37). The only
interrupt the five boot IRX modules register is **SIO2 (line `0x11`/17)**. The SIF RPC is HLE'd on the
EE side, not serviced by IOP guest code, so "wake the IOP via a SIF interrupt" was the wrong lever.

The actual wall — found with the `VULCAN4_SIFRPC=1` honest path — was that the **CDVDFSV service
decoded the wrong interface**. GT4's SONY cdvdfsv is **per-SID** (ps2tek 09 L6805-6813), each SID its
own RPC server; our `CdvdService` was written against ps2sdk's **merged** numbering (rpc1=init,
rpc2=disk-ready, …). GT4's first CDVD call is **`CdInit` (`0x80000592`) `rpc=0`**, which hit the
`default` case and was refused → the EE retried the CDVD init forever. Routing `0x80000592 rpc=0` to
the init reply **woke the IOP and advanced the boot**: IOP instructions went **1,892 → 175,220,688**
and loaded modules **5 → 26**; the halt moved from `wallclock_deadline` (CDVD retry at `0x00580dd8`)
to `stuck_in_syscall` (blocked in EE syscall `0x22` StartThread at `0x005608e0`) — the **10-09 regime**
W279 was working from, i.e. a *later* stage of boot.

## The measurement that killed the SIF-interrupt brick

New probe (off by default): `IopIntrman` logs every `RegisterIntrHandler`/`EnableIntr` line, and
`IopEmulator::raiseSifInterrupt()` (gated `VULCAN4_SIF_INTR=1`) dispatches the SIF1 line `0x2B` and
reports whether a handler ran. `run/boot_w283sif.log`:

```
[IOP:intrman] RegisterIntrHandler line=17 handler=0x10584 arg=0x10fc8 gp=0x18fb0
[IOP:intrman] EnableIntr line=17
...
[IOP:sifintr] line=0x2b dispatched=0 instr=1892 threads=6   (×37)
```

- **One** interrupt handler is ever registered: **line 17 = `0x11` = INT_SIO2** (ps2tek intrman table),
  handler `0x10584` inside SIO2MAN (module 1, base `0x10000`).
- The SIF1 line `0x2B` (`INT_dmaSIF1`) has **no handler** — `dispatchInterrupt` returns false every time.
- So the W282 next brick would have dispatched to nothing; threads 1/3 (SIO2MAN/MTAPMAN, waiting on
  event flags #1/#2 for the SIO2 interrupt) are not the thing a SIF interrupt can wake.

Why: the five modules loaded at that point are SIO2MAN/MTAPMAN/MCMAN/MCSERV/PADMAN (all I/O drivers,
all from `cdrom0:\IRX\`). SIFCMD/SIFINIT (which would register the SIF interrupt) live in the IOP ROM
and are never loaded — the SIF RPC is HLE'd EE-side. So the IOP has nothing to wake *on the SIF line*;
its threads are I/O threads waiting on the SIO2 interrupt, which is irrelevant to the CDVD call the EE
is retrying.

## The actual wall (VULCAN4_SIFRPC=1, `run/boot_w283sifrpc.log`)

```
[SIFRPC] bind a=0x874fa8 b=0x80000592 ...          (CdInit)
VULCAN 4 LIMITATION: CDVDFSV sid=0x80000592 rpc=0 is not implemented — unknown rpc number
[SIFRPC] call a=0x874fa8 b=0x0 c=0x80000592 d=0x1
[SIFRPC] bind a=0x657a40 b=0x80000593 ...          (CdSCmd)
VULCAN 4 LIMITATION: CDVDFSV sid=0x80000593 rpc=34 is not implemented
[SIFRPC] call a=0x657a40 b=0x22 c=0x80000593 d=0x1
[SIFRPC] reboot-iop arglen=32 mode=0 arg="rom0:UDNL cdrom0:\IOPRP300.IMG;1"
```

ps2tek 09 L6805-6813 names the per-SID interface (grounding, not invention):

| SID | Server | GT4's measured rpc |
|---|---|---|
| `0x80000592` | **CdInit** | `rpc=0` (init) |
| `0x80000593` | **CdSCmd** (S-command channel) | `rpc=34` (`0x22`) |
| `0x80000595` | CdNCmd (N-command channel) | — |
| `0x80000597` | CdSearchFile | — |
| `0x8000059A` | CdDiskReady | — |

`CdvdService` (ps2xIOP/src/modules/cdvd.cpp) was written against ps2sdk's *merged* interface
(`rpc1=init, rpc2=disk-ready, rpc3=SCMD, rpc4=searchfile`) and routed none of GT4's actual numbers, so
`CdInit rpc=0` was refused → the EE's CDVD init retried forever.

## The fix (measured before/after)

Route `0x80000592 rpc=0` to the existing init reply (retres=1 + version words, already labelled a
limitation for the version values). One block in `CdvdService::handleRpc`. The other per-SID servers
(CdSCmd rpc=34, CdNCmd, CdSearchFile) are still refused honestly — they are the *next* wall, not this
one.

| metric | before (`boot_w283sif.log`) | after (`boot_w283cdvdinit.log`) |
|---|---|---|
| IOP instructions | **1,892** | **175,220,688** |
| IOP modules | **5** | **26** |
| EE halt | `wallclock_deadline`, spins `0x00580dd8` | `stuck_in_syscall`, `0x22` StartThread `0x005608e0` |
| EE threads | 1 | **10** |
| functions_entered | 58,067 | 13,906 (different regime — not a comparator) |

The halt moving from the CDVD retry loop to **StartThread at `0x005608e0`** is exactly the **10-09
regime** W279 recorded (`10 EE threads / 26 IOP modules / blocked inside syscall 0x22`), i.e. the boot
advanced to a *later* stage. The IOP waking (92,600× more instructions) is the countable product claim.

## Next brick

Two walls remain, in order of first divergence:

1. **`CdSCmd` (`0x80000593`) `rpc=34` (`0x22`) is refused** — still fires after the fix
   (`boot_w283cdvdinit.log` carries 2 "CDVDFSV … rpc= is not implemented" lines). `0x22` is **not** in
   ps2tek's S-command list (L3379-3454), so its semantics must be read from GT4's own cdvdfsv (oracle:
   break the CdSCmd rpc handler and read the reply) — **not guessed**.
2. **EE syscall `0x22` (StartThread) at `0x005608e0`** is the current halt (the wall W275/W277 were
   already on; the SIF0-chain plan in `docs/SIF0-CHAIN-PLAN-REVIEWED.md` is the route).

## Probe/limitation notes

- `IopIntrman` now logs `RegisterIntrHandler`/`EnableIntr` (capped 64 lines, low-volume, module-init
  time only) — kept on, it is the instrument that settled the SIF-interrupt question in one run.
- `VULCAN4_SIF_INTR=1` raises the SIF1 line `0x2B`; it is a **documented negative-result probe** (no
  handler exists), off by default and harmless.
- `CdvdService` still refuses CdSCmd/CdNCmd/CdSearchFile/CdDiskReady rpc numbers it has not decoded —
  the honest-law behaviour, unchanged.
