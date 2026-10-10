# TODAY'S WALL — SIF0 chained-DMA routing (2026-10-10)

> **UPDATE, same day — the wall MOVED, and this section is the new measurement.**
>
> Before writing any routing code I measured the thing the plan would route, and it does not exist yet:
> **in every run of the current build our IOP executes 1,892 instructions and issues ZERO IOP→EE SIF
> DMAs.** The SIF0-routing plan is therefore correct but premature — there is no transfer to route.
>
> Cross-checked against PCSX2 the same day: real GT4 issues `sceSifSetDma` 36 times in its first
> minute, and the oracle's `ra=0x5b0d8c` at that call matches our runtime's own log line byte for byte,
> so we are looking at the right syscall. We simply never get to a state where the IOP produces
> anything.
>
> ### The two regimes (this is the real finding)
>
> | run | `functions_entered` | `halt` | `iop_instructions` | IOP modules | PDI modules |
> |---|---|---|---|---|---|
> | crew, 09:26 (`boot_w277r30u.log`) | 13,652 | **`stuck_in_syscall`** | **97,529,049** | **26** | 7 (PCDV registered) |
> | today, desktop (`boot_desk173635.log`) | 144,891 | `wallclock_deadline` | **1,892** | 5 | 0 |
> | today, handoff (`boot_handoff.log`) | 57,549 | `wallclock_deadline` | **1,892** | 5 | 0 |
> | today, capture (`boot_w281.log`) | 144,891 | `wallclock_deadline` | **1,892** | 5 | 0 |
>
> This is **not** a simple regression — the two runs are in different regimes. When the EE parked in a
> syscall (09:26) the IOP ran freely: 26 modules, 97.5M instructions, the physical PDI driver live.
> Today the EE **spins** in its dispatcher for ~145,000 functions until the deadline, and the IOP gets
> essentially nothing: 1,892 instructions, 5 modules, no PDI driver at all. Nothing loads a producer,
> so nothing can ever write the tag — which is exactly why the dispatcher spins on an unwritten address.
>
> ### The wall this leaves, and where the fix lives
>
> **The IOP must advance while the EE spins.** On hardware both CPUs run concurrently; here the IOP's
> only clock is the EE's checkpoints: `EeScheduler::checkpointDue()` → `accountCycles()` →
> `PS2Runtime::advanceIopEeCycles()` → `IopSubsystem::runEeCycles()`
> (`ps2_runtime.cpp:824`, `EeScheduler.cpp:522`). ~145,000 checkpoints bought the IOP 1,892
> instructions — about 0.013 per checkpoint — so either the charge per checkpoint is near zero on the
> spin path, or the IOP is asleep and nothing wakes it once the EE stops making the SIF calls that
> would. Both are checkable and both are in our tree, not in the game.
>
> **Order of work, corrected:** (1) make the IOP tick during the EE's spin (measure instructions per
> checkpoint, then fix the accounting or the wakeups); (2) get the PDI modules loaded again — the
> guest only requests 5 (`SIO2MAN, MTAPMAN, MCMAN, MCSERV, PADMAN`) where 26 loaded at 09:26;
> (3) *then* the SIF0 chain routing below, which is already specified and waiting.

Owner: Caine (project lead). Method: law 13 — the wall is stated as a **value**, and the spec is read
from the reference implementation, not invented.

## Where we are

The file lane is closed (see `docs/RESEARCH-CORE-GT4-DECODE.md`: the decoded `CORE.GT4` image is
byte-identical at the hand-off). What remains is the **streaming** path: the EE's command dispatcher
polls a tag at `0x00874304`, our IOP never makes it arrive, and the picture never leaves the disclaimer.

## What the oracle confirmed (measured today, live PCSX2)

- `0x00654A84` on the real machine holds **`0x20874300`** — the same value our runtime stores there.
  So the EE-side **setup is correct**: the game's SIF0 receive MADR is `0x00874300` (uncached alias).
- The EE thread layout matches ours (`tid0` at `0x00081FC0`, workers parked at `0x005ADBC8` /
  `0x005ADCE8`) — the two runtimes are at comparable states, which makes values comparable.
- `0x008851C0` (the buffer our IOP writes to) is **non-zero on hardware too** (`01 00 00 00`) — so
  writing there is not automatically wrong; the question is the *routing*, below.

## Oracle limits discovered (so nobody wastes a day on them)

- `read_memory` serves **EE RDRAM only**: MMIO (EE timer `0x10000000`, SIF regs `0x1000F520`) and IOP
  RAM (`0x1C000000`) all read back as zeros. Do not try to read hardware registers this way.
- A **memcheck on a hot address kills the emulator** (measured: watchpoint on `0x00874304` → PCSX2
  died, no core dump). Breakpoints are safe; memchecks are not. Relaunch:
  `cd /mnt/ssd/tools/pcsx2-src/build-pcsx2/bin && DISPLAY=:0 setsid ./pcsx2-qt -debugger "/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso"`
- `pcsx2_evaluate` and IOP-CPU breakpoints (`cpu: "iop"`) exist in the DebugServer — the IOP path is
  reachable when a specific IOP PC is known.

## THE SPEC — how the hardware routes SIF0 (read from PCSX2, cite it)

**First, what the call actually is.** `0x005AE064` is `syscall` with `v1 = 0x77` — PCSX2's syscall table
(`pcsx2/R5900OpcodeTables.h:23`) gives `sceSifSetDma = 119`, so our log's "pc=0x5ae068" is that call's
return address. PCSX2 does **not** HLE the transfer: its syscall body
(`pcsx2/R5900OpcodeImpl.cpp:1102`) only *logs* the descriptor (`n_transfer`, `size`, `attr`, `dest`,
`src`). The real work is done by the kernel, which programs the **DMA channels** — and that is the
mechanism our runtime does not have: `ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp::sceSifSetDma` HLEs the
call and hands RPC packets straight to `rawRpcDeliverReply`, so **no DMA channel, no chain, and no
tag is ever walked**. Its own comment records the symptom: *"The header's `dest`, by contrast, is 0 on
every GT4 transfer, so it can never supply the address."* On hardware the address does not come from the
packet header at all — it comes from the chain tag.

`/mnt/ssd/tools/pcsx2-src/pcsx2/Sif0.cpp`:

```c
// chain-tag path (line ~82-92)
tDMA_TAG& ptag(*(tDMA_TAG*)tag);
sif0.fifo.read((u32*)&tag[0], 4);          // read the tag packet word 0
sif0ch.unsafeTransfer(&ptag);
sif0ch.madr = tag[1];                      // <-- Sif0.cpp:88  THE DESTINATION IS THE TAG'S WORD 1
SIF_LOG("SIF0 EE dest chain tag madr:%08X qwc:%04X ...", sif0ch.madr, sif0ch.qwc, ...);

// transfer path (WriteFifoToEE, line ~24-57)
const int readSize = min((s32)sif0ch.qwc, sif0.fifo.size >> 2);
tDMA_TAG *ptag = sif0ch.getAddr(sif0ch.madr, DMAC_SIF0, true);
sif0.fifo.read((u32*)ptag, readSize << 2); // the data lands at sif0ch.madr in EE RAM
sif0ch.madr += readSize << 4;
sif0ch.qwc  -= readSize;
if (sif0ch.qwc == 0 && dmacRegs.ctrl.STS == STS_SIF0)
    if ((sif0ch.chcr.MOD == NORMAL_MODE) || ((sif0ch.chcr.TAG >> 28) & 0x7) == TAG_CNTS)
        dmacRegs.stadr.ADDR = sif0ch.madr;
```

**In words:** in chained (tag) mode the EE side does **not** choose the destination from the IOP's
descriptor — it reads the chain tag and sets `MADR = tag[1]`, then the FIFO drains into EE RAM at that
address until `QWC` reaches zero. Our runtime routes by the IOP descriptor (`dst=0x8851C0`), which is
why the game's buffer at `0x00874300` is never written and `0x00874304` never gets its tag.

## THE WORK, IN ORDER

1. Find our SIF0 path (`ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp` + the IOP side in
   `ps2xIOP/src/emulator/…`) and name the line where the destination is chosen.
2. Implement chained-tag routing per the spec above: walk the chain, `MADR = tag[1]`,
   `QWC = tag[0] & 0xFFFF`, drain into EE RAM, honour `TAG_CNTS`/`NORMAL` for the STADR update.
3. Log the routing decision as a value (`[SIF0] tag[0]=… tag[1]=… qwc=… -> EE write 0x…`), because the
   next wall must be readable from the log.
4. Verify: run with a capture + `VULCAN4_RDRAM_DUMP_AT_HANDOFF`-style probe for the streaming window,
   and check `0x00874304` for a non-zero tag. Then look at the picture.
5. Report: picture, or the next wall as a value.
