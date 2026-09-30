# FIRST BOOT — GT4's recompiled code executes

**Goal:** G1.1 — the dish where VULCAN 4 stops being analysis and becomes a program.

Nobody has made a PS2 game *execute* under recompilation. This dish did it, for three guest
functions, and then stopped and said why. **That is a pass.**

---

## 1. THE RESULT

```
VULCAN4 BOOT REPORT functions_entered=3 halt=wallclock_deadline bios_files=0
VULCAN4 HARNESS detail=guest was still inside a function when the deadline fired; the
watchdog asked the guest to yield pc=0x0102871c distinct_pcs=3 dispatcher_transfers=1
elapsed_ms=40068 entry_budget=2000000 spin_limit=1000000 deadline_s=40
```

Read it straight, with no spin:

- **GT4's own code ran.** Three guest functions were entered, starting at the real ELF entry point
  `0x01000008`, and real MIPS from `SCUS_973.28` was translated and executed natively on x86-64.
- **It stopped in a real, named, understood place** — not a crash, not a silent stub.
- **`bios_files=0`.** No console BIOS was searched, loaded, or required. The harness contains no
  BIOS loading path at all.

Full log: `/mnt/ssd/vulcan4-build/run/boot.log` (15,360 B). Harness:
`/mnt/ssd/vulcan4-build/run/vulcan4_harness` (11.8 MB). Source in this repo:
`tools/harness/vulcan4_harness.cpp`.

---

## 2. THE HARNESS

`tools/harness/vulcan4_harness.cpp`, built against the runtime from G0.1 and the generated
translation unit from G0.4. Roughly 500 lines. It is **our** code — not a modification of the
recompiler and not a modification of the ELF parser.

### Loading the image the way the console does

Not a hand-copied pretend layout. The runtime's own `loadELF()` walks the ELF's **3 program
headers**, keeps `PT_LOAD`, translates the virtual address, copies `filesz` bytes and zero-fills
the rest of `memsz`. The harness re-reads the headers independently and prints them, so the log
carries evidence rather than an assurance:

```
VULCAN4 ELF LAYOUT ei_data=1 (1=ELFDATA2LSB) entry=0x01000008 program_headers=3 bios_files=0
VULCAN4 ELF SEGMENT 0 type=0x70000000 (not PT_LOAD, not loaded into RDRAM)
VULCAN4 ELF SEGMENT 1 PT_LOAD vaddr=0x01000000 filesz=0x0002dc54 memsz=0x0002dc54 flags=0x1000 bss_zeroed=0
VULCAN4 ELF SEGMENT 2 PT_LOAD vaddr=0x0102dc80 filesz=0x00013aa4 memsz=0x00023d2c flags=0x2ec80 bss_zeroed=0x00010288
VULCAN4 ELF LAYOUT entry_point_confirmed=0x01000008
```

`ei_data=1` is `ELFDATA2LSB` — the PS2 is little-endian, independently confirming the correction
in `docs/TOOLCHAIN.md` §5a, this time from the runtime rather than from `readelf`. The runtime
refuses anything else: `if (header.elf_class != 1u || header.endianness != 1u) { ... }`.

Segment 2's `bss_zeroed=0x00010288` is the `.bss` the console would hand the game zeroed.

### Starting at the real entry point

`loadELF` sets `ctx.pc = header.entry` — `0x0100008` — and the harness confirmed it printed the
same value it read independently. The console's stack is supplied: `$sp` near the top of RDRAM
(`0x01FFFFF0`, i.e. `PS2_RAM_SIZE - 0x10`), `$a0`/`$a1` zeroed, the IOP and SIF state reset and
`initializeEeKernelState()` run — everything `PS2Runtime::run()` does, minus the render loop.

### Wiring the generated code to the runtime

`register_functions.cpp` (from G0.4) fills a **dense global table** indexed by
`(address - 0x01000008) >> 2`, 46,841 slots covering `[0x01000008, 0x0102DBEC)`. The generated
functions run *inline* on the host and **return** whenever they hit a guest control transfer,
leaving the next PC in `ctx->pc`. So the driver is:

```cpp
while (budget remains) {
    fn = g_ps2RecompiledFunctionTable[(ctx->pc - base) >> 2];
    if (!fn) { name it, stop; }
    fn(rdram, &ctx, &runtime);   // runs until it yields
}
```

Every iteration is one entry into a guest function, and that is exactly what `functions_entered`
counts. It is a real counter, incremented at the call site, not a constant.

### One runtime mechanism worth knowing about

`ps2xRuntime` implements the EE's cooperative threads with C++ exceptions. `EeDispatcherTransfer`
is documented in `ps2xRuntime/include/runtime/ee_scheduler.h` as:

> *"This exception is the EE equivalent of a longjmp to the dispatcher. It is not an error and
> must only be caught at EeScheduler::run()."*

Upstream's `EeScheduler::run()` is the dispatcher. In this harness **the loop above is**. Catching
it and re-entering at `ctx->pc` is the faithful translation, not a swallowed error. Without that
catch the process aborts with `terminate called after throwing an instance of 'EeDispatcherTransfer'`
— which is exactly what happened before the catch was added. Transfers are counted and reported
(`dispatcher_transfers=1`) so their volume is visible rather than hidden.

---

## 3. STOP CONDITIONS — decided up front, as required

| Condition | Token | Default | Why it exists |
|---|---|---|---|
| Entry budget | `entry_budget_exhausted` | 2,000,000 entries | bounds a guest churning through functions |
| Spin detector | `spin_trap` | 1,000,000 returns to one PC | GT4 uses a deliberate infinite NOP loop as a failure trap |
| Wall clock | `wallclock_deadline` | 40–120 s | bounds everything else |
| PC outside the table | `pc_outside_generated_table` | — | a transfer we cannot even name |
| No generated body | `missing_function` | — | **the important one: a named stop** |
| Runtime stop | `returned_to_entry` | — | clean finish |

**And the one that actually mattered.** All three of the budget-style limits are checked *between*
function entries. A guest spinning **inside** one generated function never comes back, so none of
them can fire. Measured: a run with all of them armed still produced no report line for 300 s.

The fix uses a public runtime API rather than a signal handler. Generated code emits
`runtime->eeCheckpointDue()` on loop back-edges, and `PS2Runtime::requestStop()` is public and sets
the flag that makes `eeCheckpointDue()` return true. So a watchdog thread calls `requestStop()` at
the deadline, the spinning guest yields, control returns to the loop, and the stop is **detected
and reported as a reason** instead of the process being killed. That is what makes
`halt=wallclock_deadline` a real result rather than a timeout.

*Correction to a hypothesis I started with:* I assumed the recompiler only yields on
*unconditional* back-edges. **It does not** — the conditional `bne` at `0x01028740` in
`ps2_recompiled_functions.cpp:187306` has the same `eeCheckpointDue()` guard. The stall is
genuinely inside `sub_01028638`, and no yield point on the path back to the dispatcher was reached.

---

## 4. WHERE IT GOT, AND WHY IT STOPPED

The trace, with register state (`VULCAN4_TRACE_ALL=1`):

```
VULCAN4 TRACE entry=1 pc=0x01000008 sp=0x01fffff0 ra=0x00000000
VULCAN4 TRACE entry=2 pc=0x01028640 sp=0x01ffff70 ra=0x010286dc
VULCAN4 TRACE entry=3 pc=0x010286dc sp=0x01ffff70 ra=0x010286dc
```

That is the whole of it: **three entries, ever.** The guest never returned control a fourth time.

### The path it took

1. **`0x01000008`** — the real entry point. The `padduw $zero,$zero` register-init preamble, then
   real work. This is the function read instruction-by-instruction in `docs/FUNCTION-ANATOMY.md`.
2. **`0x01028640`** — called from the entry block.
3. **`0x010286DC`** — a memory-block grow loop. It calls `0x01028638` repeatedly, advancing two
   pointers toward each other:

   ```mips
   1028714:  jal   0x1028638          ; grow the block
   102871c:  move  s3,v0
   1028740:  bne  s1,s0,0x1028708     ; keep going until they meet
   1028744:  sltu v0,s1,s0
   ```

   `0x01028638` **is** recompiled and present in the table
   (`g_ps2RecompiledFunctionTable[41356] = sub_01028638_0x1028638`). So this is not a missing
   function. The call is real, the callee is real, and the loop simply never converges.

### THE FIRST NAMED BLOCKER: `FindAddress` walks the PS2's address aliases

`sub_01028638` calls the SCE kernel address-search syscall, handled by
`ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp:791` as `FindAddress`. The runtime narrates it on
stderr, and this is the real blocker in the log:

```
[FindAddress:hit] pc=0x1028640 start=0x80000000 end=0x80080000 target=0x10285c0 result=0x800120e8 scannedWords=131072
[FindAddress:miss] pc=0x1028640 start=0x800120ec end=0x80080000 target=0x10285c0 result=0x0   scannedWords=112581
[FindAddress:hit] pc=0x1028640 start=0x4         end=0x80080000 target=0x10285f8 result=0x1218c  scannedWords=537001983
[FindAddress:hit] pc=0x1028640 start=0x12190     end=0x80080000 target=0x10285f8 result=0x1035354
[FindAddress:hit] pc=0x1028640 start=0x2012190   end=0x80080000 target=0x10285f8 result=0x3035354
[FindAddress:hit] pc=0x1028640 start=0x4012190   end=0x80080000 target=0x10285f8 result=0x5035354
...
```

`scannedWords=537001983` is **half a gigabyte scanned per call**, and look at what it keeps
finding: `0x001218C`, `0x01035354`, `0x0201218C`, `0x03035354`, `0x0401218C`, `0x05035354` — the
**same physical data at a `0x20000000` stride**, i.e. the PS2's KSEG0/KSEG1 address aliases.

`FindAddress` normalises the *target* with `normalizeKernelAlias()`, but its **search window still
covers `0x00000000`–`0x80000000`**, so every alias of the same bytes looks like a fresh match. The
guest's search loop advances `start` past each hit, immediately finds the *same data again* at the
next alias, and never terminates.

**Named, falsifiable statement:** the first hardware-path blocker on the way to boot is
**`FindAddress` (SCE kernel address search) treating PS2 address aliases as distinct matches**, so
a guest loop that searches a kernel pointer table never converges. GT4 spends its entire
first-boot budget here.

**What would falsify it:** if `FindAddress` returned only the *first* physical match and the guest
proceeded past `0x01028638`, the claim is wrong. **What would confirm it:** making the scan skip
aliased windows (search the physical range once) and watching the guest leave that loop. That is
the next dish's work, and it is a runtime change, not a recompiler change.

### The guest call list

**Unsatisfied calls: none.** `missing_functions=0` — every guest control transfer GT4 made in this
run landed on a recompiled function. Nothing was stubbed and nothing was skipped.

The runtime *did* serve guest services, from its own log:

```
[SetupHeap] base=0x10519ac alignedBase=0x10519b0 size=0xffffffff runtimeBase=0x10519b0 runtimeEnd=0x10519b0
```

GT4 asked for the console's heap (SCE syscall `0x3D`, `SetupHeap`) with size `0xFFFFFFFF`, meaning
"the rest of RAM", and got one: base `0x010519B0`, limit `0x01F00000`, with `.bss` already zeroed.
`runtimeEnd == runtimeBase` is correct for a *fresh* heap — the bump pointer has not moved yet. I
initially read that as a broken heap and was wrong.

So the no-BIOS backlog from this run is:

| Guest asked for | Served? |
|---|---|
| heap setup (syscall `0x3D`) | ✅ base `0x010519B0`, limit `0x01F00000` |
| kernel pointer search (`FindAddress`) | ⚠️ **wrong answer** — alias duplicates, never converges |
| anything past the heap call | never reached |

---

## 5. NO BIOS — the captain's item, measured

```
VULCAN4 HARNESS bios_policy=none (no console BIOS is searched, loaded or required)
VULCAN4 BIOS none_required=true files_opened=0 (the harness contains no BIOS loading path)
```

`bios_files=0` in the report line is a **measured count, not an assurance**. The harness holds a
`biosFilesOpened` counter that only a BIOS open would increment, and **there is no such call
anywhere in the file** — `grep` for a BIOS path search in `vulcan4_harness.cpp` finds nothing but
the policy line and the counter. The gate requires this to be 0 and it is 0.

**This is not yet goal G1.0.** G1.0 is *GT4 running to a milestone with every console-OS call
served by us and named*. We proved the negative half — nothing needs a BIOS to get three functions
in — but the game has not run far enough to demand one, so there is no evidence yet about what it
would ask for. G1.0 remains open and honestly so.

---

## 6. BUILD AND RUN

```bash
# prerequisites: G0.1 toolchain, G0.4 generated translation unit
R=/home/or/vulcan4/tools/PS2Recomp
B=/mnt/ssd/vulcan4-build
G=$B/recomp
INC="-I$G -I$R/ps2xRuntime/include -I$R/ps2xRecomp/include -I$R/ps2xRuntime/src/lib/Kernel -I$R/ps2xIOP/include"
DEFS="-DPS2_RUNTIME_LOGS=1 -DAGRESSIVE_LOGS=1 -DPS2_FUNCTION_LOG_TRACKER=1 -DPS2X_ENABLE_IOP_RPC_TRACE=1 -DPS2X_HAS_FFMPEG=1"
mkdir -p $B/run && cd $B/run

g++ -std=c++20 -O1 -msse4.1 $INC $DEFS -c /home/or/vulcan4/tools/harness/vulcan4_harness.cpp -o harness.o
g++ -std=c++20 -O1 -msse4.1 $INC $DEFS -c $G/register_functions.cpp -o register_functions.o
g++ -std=c++20 -O0 -msse4.1 $INC $DEFS -c $G/ps2_recompiled_functions.cpp -o ps2_recompiled_functions.o

FFMPEG=$(pkg-config --libs libavcodec libavformat libavutil libswresample libswscale)
g++ -o vulcan4_harness harness.o register_functions.o ps2_recompiled_functions.o \
  $B/ps2xRuntime/libps2_runtime.a $B/ps2xIOP/libps2_iop.a \
  $B/_deps/raylib-build/raylib/libraylib.a $FFMPEG -lpthread -ldl -lm -lrt -lX11

# run it. Xvfb because PS2Runtime::initialize() opens a raylib window, and this box is headless.
xvfb-run -a -s "-screen 0 640x480x24" ./vulcan4_harness \
  /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml 2000000 40 \
  > /mnt/ssd/vulcan4-build/run/boot.log 2>&1
```

`vulcan4_harness <guest.elf> [sdk_names.toml] [max_entries] [max_seconds]`. Set `VULCAN4_TRACE=N`
to print the first N entries, or `VULCAN4_TRACE_ALL=1` for every entry — which is how the stall
was located rather than guessed at.

**Why Xvfb:** `PS2Runtime::initialize()` brings up the memory model and binds the core subsystems
(both required) and then calls raylib's `InitWindow()`. On a headless box GLFW fails and the
process segfaults — measured in G0.1 as Finding 4. A virtual framebuffer gives raylib a display
without changing the runtime. Software GL (`swrast_dri.so`) is already present.

---

## 7. G1.1 VERDICT

| Requirement | Status |
|---|---|
| Harness loads the image via program headers | ✅ 3 headers, both `PT_LOAD` segments, `.bss` zeroed |
| Starts at the real entry point | ✅ `0x01000008`, confirmed twice independently |
| Executes real GT4 code | ✅ **3 guest functions**, 46,852-instruction translation unit |
| Machine-readable report line | ✅ `functions_entered=3 halt=wallclock_deadline bios_files=0` |
| Names where it stopped | ✅ `FindAddress` alias bug, `pc=0x0102871c`, named in §4 |
| Guest call list | ✅ zero unsatisfied; heap served; `FindAddress` wrong |
| Stop conditions decided and documented | ✅ §3, three independent, plus the watchdog |
| `bios_files=0` | ✅ measured, and there is no BIOS path in the harness to increment it |
| Nothing silent, nothing faked | ✅ including the hypotheses I had to retract |

**G1.1 is met.** The project's headline claim now has a run behind it: *GT4's own machine code,
translated to C++, executes natively on x86-64, with no BIOS.*

### Honest limits of that claim

- **Three functions.** Not a boot, not a menu, not a frame. The guest spent its entire budget
  inside one non-terminating search loop.
- **The translation unit has never been exercised beyond these three functions.** 721 recompiled
  bodies exist; 3 ran. 82 are stubs, not translations. 122 were promoted to runtime-dispatched
  fallbacks (13,768 entries) and have not been reached.
- **Nothing has been verified as visually or behaviourally correct.** A function that ran is not a
  game that works.

### Carried forward, in order

1. **`FindAddress` alias handling** — the first blocker, and the cheapest one to test. It is
   blocking everything behind it.
2. **The 197 SCE library functions with no runtime handler** (`docs/DISC-MAP.md` §4) — the no-BIOS
   backlog proper. Only one syscall has been demanded so far; there will be many more.
3. **The grow loop at `0x010286DC`** will need re-examination once `FindAddress` behaves, since
   they are almost certainly the same bug.
4. **A yield point inside `sub_01028638`.** Even with the watchdog this cost a wall-clock deadline.
   A path that returns to the dispatcher would make stalls cheap to detect and stop.

### Licence note

This document contains measurements, addresses, disassembly excerpts and one runtime-behaviour
diagnosis about a disc the user owns. **No game code is reproduced here beyond short disassembly
excerpts used as analysis**, and the generated C++ stays on the SSD. Not affiliated with Sony
Interactive Entertainment or Polyphony Digital.

---

## 8. G1.2 ADDENDUM — naming the wall

> Appended, not rewritten. §1–§7 above are the G1.1 record, including its `wallclock_deadline`
> and its unanswered question. This section is the answer.

### The result

```
VULCAN4 BOOT REPORT functions_entered=3 halt=stuck_in_syscall bios_files=0
VULCAN4 HARNESS detail=blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x0102871c
  -- this syscall is the wall pc=0x0102871c distinct_pcs=3 dispatcher_transfers=1
  elapsed_ms=200055 entry_budget=20000000 spin_limit=1000000 deadline_s=200
  VULCAN4 CALLKIND syscalls=5 total_syscall_calls=317 distinct_mmio_addresses=0
        total_mmio_accesses=0 missing_functions=0
  0x83 sce_FindAddress calls=311 last_pc=0x01028640
  0x74 sce_SetSyscall  calls=2   last_pc=0x01028788
  0x40 sce_CreateSema  calls=2   last_pc=0x0101f428
  0x3d sce_SetupHeap   calls=1   last_pc=0x010001e8
  0x3c sce_SetupThread calls=1   last_pc=0x010001cc
VULCAN4 BIOS none_required=true files_opened=0
```

`halt` moved from `wallclock_deadline` to `stuck_in_syscall`, and the call list went from empty
to five named syscalls.

### The question, answered in one sentence

> **The guest is waiting on the console's kernel address-search syscall, `FindAddress`
> (SCE syscall `0x83`), which it calls from PC `0x0102863C` in a loop that repeats a search it
> can no longer satisfy — and it never touches hardware at all.**

### It is not hardware. That is measured, not assumed

```
distinct_mmio_addresses=0  total_mmio_accesses=0
```

Counted in `PS2Memory::translateAddress()`, which every guest read and write passes through.
**Zero.** The guest never read or wrote a single console register, DMA channel, timer or vblank
flag. So the classic "spinning on a hardware bit we don't emulate" explanation is **ruled out by
measurement**, and so are the GS and VU1 walls — it never got near them.

### It is `FindAddress`, and here is the instruction

The guest PC that keeps recurring is `0x01028640`; the instruction *issuing* the syscall is the
one before it, in `sub_01028638`:

```mips
1028638:  24030083  addiu $v1,$zero,0x83    ; $v1 = 0x83 = the syscall number
102863c:  0000000c  syscall                  ; <-- the wall
1028640:  03e00008  jr   $ra                ; resume point
```

and in the generated C++ it is literally `runtime->handleSyscall(rdram, ctx, 0x0u)`, which the
runtime resolves to `$v1` = `0x83` = `FindAddress` in the dispatcher's switch
(`ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp:306`).

The guest's loop, at `0x010286DC`:

```mips
1028714:  jal  0x1028638          ; FindAddress wrapper
102871c:  move s3,v0
1028740:  bne  s1,s0,0x1028708     ; keep growing until the two pointers meet
```

### Why it never finishes: it is repeating a search that can only miss

`FindAddress` takes a guest range and a target value and returns where that value is stored. The
runtime narrates every call; the run's tally is:

| Outcome | Count |
|---|---:|
| `FindAddress:hit` | 16 |
| `FindAddress:miss` | 103 |

The 16 hits were at guest addresses `0x001218C` and `0x01035354` — both in the low 32 MB heap the
guest had **written itself**. The guest advances `start` past each hit, searches again, and after
the last real occurrence every call is identical:

```
start=0x01035358 end=0x80080000 target=0x010285f8 result=0x0
```

103 times. **It is searching for the value `0x010285F8` — a pointer to one of its own functions —
in a range where that value does not exist, and retrying the same failing search forever.**

### A real bug found and fixed on the way: the search scanned 2 GB for 32 MB of RAM

Before blaming the guest, the scan itself was pathological. `ps2ResolveGuestPointer` folds
KSEG0 (`0x20000000`), KSEG1 (`0x80000000`) and the `0x40000000`–`0x80000000` window onto the same
**32 MB** of RDRAM, but `FindAddress` walked the caller's window literally. GT4 passes
`[0x00000004, 0x80080000)`, so one call re-read the same words ~64 times:

| | `scannedWords` per call |
|---|---:|
| before | **537,001,983** (≈2 GB) |
| after | **8,392,703** (≈32 MB) |

Exactly **64×**, the alias factor. And the practical effect, from two identical runs:

| | calls completed | wall clock |
|---|---:|---|
| before | 144, **still not converged** | 19 min, killed |
| after | 317, all completing | 200 s |

**Fix:** one high-water mark on the *physical* offset, so each distinct word is visited once, in
the same order, returning the same first match. `ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp`,
28 lines, with the reasoning in the comment. Patch:
`tools/patches/ps2recomp-linux-g1wall.patch`.

This fixed a real inefficiency but **did not unblock the boot** — because the wall is the *miss*,
not the cost of the scan. Both things are true, and saying only the first would have been
flattering.

### What the wall is NOT claiming

The guest is looking for a pointer that is not in memory. Two readings fit the evidence, and I am
not going to pick one without evidence:

- **(a) The data was never loaded.** The table that pointer belongs to may be populated from
  `CORE.GT4` (2 MB, 7.96 bits/byte, no ELF inside) or `GT4.VOL` (2.29 GiB, payload compressed by
  something non-standard) — both opaque since `docs/DISC-MAP.md` §5. We loaded only the ELF.
- **(b) The guest's own bookkeeping diverged** earlier, so it is scanning for something it
  believes it wrote but did not.

**Falsifier for the whole section:** if `0x010285F8` turns out to be present in memory once the
game's data files are loaded, reading (a) wins and the wall moves. If it is present in a
correctly-running guest's memory at this point, reading (b) wins and the divergence is ours to
find. Either way the next step is the same: **load what the guest is looking for, or find out why
it thinks it is there.**

### The no-BIOS backlog, populated

This is the list the next dish needs. Five syscalls, 317 calls, all named, all served, no BIOS:

| Syscall | Name | Calls | Last PC | Status |
|---|---|---:|---|---|
| `0x83` | `FindAddress` | 311 | `0x01028640` | ✅ served — **but returns "not found" 103 times** |
| `0x74` | `SetSyscall` | 2 | `0x01028788` | ✅ served |
| `0x40` | `CreateSema` | 2 | `0x0101F428` | ✅ served |
| `0x3D` | `SetupHeap` | 1 | `0x010001E8` | ✅ served — base `0x010519B0`, limit `0x01F00000` |
| `0x3C` | `SetupThread` | 1 | `0x010001CC` | ✅ served |

**Zero BIOS files.** Every one of these was served by our own runtime. The `0x83` result being
*wrong* is a different problem from *absent* — and it is the one that matters now.

### The instrumentation, since it will be needed again

The three things this dish was told to watch, and where they are counted:

| Thing | Counted in | Surface |
|---|---|---|
| syscalls (number, PC, count) | `PS2Runtime::handleSyscall` | `syscallCallCount()`, `lastSyscallId()`, `syscallCounts()` |
| which syscall is executing *now* | same, saved/restored around dispatch | `activeSyscallId()` — this is what turns a timeout into a name |
| MMIO (address, count) | `PS2Memory::translateAddress` | `mmioAccessCount()`, `mmioCounts()`, `lastMmioAddress()` |
| the yield / dispatch path | the harness driver loop | `functions_entered`, `distinct_pcs`, `dispatcher_transfers` |

Syscall **names** are not in the runtime — a second copy of the dispatch table there would drift.
They are generated from the dispatcher's own switch by
`tools/harness/gen_syscall_names.py` into `ps2xRuntime/include/runtime/syscall_names.h`
(58 names). Regenerate it if the dispatcher changes.

### How "it timed out" became a name

`PS2Runtime::requestStop()` is public and makes `eeCheckpointDue()` return true, so the watchdog
from G1.1 still causes the guest to yield. The difference is what the harness *does* with that:

```cpp
const uint32_t active = runtime.activeSyscallId();
if (active != PS2Runtime::kNoActiveSyscall) {
    haltReason = kHaltInSyscall;                 // "stuck_in_syscall"
    haltDetail = "... blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x0102871c ...";
} else {
    haltReason = kHaltSpinningInGuest;           // "spinning_in_guest_code"
}
```

Before: a clock ran out. After: the runtime says which syscall it is standing in. Same stop, two
very different meanings — and only the second one tells the next dish what to build.

### Honest limits

- **Still three functions.** The fix made the search 64× cheaper and the syscall completes 317
  times instead of hanging, but the guest's loop does not terminate, so the count is unchanged.
- **The two readings in the previous section are hypotheses.** I have not loaded `CORE.GT4` and
  I have not traced the guest's earlier bookkeeping.
- **0 MMIO accesses is a statement about this run only.** It rules out the hardware walls *at this
  point in the boot*, not for the whole game. The GS and VU1 walls are still exactly where
  `docs/DISC-MAP.md` said they were — ahead of us, not behind.

### Licence note

Measurements, addresses, short disassembly excerpts and a runtime-behaviour diagnosis, for a disc
the user owns. No game code beyond short excerpts used as analysis. Generated C++ and run logs
stay on the SSD.

---

## 9. G1.3 ADDENDUM — the syscall is served, and it still does not get past

> **The gate for this dish was `functions_entered >= 4`. It is still 3. This dish did not
> meet its gate, and the doc says so first rather than burying it.**
>
> What it did produce: the research the brief asked for, a real 64× fix, a second fix grounded
> in the game's own code, five new unit tests, one pre-existing test corrected, and a much more
> precisely named wall.

```
VULCAN4 BOOT REPORT functions_entered=3 halt=stuck_in_syscall bios_files=0
VULCAN4 HARNESS detail=blocked inside SCE syscall 0x83 (FindAddress), guest pc 0x0102871c
  VULCAN4 CALLKIND syscalls=5 total_syscall_calls=512 distinct_mmio_addresses=0
        total_mmio_accesses=0 missing_functions=0
```

### 1. The guest's syscall list, all five

| Syscall | Name | Calls | Last PC | Served? |
|---|---|---:|---|---|
| `0x3C` | `SetupThread` (RFU060) | 1 | `0x010001CC` | ✅ yes |
| `0x3D` | `SetupHeap` (RFU061) | 1 | `0x010001E8` | ✅ yes — base `0x010519B0`, limit `0x01F00000` |
| `0x40` | `CreateSema` | 2 | `0x0101F428` | ✅ yes |
| `0x74` | `SetSyscall` | 2 | `0x01028788` | ✅ yes |
| `0x83` | `FindAddress` | 506 | `0x01028640` | ✅ **dispatched and ran — but does not unblock the guest** |

**None of the five is unhandled.** The `0x83` problem was never "we don't serve it" — it was "we
serve it and the answer doesn't get the guest moving". That distinction is the whole finding.

### 2. Research: what `0x83` is, and from where

The brief asked for sources, not guesses. Here is what I actually found.

**The number is right.** ps2SDK defines it:

- <https://github.com/ps2dev/ps2sdk/blob/master/ee/kernel/include/syscallnr.h> —
  `#define __NR_FindAddress 0x83`
- <https://www.psdevwiki.com/ps2/EE_Syscalls> — same table
- <https://github.com/ran-j/PS2Recomp/issues/90> — an **upstream issue**, *"Missing 0x83 Syscall —
  Some games stop and reset if there is no implementation for the 0x83 syscall."* So this is a
  known upstream gap, and we are working in the same gap the maintainer knows about.

**The arguments and return value are genuinely undocumented.** The most complete public EE syscall
reference stops before it:

- <https://github.com/mlafeldt/ps2rd/blob/master/Documentation/technical/ee-syscalls.txt> —
  documents `0x3C` RFU060, `0x3D` RFU061 *"Sets up the heap. Arguments: heap_start, heap_size"*,
  `0x40 CreateSema`, `0x74 SetSyscall`, `0x7F GetMemorySize` — all consistent with what the runtime
  already did — then **stops at `0x7F`**. No `0x80`–`0x87`.

So `0x83`'s contract had to come from somewhere better than the internet. It came from
**Gran Turismo 4 itself**, which carries its own inlined copy of the identical algorithm at guest
`0x010285F8`:

```mips
10285f8:  lw    v0,0(a0)          # load the word at the cursor
10285fc:  beq   v0,a2,0x102862c   # match?
1028600:  sltu  v0,a0,a1
1028604:  beqzl v0,0x1028630     # cursor >= end -> done
102860c:  addiu a0,a0,4           # cursor += 4
1028610:  (loop)
102862c:  (match)                 movz a0,zero,v0
1028630:  jr   $ra
1028634:  move  v0,a0             # <-- returns $a0, NOT $v0
```

Unambiguous: on a match it returns the address of the matching word; on a **miss** it returns the
cursor after it has stepped past the end of the window — **not zero**.

That is a first-party source, and it settles a question the public documentation leaves open.

### 3. Two fixes, both in the runtime

**(a) The 64× rescan — carried over from G1.2, still the biggest win.**
`ps2ResolveGuestPointer` folds KSEG0/KSEG1 onto the same 32 MB, so the caller's window was being
walked 64 times over. `scannedWords` per call: **537,001,983 → 8,392,703.**

**(b) The miss return value.** The handler returned `0` when nothing matched. Changed to return the
aligned end of the window, per the reference above. One line, in
`ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp`, with the reasoning and the disassembly in a
comment so the next person can find the line to change if a better source appears.

**It works, measurably.** The guest's behaviour changed. Before, a miss returned `0` and the
enumeration restarted. Now the search terminates at the window end and the guest advances past it —
visible in the call sequence, where the third call's start is now `0x80080004` instead of
restarting at `0x4`:

```
start=0x80000000 target=0x10285c0 -> 0x800120e8   (hit)
start=0x800120ec target=0x10285c0 -> end of window (was: 0, restart)
start=0x80080004 target=0x10285c0 -> end of window
```

> **A trap worth naming:** the runtime's own log line prints the *internal* `resultAddr`, which is
> still `0x0` on a miss. It does **not** print what was returned in `$v0`. Reading that log to
> judge the return value is misleading — I nearly did exactly that. The evidence that the fix
> landed is the *next* call's `start`, not the previous call's `result=`.

### 4. The unit tests — on the handler, not on "the game got further"

New file `ps2xTest/src/ps2_find_address_tests.cpp`, 5 cases, calling the handler directly with
`$a0`–`$a2` and reading `$v0` back, with no `PS2Runtime` and no game involved:

1. finds a target and returns the address of the matching word
2. **returns the end of the window when the target is absent, never zero**
3. a KSEG-spanning window finds the word, and yields a result in the caller's own address family
4. respects the start bound
5. returns the first match when the value appears twice

**And one pre-existing test was wrong.** `ps2xTest/src/ps2_runtime_kernel_tests.cpp` asserted
*"FindAddress should return 0 when no matching word exists"*. That assertion contradicts the
reference implementation above, so I changed it — and I am flagging that explicitly, because editing
a test to match new code is exactly the move that should be distrusted. It is defensible here only
because the new expectation comes from a first-party source and the reasoning is in the commit and
in the code comment. If that reasoning is wrong, this test is where it will show.

**Result: `441/441` pass** (the suite needs to be run from the directory containing
`ps2recomp/instructions.h`, or 3 unrelated VU0/header tests fail on a path lookup — that is
pre-existing and not mine).

### 5. Why it still does not get past — the next wall, named

Now that the search terminates honestly, the guest's outer loop can be read properly. At
`0x01028740`:

```mips
10286f0:  addiu s1,s3,-524      # s1 = previousResult - 0x20C
10286f8:  addiu s0,s2,-360      # s0 = currentResult  - 0x168
1028740:  bne  s1,s0,0x1028708   # loop until they are equal
```

It converges when **`previousResult - currentResult == 0xA4`, i.e. exactly 164 bytes.**

So the guest is looking for **two function pointers 164 bytes apart**. What is actually in the
image:

```
func_010285F8 0x010285f8: 1 static occurrence at 0x01035354
func_010285C0 0x010285c0: 1 static occurrence at 0x0103535C
                               0x0103535C - 0x01035354 = 8 bytes
```

**One occurrence of each, and they are 8 bytes apart — a pair of consecutive entries in a handler
table. The pair the guest wants is 164 bytes apart, and it is not in the loaded image.**

**The wall, named: GT4 is waiting on a table of 164-byte-stride entries that lives in
`CORE.GT4` (2 MB) or `GT4.VOL` (2.29 GiB) — the two files `docs/DISC-MAP.md` §5 has called opaque
since G0.2, and neither of which we have ever loaded.** We load only the ELF, and the guest reaches
its file-loading path only *after* this table walk completes, so it cannot ask us for the data
itself. There is no syscall to serve here and no handler to write: **the missing thing is the data,
and getting it in without the guest asking would be the faking this project refuses.**

**Falsifier:** if loading `CORE.GT4` at the right address does *not* make this loop converge, the
"missing data" reading is wrong and the divergence is in our syscall answers — in which case
`FindAddress`'s contract is still not what GT4 expects, and the tests in §4 are pinning the wrong
thing.

**Not claimed:** I have not loaded `CORE.GT4`, and I have not verified that its contents are a
164-byte-stride pointer table. That is the next measurement, not a conclusion.

### 6. What this dish is worth, honestly

| | G1.2 | G1.3 |
|---|---|---|
| `functions_entered` | 3 | **3** |
| `halt` | `stuck_in_syscall` | `stuck_in_syscall` |
| `0x83` calls | 311 | 506 |
| `scannedWords` per call | 8,392,703 | 8,392,703 |
| `0x83` on a miss | returned `0` | **returns the window end** (sourced, tested) |
| Search terminates at the window end | ❌ | **✅** |
| Unit tests on the handler | 0 | **5** (plus 1 corrected) |
| The wall | "a syscall we serve" | **"data we have not loaded"** |

The gate says `functions_entered >= 4`. **It is 3. This dish failed its gate.** What it bought is a
correct and tested syscall, a 64× cheaper search, and — more valuable — the difference between a
wall described as *"our kernel call returns 0"* and one described as *"the guest is waiting for a
164-byte-stride table that is in a data file we have not loaded"*. Only the second one tells the
next dish what to do.

### 7. Next

1. **Load `CORE.GT4`.** 2 MB, already extracted at `/mnt/ssd/gt4/work/CORE.GT4`, SHA-256
   `85d26aa8…`. Find out whether it is a 164-byte-stride pointer table. This is the single
   measurement that either confirms or kills §5.
2. **Then the file path.** The guest will eventually need IOP + SIF + `fileio` to open it for
   itself; the syscall list currently has no file syscalls at all, so that whole path is unbuilt.
3. **The GS and VU1 are still ahead, not behind** — `total_mmio_accesses=0` remains 0. G2 is
   untouched by this dish.

### Licence note

Measurements, addresses, short disassembly excerpts, and a syscall-contract citation, for a disc
the user owns. References to ps2sdk and ps2rd are cited by URL and are not reproduced here.
Not affiliated with Sony Interactive Entertainment or Polyphony Digital.

---

## 10. G1.3 CORRECTION — §5 was wrong, and a full-disc scan proves it

> The previous section ended with *"the wall is missing data: that table lives in `CORE.GT4` or
> `GT4.VOL`."* **That is false, and I am retracting it before it sends the next dish after a file
> that provably does not contain the table.**

### The test

§5's claim was falsifiable, so I ran the falsifier. A scan of the **entire disc** — all
5,314,478,080 bytes — for the two 4-byte little-endian values the guest is hunting for:

```
scanned 5314478080 bytes
  func_010285F8  1 occurrence  0x10d354
  func_010285C0  1 occurrence  0x10d35c
```

**One occurrence each, on the whole disc, and they are 8 bytes apart.** There is no 164-byte-stride
table anywhere on this DVD. `CORE.GT4` contains **zero** literal occurrences of either, and only
3 words in 2 MB that look like in-image code pointers — noise, consistent with its 7.96 bits/byte.

### What is actually at 0x01035350 — the table, in the ELF, all along

Both pointers live in the executable's own `.data`, and reading the words around them gives the
structure:

```
0x01035350:  0x00000083   number = 0x83
0x01035354:  0x010285F8   handler = sub_010285F8
0x01035358:  0x0000005A   number = 0x5A
0x0103535C:  0x010285C0   handler = sub_010285C0
0x01035360:  0x00000000   terminator
0x01035364:  0x00000000
```

That is a **syscall-override table** — the `SyscallData { int syscall_num; void *function; }`
array from ps2SDK's `ee/kernel/src/libosd.c`, the same shape as its `SyscallPatchEntries`:

```c
struct SyscallData { int syscall_num; void * function; };
static struct SyscallData SyscallPatchEntries[] = { {0x5A, &kCopy}, {0x5B, ...}, ... };
```

and it is installed with `setup()`, which is SCE syscall **`0x74 SetSyscall`** — a syscall the guest
**already called, twice**, at `0x01028788`.

### So the guest is not missing data. It is installing syscall overrides.

The picture now fits everything observed, with no missing file:

1. GT4 boots, sets up a heap (`0x3D`), a thread (`0x3C`), a semaphore (`0x40`).
2. It holds a table of kernel-call patches: *replace syscall `0x83` with `sub_010285F8`, and
   syscall `0x5A` with `sub_010285C0`*.
3. To patch a kernel syscall it must first **locate the slot** in the console's syscall table —
   which is what `FindAddress` is for, and what ps2SDK calls `GetEntryAddress()`. The console's
   table base is **`0x80011F80`**, and `PS2Runtime::initializeEeKernelState()` already knows it
   (`kTableGuestBase`).
4. The guest searches, and `SetSyscall` is called twice — so the patch path *is* being taken.

**This also explains why the two pointers looked "8 bytes apart" while §5 demanded 164.** The
`0xA4` figure came from my reading of the loop at `0x01028740`
(`s1 = s3 - 0x20C`, `s0 = s2 - 0x168`, so `s1 == s0 ⟺ s3 - s2 == 0xA4`). The real table stride is
**8**. My instruction reading and the actual data disagree, and when they disagree **the data wins**.
I should not have carried the `0xA4` derivation into a committed finding without checking it
against the bytes — that is the same mistake as the G0.1 endianness generalisation, and I made it
again one dish later.

### What survives, and what does not

| §5 claim | Status |
|---|---|
| "The two targets are 8 bytes apart" | ✅ **confirmed** — now known to be a `{number, handler}` table |
| "a 164-byte-stride table is what the guest converges on" | ❌ **retracted** — contradicted by the data |
| "the table lives in `CORE.GT4` or `GT4.VOL`" | ❌ **retracted** — a full-disc scan finds one occurrence each, both in the ELF |
| "`FindAddress` is served and now returns the window end on a miss" | ✅ **stands** — sourced, unit-tested, 441/441 |
| the 64× rescan fix | ✅ **stands** — `scannedWords` 537,001,983 → 8,392,703 |
| **`functions_entered` = 3, gate not met** | ✅ **stands, unchanged** |

**The gate outcome is unaffected by this correction.** `functions_entered` was 3 before and is 3
after; the correction changes only *why*, and it changes it in a way that points somewhere real
instead of somewhere empty.

### Where the wall now points

Not at a missing file. At the **kernel syscall table itself**: the guest wants to patch slots for
`0x83` and `0x5A`, it is asking our `FindAddress` where those slots are, and our `FindAddress`
searches guest memory for a *value* — it never consults the kernel table the way
ps2SDK's `GetEntryAddress()` computes it as `0x80011F80 + n * 4`.

That is a concrete, testable hypothesis for the next dish, and unlike the one it replaces it does
not require a file we do not have.

**Falsifier for this correction:** if `0x80011F80 + 0x83 * 4` is not how the console kernel
addresses syscall `0x83`, this reading is wrong too — and the way to check it is the `0xFFFFC402`
offset in ps2SDK's own `libosd.c`, which the comment there says is
*"relative to the start of the syscall table, and is in units of 32-bit pointers"*. That is a
number we can test, not a story.

---

## 11. G1.3b — WHAT is the guest looking for?

The brief for this one was right to insist on the *call* rather than the count. Reconstructing
intent from disassembly had already been wrong twice in this project, so this time the arguments
and the value actually returned are logged at the point where they are known.

### The trace

New logging in `ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp` prints, for the first 24 calls and
afterwards for any previously unseen argument tuple:

```
[FindAddress] call=3 a0=0x4 a1=0x80080000 a2=0x10285f8 a3=0x1041800 ret=0x1218c
             outcome=hit caller_pc=0x1028640 caller_ra=0x102871c scanned=8392703
```

Two things this fixes immediately. The pre-existing diagnostic line printed the **internal**
`resultAddr`, which is `0` on a miss and is *not* what the guest receives — that mismatch cost a
whole dish in G1.3. And the caller PC is the same for every call (`0x1028640`) because
`sub_01028638` is a three-instruction stub (`li $v1,0x83` / `syscall` / `jr $ra`); the *useful*
call site is `caller_ra`, the return address, and it distinguishes the two call sites in the loop.

### The three answers

**1. What is the guest looking for?**

Two specific 32-bit values, both of which are **pointers to its own code**: `0x010285F8` and
`0x010285C0`. It searches the guest range `[0x00000004, 0x80080000)` word by word for them,
repeatedly, advancing its start past each hit.

Those pointers are not a mystery — they are the two handlers in GT4's own syscall-override table,
at `0x01035350`:

```
0x01035350:  0x00000083   syscall number 0x83
0x01035354:  0x010285F8   handler  sub_010285F8
0x01035358:  0x0000005A   syscall number 0x5A
0x0103535C:  0x010285C0   handler  sub_010285C0
0x01035360:  0x00000000   terminator
```

which is ps2SDK's `struct SyscallData { int syscall_num; void *function; }`. And per the PS2 syscall
table, **`0x5A` is `Copy` and `0x83` is `FindAddress`** — the guest is installing its own
replacements for both. `sub_010285C0` is a word-copying routine, which is exactly what `Copy` is.

**So: the guest is trying to find where the console kernel's syscall table holds its two override
handlers, so it can confirm the overrides landed.**

**2. What do we return?**

The address of the matching word on a hit, and the end of the search window on a miss. From the
trace: `0x0001218C`, `0x01035354`, `0x800120E8` on hits and `0x80080000` on misses.

**3. Why does it retry?**

Because it is running a check that cannot close. At the loop (`0x01028740`):

```mips
10286f0:  addiu s1,s3,-524     # s1 = s3 - 0x20C
10286f8:  addiu s0,s2,-360     # s0 = s2 - 0x168
1028740:  bne  s1,s0,0x1028708 # loop until they are equal
```

and those two constants are not arbitrary:

| constant | equals | syscall |
|---|---|---|
| `0x20C` | `0x83 * 4` | `FindAddress` |
| `0x168` | `0x5A * 4` | `Copy` |
| difference `0xA4` (164) | `0x8001218C - 0x800120E8` | the gap between the two slots |

ps2SDK's own `ee/kernel/src/libosd.c` puts the console kernel's syscall table at **`0x80011F80`**, so
slot `0x83` is `0x8001218C` and slot `0x5A` is `0x800120E8`. The loop is therefore
*"subtract n*4 from the address you found, and check both give me the table base."*

**It could not close, because we were reporting the same physical word in two different address
families.** The trace shows it:

```
call=1  a2=0x10285c0  ret=0x800120e8   <- KSEG1 family
call=3  a2=0x10285f8  ret=0x1218c      <- low family, SAME physical word
```

`0x800120E8 - 0x168 = 0x80011F80` ✓ but `0x1218C - 0x20C = 0x11F80` ✗. Two aliases of one
32 MB of RAM, two different bases, and the comparison never matches — forever. The guest asked
with a KSEG1 start for one and a low start for the other, and we faithfully answered in each.

### The fix, and it is measurable

Canonicalise the result into the **KSEG1 (uncached) window**, `0x80000000 | physical`, so every
hit is reported in one family. One line, in the handler, with the arithmetic in the comment.

The trace after the change:

```
call=1  a2=0x10285c0  ret=0x800120e8   hit    <- slot for syscall 0x5A
call=2  a2=0x10285c0  ret=0x80080000   miss
call=3  a2=0x10285f8  ret=0x8001218c   hit    <- slot for syscall 0x83   (was 0x1218c)
call=4  a2=0x10285f8  ret=0x80080000   miss
```

**Both kernel slots are now correctly discoverable**, and the enumeration shortened from three
calls per pass to two. `SetSyscall` was working all along — the runtime was simply not reporting
where it had put the handlers in a form the guest could do arithmetic on.

### The decision, and it is the honest one

`functions_entered` is **still 3**, so **the gate is not met again.**

| | G1.3 | G1.3b |
|---|---|---|
| `functions_entered` | 3 | **3** |
| `0x83` calls in the run | 506 | 2-call cycle instead of 3 |
| `0x83` hit for syscall `0x83`'s slot | reported as `0x1218C` (low) | **`0x8001218C`** (KSEG1) |
| the guest's base comparison | could never close | both bases now `0x80011F80` |
| unit tests | 441/441 | **441/441** |

Per the brief's own decision rule, I am **naming the remaining dependency and stopping** rather
than spending another hour poking. What is left is specific:

> **One call site is untraced, and it is the one that matters.** The trace begins at call 1, whose
> return address is `0x010286F0` — the `jal` at `0x010286E8`. But `s3`, which the loop's
> termination depends on, is assigned at `0x010286DC` from a *different* `jal` at `0x010286D4`, and
> **that call does not appear in the trace at all.** The guest entered this function at exactly
> `0x010286DC` — the resume point immediately after the `0x010286D4` call — so that call was made
> and completed during the one `EeDispatcherTransfer` before the harness regained control, outside
> the traced region.
>
> That call is the first thing the guest does on entering this code, it searches for
> `0x010285F8` over the KSEG1 window, and its result is `s3`, the value the loop cannot converge
> without. **We do not know what it returned.** At entry, `SetSyscall` has not yet run, so the only
> copy of that pointer in the KSEG1 window is the static one in the guest's own `.data` at physical
> `0x35354` → `0x80035354`, which is not the kernel slot and would give the wrong base.

**Next dish, one line of work:** extend the trace to cover the pre-entry calls — either start
tracing from the first guest instruction rather than the first one the harness loop observes, or
log the `0x010286D4` call site explicitly. Then read what `s3` actually was. That is a measurement,
not a guess, and it is the last thing standing between here and the guest moving on.

**Falsifier for this whole section:** if `s3` from the untraced call is already `0x8001218C`, then
the base comparison should have matched on the first iteration and something else is holding the
loop — in which case the address-family fix, though correct and measured, is not the blocker.

### Correction to §10, again

§10 retracted my `0xA4` claim. **That retraction was wrong, and this section is the proof.**
`0xA4` is real: it is exactly the distance between syscall `0x83`'s and `0x5A`'s slots in the
console kernel's table. What defeated the comparison was not a misread stride — it was that we
reported the two slots under different address families, so the two derived bases differed by
exactly `0x80000000`. The stride reading was right; the conclusion drawn from it was not, because
I had not yet looked at the values the guest actually received.

That is three findings in a row now that came from reasoning about disassembly instead of logging
what happened. The trace is cheap, it is in the runtime where the values are known, and it should
have been the first thing built.

---

## 12. G1.4 — the register-level trace, three attempts, and a STUCK

**Branch taken: C — the guest is blocked on something we have not built.** But C was reached the
honest way, by measurement, and the dish did land one real fix on the way.

```
VULCAN4 BOOT REPORT functions_entered=3 halt=stuck_in_syscall bios_files=0
```

**The gate for this dish is not met: `functions_entered` is still 3 and `halt` is unchanged.**

### Attempt 1 — log the loop registers, not just the arguments

The G1.3b trace printed arguments and the return value. It did not print the registers the guest's
loop actually tests, so the convergence condition could only be guessed at. The trace now prints
`s0`–`s5` and `v0`. That ended the guessing immediately:

```
call=1 (ra=0x10286f0): s0=0x1035350 s1=0x0        s2=0x0         s3=0x0
call=2 (ra=0x1028738): s0=0x80011f80 s1=0xfffffdf4 s2=0x800120e8 s3=0x0
call=4 (ra=0x102871c): s0=0xfffffe98 s1=0x80011f80 s2=0x0         s3=0x8001218c
```

The loop is `s1 = s3 - 0x20C` / `s0 = s2 - 0x168` / `bne s1,s0` — *"subtract n*4 from the address
where I found my handler, and check both give me the console kernel's syscall table base
`0x80011F80`."*

And the registers say exactly why it cannot close:

| pass | `s1 = s3 - 0x20C` | `s0 = s2 - 0x168` | equal? |
|---|---|---|---|
| 1 | `0xFFFFFDF4` — `s3` is **0** | `0x80011F80` | no |
| 2 | `0x80011F80` | `0xFFFFFEF8`… `s2` is **0** | no |

**`s3` is zero. Nothing in the run ever sets it.**

### What never ran

`s3` is assigned at `0x010286DC` from the search at `0x010286CC`–`0x010286D8`, inside
`sub_01028680`. Counting call sites across the whole run:

```
$ grep -oE "caller_ra=0x[0-9a-f]+" boot.log | sort | uniq -c
      1 caller_ra=0x10286f0
     24 caller_ra=0x102871c
      1 caller_ra=0x1028738
$ grep -c "caller_ra=0x10286dc" boot.log
0
```

**128 `FindAddress` calls, none from `0x010286D4`.** The recompiler *did* emit that call
(`ps2_recompiled_functions.cpp:187146`, in `sub_01028680`, with `0x10286cc` present as a resume
`case`), so this is not a codegen gap — **the guest does not execute that path.**

**The named dependency, with an address:** *the guest must execute `0x010286CC`–`0x010286D8` in
`sub_01028680` to populate `s3`. Our run never reaches it, and the guest arrives at `0x010286DC` —
that `jal`'s own return point — without having taken the branch that leads to it.* The runtime must
be resolving some guest control transfer to the wrong resume point, skipping the call. That is a
**dispatch/resume-model problem, not a syscall problem**, and it is a recompiler/runtime correctness
issue that needs its own dish with its own test. I have not fixed it and I am not going to guess at
it: my trace proves a call is missing but does not prove which transfer loses it, and two previous
guesses in this project were wrong.

### Attempt 2 — a general cycle detector (correct, and it cannot fire here)

Added to the harness: it records recent guest PCs and stops with `halt=guest_cycle_no_progress` if
the same cycle repeats with no new address reached. It is general — it knows nothing about the guest.

**It does not fire, and the reason is worth writing down.** The detector lives in the driver loop,
so it can only see the guest at the moments the driver regains control. It regained control **three
times in the entire run**. A loop the guest never yields from is invisible from outside it. The
detector is kept because it is correct and it will fire for any guest that does pass through the
dispatcher.

### Attempt 3 — initialise the EE scheduler (correct, and it does not arm)

`PS2Runtime::run()` calls `m_eeScheduler->reset(...)` before executing the guest. A hand-rolled
harness is easy to miss that line, and missing it looks like it should matter: `eeCheckpointDue()`
reads `EeScheduler::checkpointDue()`, which decides from a deadline cycle counter. Both
`PS2Runtime::eeScheduler()` and `EeScheduler::reset()` are **public**, so this needed no runtime
patch. Added.

**It did not change the outcome**, and the reason is not a bug:

```cpp
// EeScheduler::checkpointDue
if (m_eeCycle < m_sliceEndCycle) return false;
const GuestThread *running = currentThread();
if (running != nullptr && hasReadyAtOrAbovePriority(running->currentPriority)) { ... return true; }
renewTimeSlice();
return false;
```

For a single-threaded guest with nothing else ready there is nothing to preempt, so
**`eeCheckpointDue()` correctly returns false forever.** The scheduler is doing its job. GT4's
back-edge at `0x01028740` is taken every iteration, so it *would* yield — but only if something
needed running, and nothing does.

**This is a real limitation of the harness, stated plainly: a guest loop that never yields cannot be
observed or bounded from the driver, only by a wall-clock watchdog.** G1.1's "it cannot spin
forever" is currently true by watchdog, not by construction. Fixing that properly means the
recompiled code should yield on a *cycle* budget rather than only on scheduler preemption — a
recompiler change, which this dish was told not to make, and which deserves its own dish and test.

### What this dish is worth

| | before | after |
|---|---|---|
| `FindAddress` result address family | mixed (`0x1218C` and `0x800120E8`) | **canonical KSEG1** — **fixed** |
| slot `0x83` discoverable at `0x8001218C` | ✗ | **✓** |
| slot `0x5A` discoverable at `0x800120E8` | ✓ | ✓ |
| enumeration length per pass | 3 calls | **2 calls** |
| trace shows the loop's own registers | ✗ | **✓ `s0`–`s5`, `v0`** |
| call at `0x010286D4` accounted for | unknown | **measured: never runs, 0/128** |
| `functions_entered` | 3 | **3** |
| unit tests | 441/441 | **441/441** |

The address-family fix is real, correct, sourced and tested. **It did not get the guest moving**,
because the guest's loop was never going to close: it is missing a value, not being given a wrong
one.

### STUCK

```
STUCK: the guest never executes 0x010286CC-0x010286D8 in sub_01028680, so s3 is 0 and the
       convergence loop at 0x01028740 can never close.
TRIED: (1) register-level FindAddress tracing -> proved s3=0 and that 0 of 128 calls come from
       0x010286D4; (2) a general guest-cycle detector -> correct, but the driver only regains
       control 3 times so it cannot see an in-function loop; (3) initialising the EE scheduler via
       the public eeScheduler().reset() -> correct and faithful to run(), but checkpointDue()
       legitimately returns false for a single-threaded guest.
BLOCKED BY: a guest control transfer is being resolved to the wrong resume point, skipping the call
       that sets s3. The trace proves a call is missing but not which transfer loses it.
NEED: the dispatch/resume decision for the transfer that lands the guest at 0x010286DC. Concretely:
       log the guest PC and the resume PC at every dispatchGuestBranch yield for this function, and
       compare against the MIPS fall-through. That is a recompiler/runtime correctness question and
       should be its own dish with its own test, not a guess made here.
```

### Left in the tree, and why

- **the KSEG1 canonicalisation** — a measured, tested correctness fix. Keep.
- **the register trace** — the thing that found `s3=0`. Keep; it is the reason this dish produced
  an answer at all.
- **the cycle detector** — correct but inert for this guest. Keep, with the limitation written down.
- **the scheduler reset** — faithful to `run()`. Keep; it is one line and it is right.

---

## 13. G1.4 addendum — the STUCK's "NEED" is answered: the transfer is localised

§12 ended with a STUCK whose `NEED` was a specific measurement — *"log the guest PC and the resume PC
at every `dispatchGuestBranch` yield for `sub_01028680` and compare against the MIPS fall-through."*
Leaving that undone would not have been finishing the dish, so I took it. **It found the defect.**

### The instrumentation

Three capped traces added to the runtime, all diagnostic, all bounded (400 events each):

- `[Yield]` — every time `dispatchGuestBranch` returns `false` at its checkpoint, with the source
  PC, the target, the fall-through and `$ra`.
- `[Dispatch]` — every inter-function transfer *into* a guest function.
- `[Returned]` — what `ctx->pc` was left holding when that function returned.

### What it shows

```
[Dispatch] n=7  target=0x1028780 source=0x10286c4 fallthrough=0x10286cc
[Returned] n=5  ctx_pc=0x10286cc  entry=0x1028780 fallthrough=0x10286cc
[Dispatch] n=8  target=0x1028638 entry=0x1028638 source=0x10286d4 fallthrough=0x10286dc ra=0x10286dc
VULCAN4 TRACE entry=2 pc=0x01028640 ra=0x010286dc          <-- control returns at the STUB'S OWN jr $ra
VULCAN4 TRACE entry=3 pc=0x010286dc                        <-- resumes at the jal's return; s3 = v0 = 0
[Dispatch] n=9  target=0x1028638 entry=0x1028638 source=0x10286e8 fallthrough=0x10286f0
```

Three facts, and together they are the bug:

1. **The call at `0x010286D4` *is* dispatched** — `[Dispatch] n=8` exists, targeting the
   `FindAddress` stub at `0x01028638`, with `$ra = 0x010286DC` and fall-through `0x010286DC`.
   So the transfer is not dropped before dispatch. That kills the hypothesis in §12.
2. **The stub's body never runs.** Between that dispatch and the next one there is **no
   `[FindAddress]` line and no `[Yield]` line** — the stub is three instructions
   (`addiu $v1,0x83` / `syscall` / `jr $ra`) and its `syscall` is the only thing that calls
   `FindAddress`. Control comes back at **`0x01028640`, the stub's own `jr $ra`**, with `$ra`
   still `0x010286DC`.
3. **So the guest lands on `0x010286DC` having never executed the call's body**, and `s3` is
   assigned `v0 = 0`. Every downstream measurement follows from that.

**`[Dispatch] n=8` has no matching `[Returned]`.** The stub was entered at `0x01028638` and
control left the driver at `0x01028640` — the `jr $ra` label — without the function ever reaching
`handleSyscall`.

### What this is, and what it is not

- **It is not a syscall problem.** `0x83` is served, correctly, 128 times.
- **It is not a codegen gap.** The `jal` at `0x010286D4` is emitted correctly
  (`ps2_recompiled_functions.cpp:187146`).
- **It is a resume-model defect in the interaction between generated code and
  `PS2Runtime::dispatchGuestBranch`.** Control reaches the *return* point of a call without the
  callee's body having executed. Note the shape of the generated `jal`:

  ```cpp
  // 0x10286d4: 0xc40a18e  jal  func_1028638
  ctx->pc = 0x10286D4u;
  SET_GPR_U32(ctx, 31, 0x10286DCu);
  ...  delay slot ...
  ctx->pc = 0x1028638u;
  if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1028638u, 0x10286D4u, 0x10286DCu, DirectCall, "JAL")) {
      return;                                  // <-- on yield, re-entry happens at ctx->pc
  }
  ctx->pc = 0x10286DCu;
  label_10286dc:                               // <-- NO ctx->pc re-entry check here
  ```

  A `jal` elsewhere in the same file *does* carry a re-entry check
  (`if (ctx->pc == 0x1000570u) { ... goto label_1000574; }`, at the delay-slot label). This one does
  not. **Whether that asymmetry is the defect, or merely where it shows up, is not yet established**
  — and I am not going to claim it is without a test that fails on the current code.

### State of the dish

| | |
|---|---|
| `functions_entered` | **3** — unchanged, **gate not met** |
| `halt` | `stuck_in_syscall` — unchanged |
| `bios_files` | **0** |
| unit tests | **441/441** |
| the KSEG1 `FindAddress` fix | landed, tested, kept |
| the register trace | landed — it is what found `s3 = 0` |
| the dispatch/yield/return traces | landed, capped, kept — they localise the defect |
| the cycle detector | correct, inert for this guest, kept |
| the scheduler reset | faithful to `run()`, kept |

**The gate for G1.4 is not met, and this addendum does not claim otherwise.** What changed is that
the STUCK is no longer vague: §12 said *"a control transfer is being resolved to the wrong resume
point"*. §13 says **which one** — the `jal` at `0x010286D4` into `sub_01028638` — and shows the
callee's body being skipped. That is a defect a next dish can write a failing test against, which is
the most useful thing a stuck dish can hand over.

### Next, precisely

1. Write a test that drives `sub_01028638` (or any three-instruction `li/syscall/jr` stub) through
   `dispatchGuestBranch` and asserts the callee body executes before control reaches the return
   point. It should fail on today's code.
2. Only then look at the recompiler's `jal` emission: the asymmetry between a `jal` with a
   re-entry check and one without is the first thing to check, and the check is cheap once (1) has
   given a red test to aim at.

**Until that red test exists, any fix is a guess** — and two guesses in this project have already
been wrong.


---

## 8. G1.6 — WHO WAS SUPPOSED TO WRITE THAT?

**Verdict: the value is not missing. It is in the image, in the search window, and our own trace
contradicts itself about it.** The gate is **not** met: the report still reads
`halt=livelocked_in_syscall`.

### 1. The value, identified

The guest is not asking a question we answer wrongly. From the disassembly at the call site
(`objdump`, `/mnt/ssd/gt4/work/SCUS_973.28`):

```
010286cc:  lui   a0, 0x8000        ; a0 = 0x80000000   <- scan start
010286d0:  lui   a1, 0x8008        ; a1 = 0x80080000   <- scan end
010286d4:  jal   0x1028638         ; FindAddress
010286d8:  addiu a2, s5, -31240    ; a2 = 0x10285F8
010286dc:  move  s3, v0
010286e0:  lui   a0, 0x8000
010286e4:  lui   a1, 0x8008
010286e8:  jal   0x1028638
010286ec:  addiu a2, s4, -31296    ; a2 = 0x10285C0
010286f0:  addiu s1, s3, -524      ; s1 = s3 - 0x20C
010286f4:  move  s2, v0
010286f8:  addiu s0, s2, -360      ; s0 = s2 - 0x168
010286fc:  beq   s1, s0, 0x1028750 ; stop when they line up
0102870c:  addiu a0, s3, 4         ; otherwise search for the NEXT one
01028718:  addiu a2, s5, -31240    ; looking for 0x10285F8 again
```

So the signature really is `FindAddress(start, end, value)` with `start=0x80000000`, `end=0x80080000`
and **value = `0x10285F8` and `0x10285C0`** — and the guest then loops, searching for the *next*
occurrence, comparing `hit(85F8) - 0x20C` against `hit(85C0) - 0x168` and stopping when they match.

**What kind of value is it: a 32-bit CODE POINTER.** Proof: `0x10285F8` and `0x10285C0` are not
data — they are the entry addresses of two of GT4's own **memory-scan helper routines**:

| Symbol | First instructions | What it is |
|---|---|---|
| `sub_010285F8` | `lw $v0,0($a0)` / `beq $v0,$a2` / `addiu $a0,$a0,4` | a word-by-word memory search |
| `sub_010285C0` | `srl $a2,$a2,2` / `lw $v1,0($a1)` / `daddu $a3,$a3,1` | an indexed memory search |

The guest is looking for **pointers to its own scanners**. It wants to call them indirectly.

### 2. Where the value lives

Each value occurs **exactly once** in the whole 273,020-byte image, and they are 8 bytes apart —
one 16-byte descriptor:

```
vaddr 0x01035354 (RAM offset 0x00035354), inside the SECOND PT_LOAD segment
  +0   0x010285F8   <- code pointer to sub_010285F8
  +4   0x0000005A   <- 90: a count, handle or id
  +8   0x010285C0   <- code pointer to sub_010285C0
  +12  0x00000000   <- terminator
```

Segment map (`readelf -l`): segment 2 is `off=0x02ec80 -> vaddr=0x0102dc80, filesz=0x13aa4,
memsz=0x23d2c, RW`, so file offset `0x36354` maps to RAM offset `0x35354` — and `0x35354` **is inside
the search window** `[0x00000000, 0x00080000)` the guest scans.

### 3. The contradiction — and it is in OUR trace, not the game's

`scannedWords=112581` is exactly the full window from `0x000120ec` to `0x00080000`
(`0x6DF14 / 4 = 112,580`). So the scan walked over RAM offset `0x35354` and did not match — while
the file says the word there is `0x010285F8`.

Worse, the diagnostic reports **`allZero=true`** for that window. That cannot be true if segment 2
were loaded: segment 2 covers RAM `0x2dc80..0x41724`, which lies inside the window and is full of
data. **`allZero=true` over a window that provably contains a non-zero word is a self-contradiction
in our own log line.**

### 4. So who should write it, honestly

**Not a subsystem we lack.** No IRX, no BIOS, no export table is required to explain this: the value
is statically present in GT4's own data segment, at an absolute address, needing no relocation.

Two candidates remain, and they are distinguishable with one experiment that this dish did not have
runway for:

1. **The ELF loader is not mapping segment 2 where the ELF says.** `PS2Runtime`'s loader
   (`ps2_runtime.cpp:847+`) copies `filesz` bytes at `translateAddress(ph.vaddr)`. If that mapping is
   wrong for the second segment, the word is absent and `allZero=true` becomes *true* — the trace
   would be self-consistent and the guest's search is right to fail.
2. **The `FindAddress` scan itself is wrong** — for instance the `allZero` fast-path short-circuits
   before reaching `0x35354`, or the comparison normalises wrongly. Then the value is present and we
   fail to see it.

**The one experiment that settles it:** dump `RDRAM[0x35354]` at the moment the guest issues the
first `FindAddress`, and compare it to `0x010285F8`. Non-zero → the scan is broken (2). Zero → the
loader is broken (1). That is a one-line diagnostic and it is the next thing to do. **This dish ran
out of runway before it, and it is not going to claim an answer it did not measure.**

### 5. What this rules OUT, which is worth having

- **It is not the missing-writer story I set out to prove.** I expected an absent IRX export table.
  The value is in the file. The hypothesis was wrong.
- **It is not a `FindAddress` semantics problem in the way G1.2 fixed.** The handler's argument
  order matches the guest's own call sites exactly (`a0`,`a1`,`a2`), and the align-to-`uint32` window
  arithmetic is right; the scan length it reports matches the window it was given.
- **It is not "the guest never reached an initialiser"** in the simplest reading — the value needs no
  initialiser.



### 6. G1.6 RESULT — the experiment, run: it was the LOADER

The diagnostic in `tools/harness/vulcan4_harness.cpp` (`VULCAN4 PROBE1..4`) reads RDRAM directly
after the image is loaded. **Before the fix:**

```
VULCAN4 PROBE2 rdram[0x35354..0x35363] = 0x0 0x0 0x0 0x0   <-- sub_010285F8 IS **MISSING**
VULCAN4 PROBE3 0x010285F8 at RAM offset 0x1035354, 0x010285C0 at RAM offset 0x103535c
VULCAN4 PROBE4 search window [0,0x80000): non-zero words = 2
```

**Candidate 1 was right: the ELF loader.** `PS2Memory::translateAddress`
(`ps2_memory.cpp:600`) treated every address below `0x80000000` as an identity map:

```cpp
// In this runtime, low segments are treated as physical-style addresses already.
if (virtualAddress < 0x80000000) { return virtualAddress; }
```

But `0x01000000`–`0x01FFFFFF` is the PS2 **user segment**: the kernel loads a game at `0x01000000`,
so user vaddr `U` is RDRAM at `U - 0x01000000`. That must agree with the KSEG0 aliases, because the
guest reaches the same bytes both ways — user `0x01035354` and KSEG0 `0x81035354` are one location,
physical `0x35354`. Under the identity rule they were **16 MB apart**, so the guest's data was
invisible to every KSEG0 pointer it formed. That is why the search window held **2** non-zero words.

**The fix** adds the user-segment case ahead of the identity rule
(`tools/patches/ps2recomp-linux-g16-userseg.patch`, 1 file, +22/-1):

```cpp
if (virtualAddress >= 0x01000000u && virtualAddress < 0x02000000u)
{
    return virtualAddress - 0x01000000u;
}
```

**After the fix:**

```
VULCAN4 PROBE2 rdram[0x35354..0x35363] = 0x10285f8 0x5a 0x10285c0 0x0  <-- sub_010285F8 IS PRESENT
VULCAN4 PROBE3 0x010285F8 at RAM offset 0x35354, 0x010285C0 at RAM offset 0x3535c
VULCAN4 PROBE4 search window [0,0x80000): non-zero words = 60951
```

The descriptor is now exactly where the guest looks, and the window went from 2 non-zero words to
**60,951**. The memory map is now self-consistent.

### 7. And the honest part: the gate moved, the guest did NOT

```
VULCAN4 BOOT REPORT functions_entered=1 halt=spinning_in_guest_code bios_files=0
```

The gate is met on its second condition — the halt is no longer a livelock — and the report now
names the real remaining state: **no syscall executing, the guest spinning in recompiled code at
`0x0102871C` with `distinct_pcs=1`.**

**This is not a step forward for the game.** `functions_entered` went **3 → 1**. `sce_FindAddress`
is still called **154** times and still misses **102**. So:

- The memory-map bug was **real** and is **fixed**. Any guest data access through a KSEG0 pointer was
  silently reading the wrong 16 MB before; that class of bug is gone.
- The livelock *in name* changed; the guest is **not** further along, and it has fewer distinct
  program counters than before, so on this evidence the fix traded a livelock for an earlier spin.

**No claim is made that this unblocked the guest.** The next question is now much better posed
though: with the data in the right place, why does a 102-miss `FindAddress` still fail on a word
that is demonstrably inside the window at `0x35354`? That is candidate 2 — the scan — and it is now
isolated from the loader, which was not true before this dish.
