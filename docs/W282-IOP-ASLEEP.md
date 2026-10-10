# W282 — THE IOP IS NOT STARVED OF CLOCK, IT IS ASLEEP (2026-10-10)

*Method: law 13 — the wall is stated as a value, and the value comes from a run. Every number below
names the probe, the log and the commit that produced it. The probe ships **OFF by default**.*

## TL;DR

W279's fix order began with *"make the IOP tick while the EE spins"* on the theory that the IOP's only
clock is the EE's checkpoints (0.013 IOP instructions per checkpoint). **Measured today: that theory is
wrong.** The IOP is handed **387,001,368 cycles** in a 45 s boot and executes **1,892 instructions**,
because **99.95 % of its scheduling iterations have no runnable thread**. The IOP is asleep, not
starved — and nothing in our runtime can wake it.

## The probe (new, off by default)

`VULCAN4_IOP_DIAG=1` (counters) / `=2` (counters + one line per IOP thread). Implementation:
`ps2xIOP/src/emulator/iop_emulator.cpp` (`Impl::diagReport`, counters in `runCycles`/`runEeCycles`) +
`IopKernel::debugStats()` / `debugThreads()` in `core/iop_kernel.{h,cpp}`. Unset = the code path is
byte-for-byte what it was.

Counters: `calls` (runEeCycles entries), `eeCycles` (EE cycles handed to the IOP), `iopCycles` (what
those bought, ee/8), `instr`, `slices` (iterations that had a thread), `idleIters` (iterations with
**no** runnable thread), `throws` (exceptions swallowed by `runCycles`' `catch (...)`).

## The measurement

`run/boot_w282diag.log` (45 s, `run_boot_named.sh w282diag 2000000 45`, final line):

```
calls=97980000 eeCycles=3094327273 iopCycles=386790908 instr=1892
slices=97 idleIters=97930203 throws=0
threads=6 ready=0 running=0 delay=0 sleep=4 sem=0 evt=2 dormant=0 dead=0 susp=0 earliestWake=0
```

| Claim | Value | Meaning |
|---|---|---|
| IOP cycles granted | **386,790,908** | the clock is *not* the problem |
| IOP instructions executed | **1,892** | 0.0005 % of the budget |
| scheduling iterations with a runnable thread | **97** of 97,980,000 | 99.95 % idle |
| IOP threads | **6**, none Ready | 4 `Sleep` (indefinite), 2 `EventFlag` |
| scheduled wakes | `earliestWake=0` | no `Delay` thread, so no timer will ever fire |
| exceptions swallowed | 0 | not a hidden throw |

### The six threads, named (`VULCAN4_IOP_DIAG=2`, `run/boot_w282thr.log`)

| id | state | entry | pc | prio | waiting on |
|---|---|---|---|---|---|
| 1 | EventFlag | `0x1039c` | `0x103c4` | 16 | flag **#1**, bits `0x4155`, mode 1 (AND) |
| 2 | Sleep | `0x12414` | `0x124a0` | 46 | — (indefinite) |
| 3 | EventFlag | `0x119d4` | `0x11a14` | 20 | flag **#2**, bits `0x3`, mode `0x11` (AND+clear) |
| 4 | Sleep | `0x3a580` | `0x3a614` | 104 | — |
| 5 | Sleep | `0x45528` | `0x455ac` | 46 | — |
| 6 | Sleep | `0x455ec` | `0x45670` | 46 | — |

Entries `0x1039c`/`0x119d4`/`0x12414` sit inside module **1** (`base=0x10000`, `entry=0x10634`), the
only module of the five that starts two threads (`start=2`); `0x3a580` is module 4 (`base=0x3a300`),
`0x45528/0x455ec` module 5. Those threads only run again if **their event flags are set** or they are
woken by kernel code — and nothing in a run sets flag #1 or #2.

## Why nothing can wake them (code, not theory)

- `sceSifSetDma` (`ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp:1143`) reads the guest's descriptors, copies
  each payload into IOP RAM (`runtime->writeIopMemory`) and calls
  `PS2IopTransport::notifyTransfer(...)`.
- That lands in `IopRpcBridge::onSifTransfer` (`ps2xIOP/src/emulator/services/iop_rpc.cpp:373`), whose
  whole body is **`(void)transfer;`** — a deliberate no-op with a comment explaining that the EE
  transport owns the memory movement.
- **No EE→IOP interrupt is ever raised.** `IopIntrman::dispatchInterrupt` has exactly one caller in the
  tree: `iop_emulator.cpp:536`, which services **IOP-internal** DMA completions (`schedulePendingDma`).
  There is no SIF cause, no `INTC` raise, no `SetEventFlag` on the EE→IOP path.

So on hardware the EE's SIF1 post raises an IOP interrupt, the loaded IRX's own handler runs and sets
the flag its worker thread waits on. Here the payload is quietly teleported into IOP RAM and the
threads waiting for it stay asleep forever. **This is the LLE gap W277 named, now measured to the
thread and to the object it waits on.**

## The "two regimes" in W279, re-measured

W279 compared two runs and read the difference as a clock story. It is not. The two runs differ in how
far the **EE** got, and everything IOP-side follows from that:

| | 10-09 09:25 (`boot_w277r30u.log`) | today (`boot_desk092339.log`) |
|---|---|---|
| IOP instructions | 97,529,049 | 1,892 |
| IOP modules | 26 | 5 |
| **EE threads** | **count=10** (`CreateThread` 11, `StartThread` 9) | **count=1** (`CreateThread` 2, `StartThread` **0**) |
| EE halt | blocked inside syscall `0x22` (StartThread) at `0x005608e0` | `wallclock_deadline`, spinning at `0x00580dd8` (84 % of PC samples) |
| IOP SIF DMAs posted | 94 | 36 |

`0x00580dd8` is real engine code (oracle disasm: `addiu v0,-1` / `bne v0,v1,->0x00580DD8` — a
countdown loop). The current build is stuck **earlier** than the 10-09 build: the engine never starts
a single worker thread, so it never loads the IRX stack, so the IOP has nothing to do — 1,892
instructions are just the five boot modules' init.

**Consequence for the plan:** W279's step (1) is retired — there is no clock bug to fix. Step (2) and
(3) (reload the PDI modules, route SIF0) cannot be reached while the EE stalls at `0x00580dd8`.

## Oracle readings (law 13)

- Breakpoint set at `0x00580dd8` on the live oracle; **not hit yet** while the game runs at full speed
  (~300 M cycles/s, sampled PCs `0x005690b0`, `0x005691f0`, `0x005a4974`). Open item, not a finding:
  either real GT4 does not execute this PC at all (⇒ our path there is already a divergence) or it
  reaches it later than the sample window.
- Tool defect found and fixed: `tools/oracle/vg_oracle.py`'s `clearbps` called
  `clear_all_breakpoints`, which is **not** a DebugServer verb (the real one is `clear_breakpoints`,
  `DebugServer.cpp:843`), and there was no way to remove a single stale breakpoint. A leftover
  breakpoint silently eats every `resume` — the emulator re-breaks at the old address and `status`
  reads `paused: true`, which reads exactly like "the oracle never reaches the PC". Added `removebp`.

## Next brick

Raise the IOP's SIF interrupt on an EE→IOP transfer (spec: read PCSX2's `Sif1.cpp`/`Sif0.cpp` for the
channel state and the INTC cause, do not invent it), then re-run `VULCAN4_IOP_DIAG=2` and ask the
narrow question: **do threads 1 and 3 (event flags #1 and #2) wake?** That is a countable product
claim, and it is the first step of the LLE route W277 opened.
