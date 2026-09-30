# VU1-PLAN — what we do about the second wall

**Goal G3.0.** Same standard as [`docs/GS-PLAN.md`](GS-PLAN.md): what exists today, measured with
file and line evidence; what the guest asks of it; a chosen approach with rejected alternatives.

**Verdict up front: the VU1 is far less of a problem than the README claims, and the two things
blocking it are both small, both already located, and neither is an interpreter.** We have a real
VU1 interpreter with a full architectural state. It is not reachable by a guest, and it never
reports completion. That is the whole wall.

**This is a read-only dish. No runtime code was changed.**

---

## 1. What the runtime does about VU1 today — measured

### It is a real interpreter, not a stub

| Component | Location | Lines |
|---|---|---:|
| VU1 core (fetch/decode/execute) | `ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp` | 1,838 |
| VU1 lower unit (FPU/MINIU/ALU ops) | `ps2xRuntime/src/lib/vu/ps2_vu1_lower.cpp` | 789 |
| VU1 upper unit (load/store/transfer) | `ps2xRuntime/src/lib/vu/ps2_vu1_upper.cpp` | 506 |
| VIF1 DMA tag interpreter | `ps2xRuntime/src/lib/ps2_vif1_interpreter.cpp` | 783 |
| Public interface | `ps2xRuntime/include/runtime/ps2_vu1.h` | 299 |
| Tests | `ps2xTest/src/ps2_vu1_tests.cpp` | 1,716 |

**~4,100 lines of implementation and 1,716 lines of tests.** The state model is architecturally
complete — `VU1State` at `ps2_vu1.h:8-35` carries `vf[32][4]`, `vi[16]`, `acc[4]`, `q`, `p`, `i`,
`r`, `pc`, `mac`, `clip`, `status`, `cycles`, `ebit`, the D/T bit states, branch state, and the VIF
`TOP`/`ITOP` pair that XTOP/XITOP observe. `VU1Interpreter` supports **both** units
(`Unit::VU0`, `Unit::VU1`, `ps2_vu1.h:39-44`), so VU0-in-GS-mode is the same engine.

### The trigger path is wired

`ps2xRuntime/src/lib/ps2_runtime.cpp:670-706` installs two callbacks:

- `m_memory.setVu1MscalCallback(...)` — a guest write to **`VU1_MSCAL`** calls
  `m_vu1.execute(getVU1Code(), PS2_VU1_CODE_SIZE, getVU1Data(), PS2_VU1_DATA_SIZE, m_gs, &m_memory,
  startPC, top, itop, 65536)` (`:682`).
- `m_memory.setVu1MscntCallback(...)` — a guest write to **`VU1_MSCNT`** calls `m_vu1.resume(...)`
  (`:700`).
- Both read the **D-bit and T-bit from `vu0_fbrst` bits 10 and 11** (`:678-681`) and afterwards
  publish the result into `VPU_STAT` bits `0x0200`/`0x0400` (`:685-687`).

So the model is: microcode upload → MSCAL → run → status register. That is the right shape.

### The VIF1 DMA path is real and connected

I initially misread this and corrected it before writing it down: the VIF1 interpreter is **not**
dead code. `PS2Memory::processVIF1Data()` is declared at `ps2_vif1_interpreter.cpp:286` and is
called from the DMA engine — `ps2_memory.cpp:1837` (chain mode), `:1864` and `:1881` (direct/
scratchpad), and `:1112` (FIFO path). So a guest DMAC of VIF-tagged data **does** reach the
interpreter, which is the normal way VU1 data memory gets filled. It handles `UNPACK` including
"NUM is 8-bit and NUM==0 means 256 vectors" (`ps2_vif1_interpreter.cpp:545`).

### Defect 1 — VU1 memory is mapped at a fictitious address

```cpp
// ps2xRuntime/include/runtime/ps2_memory.h:42,43
constexpr uint32_t PS2_VU1_CODE_BASE = 0x11008000;
constexpr uint32_t PS2_VU1_DATA_BASE = 0x1100C000;
```

These are exposed as the host buffers `m_vu1Code` / `m_vu1Data` at
`ps2_memory.cpp:561-565`. **The real PS2 VU1 is not there.** VU1 Data Memory and Micro Memory live
in the `0x1D8xxxxx` physical range and are reached by the EE through the `0x04000000` expansion
region. `grep -rn "0x1D8\|0x1d8"` over the whole of `ps2xRuntime/` returns **nothing** — the real
range is not mapped, translated, or referenced anywhere.

Note the **sizes are right**: `PS2_VU1_CODE_SIZE = 16 KB`, `PS2_VU1_DATA_SIZE = 16 KB`
(`ps2_memory.h:45-46`), which is exactly the real VU1's 16 KB IMEM + 16 KB DMEM. So the hardware
model is right-sized and only the address is invented. The same applies to VU0
(`PS2_VU0_DATA_BASE = 0x11004000`, 4 KB + 4 KB — real VU0 is 4 KB + 4 KB, also correct).

**Consequence:** a guest that DMACs microcode to the true VU1 addresses writes to memory the VU1
does not own, and the interpreter then executes a buffer at `0x11008000` that nothing wrote.

### Defect 2 — the completion signal is never raised

`grep` for `vpu_stat` across `ps2xRuntime/src/lib` finds **only two writes**, both the same
D/T-status update at `ps2_runtime.cpp:685-687` and `:703-705`, both masking `0x0600`.

**`VPU_STAT.VBE` (bit 0) — "VU finished" — is never set anywhere in the runtime.** There is no
`vbe`, no `EBIT`-to-status path, and no other writer of bit 0.

This is precisely the `FindAddress` lesson from G1.5, one wall earlier. A guest that starts
microcode with MSCAL and then spins on `VPU_STAT & 1` will **spin forever**, and the boot report
will say `livelocked_in_syscall` or `spinning_in_guest_code` with nothing pointing at the VU1.

### What happens today if a guest writes microcode and kicks

Answering the question literally: the `MSCAL` write **does** reach the callback and the interpreter
**does** run, on an all-zero 16 KB buffer, because nothing can write the real address. The result is
no `EBIT`, no status change beyond D/T, and the guest waiting on a bit that never comes. **There is
no crash and no diagnostic** — the same failure shape as every other silent stub this project has had
to hunt down, which is why it is written down here rather than discovered later.

### Is VU0 macro mode actually supported?

`m_vu0` and `m_vu1` are the same class (`ps2_runtime.h:520-521`), so VU0 goes through the identical
engine with a different unit flag. **However**: the callbacks in `ps2_runtime.cpp:670-706` are
installed against `m_vu1` only. I found **no equivalent `MSCAL`/`MSCNT` callback for `m_vu0`** in
that file. So "VU0 macro mode is claimed supported" is **unverified** — the class is shared, but I
could not find the guest-facing trigger for VU0. It may exist elsewhere; I did not find it.

---

## 2. What GT4's data asks of it

**This section is mostly unverified, and I am labelling it as such rather than dressing it up.**

What I checked: `docs/DISC-MAP.md` and the executable `SCUS_973.28`. On the PS2, VU1 microcode is
**uploaded at runtime by the guest** into VU1 Micro Memory via DMA, not shipped as a disc blob —
that is the normal pattern for titles that use the vector unit, and it is the pattern that makes
"translate microcode ahead of time" hard. **unverified for GT4 specifically.**

Static evidence I could get: I scanned the executable's `.text` for `lui` instructions carrying
immediate `0x1d8` (the real VU1 range) and found **one** byte match, at vaddr `0x01002F03`, word
`0xa2FFD801` — which is **not** an `lui` (opcode `0x3C..`), it is data. So that scan is
**inconclusive**: it neither confirms nor denies that GT4 touches the real VU1 range. A scan for
`lui 0x0400` (the EE expansion region) found 2,385 matches, but `0x0400` is an extremely common
immediate and that number proves nothing on its own. **Do not treat either as evidence.**

What would settle it, cheaply, later: grep the guest's *store* instructions for the VU1 MSCAL
register address (`0x1D801C00`-ish) and follow the DMAC chain, or simply let the guest run far
enough to touch the VU1 and read `total_mmio_addresses`. **That is the right moment to answer this
question — when the guest is actually doing it — not now.**

What is *not* in doubt: GT4 is a 3D racing game that renders through the GS with per-vertex
transforms. If it uses the VU1, it will use it for vertex/lighting maths, and it will use the
**D-bit** handshake — a VU1 program parks itself in a spin loop with the D-bit set and waits for VIF
DMA to deliver the next packet. That handshake is already modelled (`dBitEnabled`,
`stoppedByD`, and the `0x0200` status bit), which is a point in this runtime's favour.

---

## 3. The approach

### Option A — interpret the microcode *(what the runtime already does)*
- **Already built.** 4,100 lines, 1,716 lines of tests, full architectural state.
- Correct, and the only option that survives runtime uploads.
- **Cost: near zero. This is the chosen path.**

### Option B — translate microcode ahead of time
- Fast, and would produce C++ like the rest of the project.
- **Rejected for now:** the microcode is uploaded *at runtime* by the guest, so it does not exist at
  build time. This is exactly the PS1 overlay problem described in `docs/PRIOR-ART.md` §5.
- Would only become viable behind a capture-and-compile cache (as `mstan/psxrecomp` does for PS1
  overlays), which is a large project and not a first move.

### Option C — special-case known microcode patterns
- **Rejected.** It is only honest if labelled as a game-specific hack, and GT4's VU1 usage is
  **unverified** — we do not yet know it even uses the VU1. Writing patterns for microcode we have
  never seen is speculation.

### Option D — borrow algorithms (not code) from a reference
- **Worth doing, licence-cleared first.** For the GS we settled this with
  `docs/PRIOR-ART.md`, and the finding generalises: **no shipped recomp links an emulator's core.**
  References worth reading, from sources already cited in that document: **PCSX2** (GPL-3.0-or-later,
  verified in G2.0 — the task brief's "LGPL-3.0" was wrong), **Ares** (used by Zelda64Recomp for
  "RSP vector instruction reference implementations"), and **Xenia** (which XenonRecomp credits for
  its PPC translator).
- **Obligation if we ever copy GPL-3.0 code:** carry the notices, state the change, offer
  corresponding source. Compatible with us, so permitted — but it is an obligation, not a freebie.
  **We are GPL-3.0; psxrecomp is PolyForm Noncommercial and must never be copied from at all.**
- **Not needed yet.** The interpreter exists; the problem is reachability, not understanding.

### Decision

**Option A, and the work is not "write a VU1". The work is: map the memory, raise the bit.** In
order:

1. **Map the real VU1 range.** Add `0x1D8xxxxx` to the memory map, pointing at the existing
   `m_vu1Code` / `m_vu1Data` buffers. This is a mapping change, not a new subsystem.
2. **Raise `VPU_STAT` bit 0 (VBE)** when a run completes or stops on D/T, alongside the D/T bits
   already written.
3. **Add a loud diagnostic** on the MSCAL path reporting the microcode the interpreter is about to
   execute, so a guest that kicks a zero buffer says so instead of hanging.
4. Only then, run the guest and find out what GT4 actually does with it.

**What would change my mind:** if the guest never touches the VU1 range at all, option A is
sufficient and the wall is smaller than advertised. If it turns out GT4 uploads microcode we must
*compile* for performance reasons on the Odin, option B's cache becomes worth revisiting — but only
after A is correct, because A is a prerequisite for observing the problem at all.

---

## 4. The interface the rest of the engine needs

Explicit, so the next dish implements against it rather than inferring it:

| Step | Guest action | Runtime must provide | Status |
|---|---|---|---|
| 1 | DMA microcode to VU1 IMEM | write path to `m_vu1Code` at the **real** address | ❌ **wrong address** |
| 2 | DMA vertex/param data via VIF | `processVIF1Data` → `m_vu1Data` | ✅ wired |
| 3 | Write `VU1_MSCAL` (start, PC, TOP, ITOP) | callback → `execute()` | ✅ wired |
| 4 | VU1 runs, parks on D-bit | `dBitEnabled` from `fbrst` bit 10, `stoppedByD` | ✅ modelled |
| 5 | VIF delivers, D-bit clears, run resumes | `VU1_MSCNT` → `resume()` | ✅ wired |
| 6 | **Guest polls `VPU_STAT` bit 0 (VBE) to finish** | **raise VBE** | ❌ **never set** |

**The thing that will hang the boot, named:** step 6. The guest launches microcode and waits on
`VPU_STAT.VBE`. If that bit is never raised, the guest spins — and the report will look exactly like
the G1.5 `FindAddress` livelock, which cost this project a whole dish of chasing a dispatch bug
that did not exist. **Do not let a stub lie about this bit.** A VU1 that is not implemented must
set VBE and log loudly, not silently never finish.

---

## 5. Licence position

- We are **GPL-3.0**; `ps2xRuntime` is GPL-3.0.
- **PCSX2 is GPL-3.0-or-later** (verified in G2.0; the G2.0 brief's "LGPL-3.0" was wrong).
  Compatible, so copying is permitted with notice + change-state + corresponding source.
- **Ares** — used as an RSP reference by Zelda64Recomp. **Licence unverified** in this dish; check
  before relying on it.
- **`mstan/psxrecomp` is PolyForm Noncommercial 1.0.0** — **incompatible with our GPL-3.0 tree.
  Read for ideas; never copy code from it.**

## 6. Honest summary

The README calls VU1 the hard wall. The measurement says otherwise: the interpreter is real,
architecturally complete and tested, the DMA path is connected, and the D/T handshake is modelled.
**Two small changes stand between us and a VU1 a guest can actually reach** — one wrong address
constant and one bit that is never set. The risk here is not the vector unit. It is that we spend a
dish chasing a *spurious* wall, and that when the guest does reach it, the failure looks like
something else entirely unless step 6 is honest.
