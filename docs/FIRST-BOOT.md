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
