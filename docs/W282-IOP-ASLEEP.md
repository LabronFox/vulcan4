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

## The EE side of the same wall: a CD-client retry loop (measured, same day)

`VULCAN4_PC_PROBE=0x580dd8` used to produce **zero** hits in a run whose own PC histogram put 84 % of
its samples at exactly that address. Cause: a recompiled `bne` back-edge is a direct `goto`, not a
`dispatchGuestBranch`, and the 24-inline-probe passed to the harness's `pcProbeCheck` was only
consulted from the guest loop — a loop head never comes back there. W280's note ("the probe anchors on
function entries") was the same defect firing on the SIF call site.

Fixed in this brick: the runtime has a second observer (`PS2PcObserver`, `ps2_runtime.h`) which
`PS2Runtime::eeCheckpointDue()` calls — the one place a recompiled back-edge is guaranteed to reach —
and the harness installs it only when `VULCAN4_PC_PROBE` is set. Unset = one null check per checkpoint
and otherwise byte-for-byte the previous path.

With both anchors live, `run/boot_w282pc2.log` gives our guest state in the loop:

```
[pcprobe] hit=1  pc=0x00580dd8 ra=0x00580dac v0=0x000ff821 v1=0xffffffff a0=0x00000020
                 a1=0xffffffff a2=0x00000001 a3=0x00000040 s0=0x00870000 s1=0x00874fa8 t0=0x20887240
[pcprobe] hit=24 pc=0x00580dd8 ra=0x00580dac v0=0x000f4021 …   (v0 counts down by 0x800 per sample)
```

Reading, from the oracle's own disassembly of that region (`0x00580dcc: lui v0,0x0010; li v1,-1;
addiu v0,-1; bne v0,v1,->0x00580dd8`):

- the guest is burning a **one-million-iteration delay** (≈2 M EE cycles) inside a routine whose
  caller sits at `0x00580dac`, itself reached from `jal 0x005B17D0` with `a1 = 0x80000592`;
- `s1 = 0x00874FA8` is **the game's own CD/SIF client** (the same address W276 R20 names as "the real CD
  client (0x00874FA8)"), and `t0 = 0x20887240` a cached alias of `0x00887240`.

So the EE is not mysteriously spinning: it is **retrying the CDVD side with a delay between attempts**,
waiting on a reply from the IOP — while the IOP is asleep (section above). The two measurements are one
cause: no IOP wake ⇒ no reply ⇒ the EE retries forever.

## Side finding — the MENU GATE false-passed on a blank capture (fixed the same day)

`bash .auto/verify-menu.sh` (the project's "is the screen still the ©2005 disclaimer?" gate, and now the
**stop condition of the daily wall campaign**) exited **0 — GATE PASS** on
`run/w281-capture.png`: a **1 quantized colour**, max-brightness **14/255** frame, i.e. the near-black
*g ghost* of the window that W278 was written about. It passed purely because a uniform frame differs
perceptually from a text screen (diff 14.81 against a threshold of 6), and the v5 structural test needs
*low colour count AND a non-black fraction within 0.05 of the reference* — 0.1111 vs 0.1831 misses that
by 0.022.

Fixed as **v6 DEGENERACY FLOOR** in `.auto/verify-menu.sh`: `<= 24` quantized colours **and** max
brightness `<= 32` now **FAILS** with an explicit message (a blank/ghost frame is not a screen, so it
can be neither the disclaimer nor the menu); 24–999 colours still passes with a human-look note against
the 1000-colour bar this project uses for a drawing claim. Re-run on the same capture: `exit=1`,
"GATE FAIL: DEGENERATE FRAME …".

Same class of bug as gate versions v1–v3 (gating a proxy instead of the deliverable), and it mattered
here for a specific reason: wired into an automated campaign as the stop condition, a false pass would
have **stopped the campaign on nothing**. `.auto/` is gitignored, so the fix lives on disk; this note is
its tracked record.

## Oracle readings (law 13)

- Breakpoint at `0x00580dd8` on a **freshly relaunched oracle** (paused at `0x01000008`, the loader
  entry, with the breakpoint armed *before* the resume). **Not hit in 20 minutes / 120 samples**
  (`run/oracle-bp-580dd8.log`, 20:22:05 → 20:41:58). The emulator is genuinely running, not stalled:
  the EE cycle counter wraps at 2^32 (max sample 4,274,507,922, 82 decreases in 119 intervals — a
  **wrapping 32-bit counter**, not a reset), and the PC histogram is 62/120 in BIOS/kernel code
  `0x00081FC0` with the rest spread over the engine's dispatcher region (`0x00563EA0`, `0x005A47B0`,
  `0x00568460` ≈ the 10-09 build's `0x005608E0`).
- **Read this as a divergence, with its caveat stated:** with the breakpoint armed from instruction 0,
  20 minutes of real GT4 never executes the delay loop our EE sits in 84 % of the time. The honest
  reading is that hardware gets the CDVD reply and therefore never enters the retry path — our EE
  retries precisely because the IOP never answers. The caveat: the sampler cannot prove "never" for a
  code path only reached in a phase this run did not enter (e.g. mid-race disc streaming), so the wall
  rests on the IOP measurement above, not on this negative.
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
