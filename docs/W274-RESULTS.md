# W274 — the wakeup. Goal: `ExecPS2` fires and GT4's engine code runs.

**Date 2026-10-08.** Predecessor W273 (image placed, 0/5,339,668 mismatches vs the disc's zlib decode).

Captain's three verified results (independent check, Caine):
1. PASS — image placement sha256-identical to hardware dump + w231-engine.bin.
2. FAIL — the zlib `decompressobj(-15)` shortcut does NOT reproduce (6,119,116 vs 6,118,856; 260 B over). Corrected below.
3. CONFIRMED — the real blocker is the link: the harness carries ZERO generated engine functions.

## Record correction (per captain, W273 zlib claim)
The W273 record stated `zlib.decompressobj(-15)` over `CORE.GT4[6:]` reproduces the image. Independent
decode yields 6,119,116 bytes (260 more than the 6,118,856 container size on record), and the
5,339,668-byte image does not align as a plain slice. The claim that HOLDS: the RDRAM dump is
sha256-identical to `hw_engine.bin` / `w231-engine.bin`. The zlib shortcut is retracted until a
command proves it.

## Seat findings (appended as they land)

### T1 — builder — full 19,404 engine RELINKED + link PROVEN (2026-10-08)

Rebuilt the harness against the W251 authoritative engine. Link took. Raw: `/mnt/ssd/tmp/w274-link.txt`.

- Command: `VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_w251 bash tools/harness/build_harness.sh`
- `build_engine.sh`: **0 unit(s) to (re)build** (the 47 `.o` in `recomp_engine_w251/` were already compiled).

| | value |
|---|---|
| BEFORE count (`nm -C … ' T (entry_\|sub_)'`) | **1481** = 707 loader `sub_` + 16 loader `entry_` + 400 small-engine `sub_` + 358 small-engine `entry_` |
| AFTER count (`nm -C`, working proof) | **20127** |
| AFTER `sub_` only (`nm -C`) | **20111** = 19,404 engine + 707 loader |
| AFTER `entry_` only (`nm -C`) | **16** (all loader) |
| AFTER `nm` no `-C` → ` T _Z21sub_` | **19404** (matches w251 header exactly) |
| AFTER captain's literal (no `-C`) | **0** — symbols mangled (`_Z21sub_…`), byte after ` T ` is `_`, not `s` |
| Binary size BEFORE | 43,916,736 B (44 MB, recomp_engine_small) |
| Binary size AFTER | **347,718,056 B** (331.6 MiB / ~347 MB) |
| sha256 AFTER | `7983f9522833cf5634087aa8ed7fd9a42170da4eff0ac12d1fe6beff09ff2a55` |
| Errors | **NONE** (clean relink) |

**Captain's `nm` gotcha (honest correction):** his command lacks `-C`; it returns 0 even when the link
is perfect. The working proof is `nm -C`. Symbols are NOT to be renamed.
**Measured correction to the task text:** the claim "there are NO `entry_` symbols" is FALSE — the
**loader** emit has 16 ` T entry_*` functions (e.g. `entry_1017868_0x10178d8`). The **W251 engine**
emit has 0 `entry_`. So the `-C` total is 20,127; 20,111 is only the `sub_`-only count.

BLOCKER CLEARED for the engine-entry dish: the binary under `run/vulcan4_harness` is now the full
19,404-function W251 engine. Do NOT boot it in this dish (out of scope) — the next dish measures
"engine entered" against the right engine now.

---

### T2 — builder — syscall 0x5B: what it IS, where 0x00075000 comes from, and the EXACT stub (STAGED — not built, not committed, no patch yet)

**Status of this section:** static work only. Nothing below has been applied to the tree. `tools/PS2Recomp`
was NOT touched, no `.h` was touched, no generated `ps2_recompiled_functions*.cpp` was touched, nothing was
built, nothing was committed, and no patch was created (law 8 capture is written out in §6 as the *next*
step for whoever applies this). Every number below was measured on this box on 2026-10-08.

---

#### 1. Deliverable 1 — 0x5B IS `GetEntryAddress`. `GetThreadTLS` is the corpus defect.

The conflict is in the corpus itself, in one file, and the same file is already known-wrong one line above:

`~/.config/opencode/skills/ps2-recomp-Agent-SKILL/resources/db-syscalls.md`

```
124:| 0x5A | QueryBootMode   | ... |   <- wrong
126:### Handler Enable/Disable (0x5B–0x5F)
129:| 0x5B | GetThreadTLS    | a0=tid, $v0=tls_ptr | ... |   <- wrong
195:... QueryBootMode(0x5A) ... SetSyscall(0x74) ...      <- 0x74 right, 0x5A/0x5B wrong
```

**Resolution, cited:** ps2sdk — the source of truth for EE syscall numbering — declared in
`ee/kernel/src/setup_syscalls.S`, where the wrapper is literally

```asm
GetEntryAddress:
    li  $v1, 0x5B
    syscall
    jr  $ra
```
and used by `ee/kernel/src/libosd.c`:

```c
SyscallPatchEntries[] = { {0x5A, &kCopy}, {0x5B, &kGetEntryAddress}, ... };
/* for each entry: *(uint32_t *)GetEntryAddress(n) = my_handler; */
```

That pair of citations is already on this box, recorded in `docs/CODE-REVIEW-2026-10-04.md` **F-3** and in
`docs/CHANGELOG-2026-10-04-NLOOP-EOP.md`, with the standing instruction *"Do not 'fix' the dispatcher toward
that corpus file."* `db-syscalls.md`'s own 0x5A entry (`QueryBootMode`) is disproved by the same F-3, and by
GT4's own wrapper (below) — `0x5A` is `Copy`, taking dest/src/size. So the corpus table's 0x5A/0x5B rows are
one defect, not two facts to reconcile.

**Second, independent ground — GT4's own code.** The loader (`SCUS_973.28`) has three thin wrappers that each
perform one `syscall` and return `$v0`:

| wrapper VA | number | how GT4 uses it |
|---|---|---|
| `func_0102AA38` | `0x5A` | `a0`=dest, `a1`=src, `a2`=size — a byte copy |
| `func_0102AA9C` | `0x5B` | one number in (`a0`), one **address** out (`$v0`), fed straight to `SetSyscall` as a handler |
| `func_0102AA80` | `0x74` | `a0`=index, `a1`=address (`SetSyscall`) |

`0x5B`'s result being used *as a syscall handler address* (see §2) is `GetEntryAddress` semantics and cannot
be `GetThreadTLS` (a TLS pointer is not a code address).

**`GetThreadTLS` in our runtime is an unwired stray, not a competitor.** Measured today:
`grep -rn GetThreadTLS ps2xRuntime/src` returns exactly two lines — the declaration
(`System.h:35`) and the definition (`System.cpp:1175`, comment "stub: return 0"). There is **no** call site
anywhere, in particular none in `Dispatcher.cpp`. It is dead code. It is not reached for 0x5B and never was.

---

#### 2. Deliverable 2 — where RDRAM `0x00075000` comes from (raw bytes, both directions)

The handler is produced **entirely by the loader**, by the classic self-relocating patch. One function,
`sub_0102AA90` (VA `0x0102AA90`, re-dumped this session):

```
0x0102aaa0: addiu s2,zero,3          ; loop counter 3 -> 8 = 5 iterations
0x0102aaac: addiu s0,v0,0x6680       ; s0 = 0x01036680  (pair list)
0x0102aab0: lw    v0,0x6680(v0)      ; pairs[0].num
0x0102aab4: addiu s1,s0,0x18         ; s1 = 0x01036698  (loop table)
0x0102aab8: jal   func_0102AA80      ; SetSyscall
0x0102aabc: lw    a1,4(s0)     (ds)  ; SetSyscall(0x5A, 0x0102AA38)
0x0102aac0: lui   a1,0x0103
0x0102aac4: lui   a0,0x8007
0x0102aac8: addiu a2,zero,0x330
0x0102aacc: addiu a1,a1,0x6300       ; a1 = 0x01036300   (src)
0x0102aad0: jal   func_0102AA38      ; syscall 0x5A = Copy
0x0102aad4: ori   a0,a0,0x5000 (ds)  ; a0 = 0x80075000   (dest), a2 = 0x330
0x0102aad8: jal   func_0101F6A0      ; cache flush(0)
0x0102aae0: jal   func_0101F6A0      ; cache flush(2)
0x0102aae8: lw    a0,8(s0)           ; a0 = pairs[3].num = 0x5B
0x0102aaec: jal   func_0102AA80      ; SetSyscall
0x0102aaf0: lw    a1,0xC(s0)   (ds)  ; a1 = pairs[3].handler = 0x80075000
0x0102aaf4: lw    a0,0x10(s0)        ; 0x54
0x0102aaf8: jal   func_0102AA80      ; SetSyscall(0x54, 0x0102AFC0)
0x0102aafc: lw    a1,0x14(s0)  (ds)
0x0102ab00: lw    a0,0(s1)           ; loop: n = *s1
0x0102ab08: jal   func_0102AA9C      ; n2 = syscall 0x5B (GT4's own, now installed) = GetEntryAddress(n)
0x0102ab0c: addiu s2,s2,1      (ds)
0x0102ab10: lw    a0,0(s1)
0x0102ab14: daddu a1,v0,zero
0x0102ab18: jal   func_0102AA80      ; SetSyscall(n, GetEntryAddress(n))
0x0102ab1c: addiu s1,s1,8      (ds)
0x0102ab20: sltiu v0,s2,8
0x0102ab24: bnez  v0,0x0102ab00
0x0102ab28: lw    a0,0(s1)     (ds)
0x0102ab2c: jal   func_0102AA9C      ; v0 = GetEntryAddress(3)
0x0102ab30: addiu a0,zero,3    (ds)
0x0102ab48: sw    v0,0x6678(v1)      ; stored to 0x01036678
```

**Data, read from the file (not inferred):**

```
0x01036680: 0000005a 0102aa38     ; {0x5A, 0x0102AA38}   Copy
0x01036688: 0000005b 80075000     ; {0x5B, 0x80075000}   <- the handler in question
0x01036690: 00000054 0102afc0     ; {0x54, 0x0102AFC0}
0x01036698: 00000055 ...          ; loop table, stride 8: 0x55 0x56 0x57 0x58 0x59 (5 = 8-3 iterations)
0x01036600: 00000055 80075038     ; the blob's OWN table, inside the copied 0x330 bytes
0x01036608: 00000056 800750c8
0x01036610: 00000057 80075108
0x01036618: 00000058 80075158
0x01036620: 00000059 800751a8
0x01036628: 00000003 80075330
```

So: **blob source = loader VA `0x01036300` (file offset `0x37300`)**, `0x330` bytes, copied by the guest's own
`Copy` (syscall `0x5A`) to RDRAM `0x00075000`; installed with `SetSyscall(0x5B, 0x80075000)`; the blob's own
table is at blob+`0x300` = RDRAM `0x00075300`; the loop then installs the blob's five TLB handlers for
syscalls `0x55`–`0x59`.

The blob's 0x5B handler (RDRAM `0x00075000`) is a **pure 6-entry lookup**:

```
80075000: lui   v0,0x8007
80075004: daddu a1,zero,zero
80075008: addiu v1,v0,0x5300        ; v1 = 0x80075300 (table, blob+0x300)
80075010: lw    v0,0(v1)            ; table[i].number
80075014: bne   a0,v0,0x80075024
80075018: addiu a1,a1,1     (ds)    ; i++
8007501c: jr    ra
80075020: lw    v0,4(v1)    (ds)    ; MATCH -> return table[i].handler
80075024: sltiu v0,a1,6
80075028: bnez  v0,0x80075010
8007502c: addiu v1,v1,8     (ds)
80075030: jr    ra
80075034: daddu v0,zero,zero (ds)   ; NO MATCH -> return 0
```

**The handler is loader-only. Measured this session (fresh, little-endian patterns):**

| file | bytes | blob signature `3c028007 0000282d 24435300` | literal `0x80075000` |
|---|---|---|---|
| `SCUS_973.28` (loader) | 273,020 | **1** — at file `0x37300` = VA `0x01036300` | **1** — the data word at `0x0103668C` |
| `w231-engine.bin` (engine image) | 5,339,668 | **0** | **0** |
| `recomp_engine_w251/*.cpp` (48 files) | — | **0** | **0** |

Two consequences, both load-bearing:
1. `0x00075000` is **below the engine image base** (`0x00100000`) and below the loader base (`0x01000000`),
   so `runtime->hasFunction(0x80075000)` can never be true — the W250 LOUD line is correct and permanent for
   this handler. Nothing in either recompile will ever claim it.
2. It is therefore **raw guest code in RDRAM**, and the only honest emulation is the runtime reproducing the
   *semantics we read out of the guest's own bytes* — which is exactly what §3 does.

---

#### 3. Deliverable 3 — the exact stub (option **b**)

**File:** `tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp`
**Header change:** **none.** Every identifier used (`getConstMemPtr`, `getRegU32`, `setReturnU32`,
`setReturnS32`, `KE_ERROR`, `std::memcpy`, `std::cerr`) is already used in this same translation unit. No `.h`
is touched, so **no full rebuild is triggered** (only `System.cpp` recompiles + relink).

**Insert A — the helper, immediately above `bool dispatchSyscallOverride(` (currently line 439):**

```cpp
    // ---------------------------------------------------------------------
    // W274: GT4's OWN GetEntryAddress, installed over kernel syscall 0x5B.
    //
    // The guest never calls the kernel's GetEntryAddress here. GT4's loader
    // (SCUS_973.28, sub_0102AA90) copies a 0x330-byte blob in with its own Copy
    // syscall and patches the table:
    //
    //   Copy(dest=0x80075000, src=0x01036300, size=0x330)  @ 0x0102AAD0
    //   SetSyscall(0x5B, 0x80075000)                       @ 0x0102AAE8/E0+F0
    //   for n in {0x55,0x56,0x57,0x58,0x59}:               @ 0x0102AB00
    //       SetSyscall(n, GetEntryAddress(n));
    //
    // The blob's 0x5B handler (RDRAM 0x00075000) is a pure table LOOKUP: it scans a
    // 6-entry {number,handler} table at RDRAM 0x00075300 and returns the paired word
    // (0 when the number is absent). That is NOT the kernel primitive, which returns
    // the ADDRESS of the writable syscall-table slot 0x80011F80+n*4 for the caller to
    // write through. Returning the slot address here would make the loader install a
    // table slot as a syscall handler -- the next call to that syscall would jump into
    // a data word. So the builtin case 0x5B cannot be reused; this is a different
    // primitive and the guest must keep its own.
    //
    // The table is read LIVE out of rdram, never baked in (law 1) and never re-derived
    // from our own copy of the game: the values are whatever the guest's Copy put there.
    // If the table is not there, we refuse loudly instead of inventing an answer.
    // ---------------------------------------------------------------------
    static bool emulateGuestGetEntryAddress(uint8_t *rdram, R5900Context *ctx,
                                           uint32_t syscallNumber, uint32_t handler)
    {
        constexpr uint32_t kGt4Syscall = 0x5Bu;
        constexpr uint32_t kGt4HandlerPhys = 0x00075000u; // 0x80075000 & 0x1FFFFFFF
        constexpr uint32_t kGt4TableAddr = 0x80075300u;   // blob+0x300
        constexpr uint32_t kGt4TableCount = 6u;           // blob scans exactly 6 entries

        // Strictly scoped: ONLY GT4's blob, ONLY on 0x5B. Anything else falls through
        // untouched to the LOUD W250 limitation in the caller.
        if (syscallNumber != kGt4Syscall || (handler & 0x1FFFFFFFu) != kGt4HandlerPhys)
        {
            return false;
        }

        static bool announced = false;
        if (!announced)
        {
            announced = true;
            std::cerr
                << "VULCAN 4 LIMITATION: syscall 0x5B is emulated as GT4's OWN GetEntryAddress "
                   "(guest blob installed by SetSyscall(0x5B,0x80075000), Copy source 0x01036300), NOT "
                   "the kernel primitive that returns the writable syscall-table slot 0x80011F80+n*4. "
                   "Emulated: scan GT4's 6-entry {number,handler} table at RDRAM 0x00075300, return the "
                   "paired handler, 0 when the number is absent. Table read live from rdram."
                << std::endl;
        }

        const uint8_t *table = getConstMemPtr(rdram, kGt4TableAddr);
        if (!table)
        {
            std::cerr << "VULCAN 4 LIMITATION: syscall 0x5B (GT4 GetEntryAddress) — RDRAM 0x"
                      << std::hex << kGt4HandlerPhys << std::dec
                      << " region is not mapped; cannot look up an entry. Returning KE_ERROR."
                      << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return true;
        }

        uint32_t firstNumber = 0u;
        std::memcpy(&firstNumber, table, sizeof(firstNumber));
        if (firstNumber == 0u)
        {
            // Asked before GT4's own Copy(0x80075000, 0x01036300, 0x330) ran, or that Copy never ran.
            // Do not invent an address.
            std::cerr << "VULCAN 4 LIMITATION: syscall 0x5B (GT4 GetEntryAddress) — GT4's entry table at "
                         "RDRAM 0x00075300 is empty, so GT4's own blob was never copied there. Returning "
                         "KE_ERROR exactly as before this stub (was the W250 line)."
                      << std::endl;
            setReturnS32(ctx, KE_ERROR);
            return true;
        }

        const uint32_t wanted = getRegU32(ctx, 4); // $a0 = syscall number to resolve
        for (uint32_t i = 0u; i < kGt4TableCount; ++i)
        {
            uint32_t number = 0u;
            uint32_t entry = 0u;
            std::memcpy(&number, table + (i * 8u), sizeof(number));
            std::memcpy(&entry, table + (i * 8u) + 4u, sizeof(entry));
            if (number == wanted)
            {
                setReturnU32(ctx, entry);
                return true;
            }
        }

        setReturnU32(ctx, 0u); // GT4's blob returns 0 for "no such entry"
        return true;
    }
```

**Insert B — the call, inside `dispatchSyscallOverride`, between the `hasInvocation` early return and the
`if (!runtime->hasFunction(handler))` block:**

```cpp
        // W274: an override target can legitimately have no generated function when the guest
        // installed its OWN blob below both images (GT4 does exactly this for 0x5B). Emulate the
        // ones we have read out of the guest; everything else keeps hitting the W250 line below.
        if (emulateGuestGetEntryAddress(rdram, ctx, syscallNumber, handler))
        {
            return true;
        }
```

Placement note: the helper is `static` and must be **above** `dispatchSyscallOverride` (Insert A's anchor).
Insert-before-`hasFunction` order matters: the `hasFunction` branch is the one that returns `KE_ERROR` today,
so the stub has to be reached first.

**What the two LOUD lines say, plainly:** (i) at first use — 0x5B is being emulated as *GT4's own* lookup,
not the kernel primitive, with the guest addresses named; (ii) if the guest's blob was never copied — an
explicit refusal + the old `KE_ERROR`. Both fire before any guest code is "served". No silent path.

**Explicitly NOT claimed by this stub:** the six handlers *inside* the blob
(`0x80075038`, `0x800750C8`, `0x80075108`, `0x80075158`, `0x800751A8`, `0x80075330`) are **not** emulated. The
stub returns their addresses exactly as the guest's own table does — it does not pretend they run. See §5.

---

#### 4. Why option (b), and why option (a) is not "an approximation" — it is a wrong pointer

Option (a) = let the override fall through to the builtin `case 0x5B:` (`Dispatcher.cpp:228`) so our
`GetEntryAddress` runs. Our builtin returns `kGuestSyscallTableGuestBase + n*4` = `0x80011F80 + n*4`.

The guest's *next* act (measured, §2, `0x0102AB00`–`0x0102AB1C`) is `SetSyscall(n, <that value>)` for
`n ∈ {0x55,0x56,0x57,0x58,0x59}`. Under option (a):

* `n = 0x55` → returns `0x80011F80 + 0x154` = **`0x800120D4`**
* `SetSyscall(0x55, 0x800120D4)` writes `0x800120D4` **into the syscall slot at `0x800120D4`**
  (`setEeSyscallOverride` writes at `0x80011F80 + n*4`)
* any later call to syscall `0x55` would jump to `0x800120D4` — a syscall-table **data word** whose content is
  the address of itself — and execute it as MIPS.

That is not a fidelity gap, it is a jump into a data table, i.e. guaranteed corruption a few instructions
later. And it is not even the kernel's own contract: per the cited ps2sdk `libosd.c`, the kernel primitive's
result is meant to be **written through** (`*(uint32_t *)GetEntryAddress(n) = handler`), whereas GT4's blob
returns a **handler to be installed**, i.e. `SetSyscall(n, GetEntryAddress(n))`. Same syscall number, two
different primitives, and GT4 replaced ours with theirs. Option (b) reproduces the one the guest actually
installed; option (a) reproduces neither and would table-jump.

---

#### 5. The next wall this stub walks into (named now, not after the fifth run)

As soon as this stub returns, the same loader loop installs the blob's five handlers:

`SetSyscall(0x55, 0x80075038)`, `0x56→0x800750C8`, `0x57→0x80075108`, `0x58→0x80075158`, `0x59→0x800751A8`.

Each of those will hit the still-live W250 line (`override handler 0x… has no generated function in either
image`), because they are in the same low-RAM blob. Disassembled, they are EE **TLB/cache kernel
primitives** — `mtc0` pagemask/entryhi/entrylo0/entrylo1, `tlbwr`, `tlbwi`, `tlbr`, `tlbp`, `mfc0 index` —
and the blob's `0x59` route (`0x800751A8`) is a larger routine that internally calls the `0x58` one.
**The runtime has no TLB model at all** (grep for `tlbwi|tlbp|TlbWrite|tlbwr` over `ps2xRuntime/src` and
`include/` returns nothing), so these are a separate dish (a TLB model + those five primitives), not a
one-liner. This dish should not be asked to fake them.

**Open question, reported not guessed:** the blob's table maps `3 → 0x80075330`, which is exactly one byte
past the end of the copied `0x330` bytes (`0x80075000 + 0x330 = 0x80075330`), and the loader bytes at
`0x01036630` are zero. So `GetEntryAddress(3)` returns an address that no loader write populates. The loader
does call it once (`0x0102AB2C`, result stored to `0x01036678`), so it is a real value the guest keeps. We do
not know what is supposed to live there — likely a tail/workspace of the blob. The stub returns `0x80075330`
for `a0=3` exactly as the guest's own bytes do, which is faithful, and we flag the rest as unknown.

**Also not verified (§6 of the record, not of the stub):** a second install site exists —
`{0x5B, 0x80074000}` at `0x01035B18`, with `Copy(dest=0x80074000, src=0x01035368, size=0x7A8)` at
`0x010288F4`. The handler the runtime actually observed was `0x80075000`, so that is what this stub covers.
A scan of the `0x7A8`-byte `0x80074000` blob found **no** `{number,handler}` pair table and **no** `0x5B`
lookup shape inside the copied range, so it is a different object; if the guest ever installs it over 0x5B,
the W250 line will fire unchanged (honest refusal, no guess).

---

#### 6. Law 8 capture — exact commands for whoever applies this (DO NOT run before applying)

`System.cpp` is **clean in the nested repo today** (verified: `git diff --numstat` in
`tools/PS2Recomp` lists only `control_flow_emitter.cpp 22`, `instruction_translator.cpp 32`,
`ps2_runtime_macros.h 90`, `ps2_runtime.cpp 921` — the W274 live snapshot already captured by commit
`2378c7a`). So the patch must carry **only** this file:

```bash
cd /home/or/vulcan4/tools/PS2Recomp
git diff --numstat -- ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp     # ground truth: added lines
git diff     -- ps2xRuntime/src/lib/Kernel/Syscalls/System.cpp \
    > ../patches/ps2recomp-linux-w274-0x5b-getentryaddress.patch
# THE CHECK THAT IS NOT OPTIONAL (per AGENTS.md law 8):
grep -c '^+' ../patches/ps2recomp-linux-w274-0x5b-getentryaddress.patch  # must be >= the numstat added count
cd /home/or/vulcan4 && git add tools/patches/ && git commit -m "W274: stub 0x5B as GT4's own GetEntryAddress (static half)"
```

#### 7. Traps for the applier (all measured here)

* **Do not mask with `PS2_RAM_MASK`** for the KSEG0 strip: in this tree `PS2_RAM_SIZE == 32MB`, so
  `PS2_RAM_MASK == 0x01FFFFFF` (`include/runtime/ps2_memory.h:26-27`) — it is a 32MB *offset* mask, not the
  `0x1FFFFFFF` alias mask the dispatcher path uses at `ps2_memory.h:137`. Use the literal `0x1FFFFFFFu` for
  the handler compare (as written above), and pass the guest address **unmasked** to `getConstMemPtr` — its
  own `ps2ResolveGuestPointer` turns `0x80075300` into `rdram + 0x75300`, which is the same byte the guest's
  `Copy` wrote.
* **Keep the scoping exact** (`syscallNumber == 0x5B && (handler & 0x1FFFFFFF) == 0x00075000`). A stub that
  fires for any unbacked override would hide the W250 signal that is doing real work elsewhere.
* **No `.h`, no generated file.** The helper is `static` inside `namespace ps2_syscalls` in `System.cpp`;
  `System.h` needs no change, and the generated `ps2_recompiled_functions*.cpp` are untouched by construction.
* **Off by default?** This is not a feature probe; it is the honest emulation of a handler the guest installs
  itself, and it replaces a path that today prints LOUD and returns `KE_ERROR`. It therefore changes behaviour
  only *after* the guest has issued `SetSyscall(0x5B, 0x80075000)` — i.e. never for a run that does not reach
  that loader function. The only new output on an unmodified-until-then run is the once-per-process LOUD line.

---

### T3 — measurer (oracle) — the three W274 questions, answered by PCSX2 DebugServer

Oracle: `pcsx2-qt -debugger` (custom DebugServer build), SCUS-97328 GT4 USA v2.00, live this whole
session — **no relaunch was needed** (`pcsx2_status` first call: DebugServer connected, Pine
connected, EE PC=0x00081fc0, Paused). Every number below is a `pcsx2_*` read, not inference.

#### D1 — who calls `SignalSema` with id 7

Breakpoint `0x0101f440` (the SignalSema stub: `li v1,66; syscall; jr ra; nop`) with
`condition="a0 == 7"` — **fired, cycles 116383141**:

| reg | value | meaning |
|---|---|---|
| `a0` | `0x00000007` | the semaphore id |
| `ra` | **`0x0100B2C4`** | return site: `jal 0x0100AE48` at `0x0100B2BC` |
| `sp` | `0x01FFFB10` | frame of the *waiter* too (same frame) |
| `a2` | `0x70000000` | |

**Signaller = `0x0100B2BC`, inside function `0x0100B1C8`.** That function is a **semaphore-guarded DMA
transfer** — signature `(a0 = slot, a1 = ?, a2 = byte length, a3 = source RAM address)`; it builds a
DMA tag chain in its own `sp-0x110` frame, `sync`, `FlushCache(0)` (`jal 0x101f6a0`), then:

```
0x0100B2A0  jal 0x0100AE18    ; WaitSema(slot)    ra = 0x0100B2A8   <- the waiter
0x0100B2AC  jal 0x0100DE58    ; DMA kick(slot, chain)
0x0100B2B4  jal 0x0100AE78    ; wait for DMA completion
0x0100B2BC  jal 0x0100AE48    ; SignalSema(slot)  ra = 0x0100B2C4   <- THE SIGNALLER
0x0100B2CC  jr ra / addiu sp,sp,0x110
```

**Where the 7 comes from — it is NOT an immediate.** The wrappers `0x0100AE18`/`0x0100AE48` compute
`a0 = *(0x70002000 + slot*12 + 0x74)`; with `slot = a0 = s0 = 0` that is **`*(0x70002074)` in EE
scratchpad RAM**. So `WaitSema`/`SignalSema` id 7 is read out of the scratchpad descriptor table, and
on hardware it really is 7 there. It is a **lock/unlock pair inside one function**, always the same
frame, and the WaitSema did **not** block (only ONE `SignalSema(a0==7)` ever fired, 3450 cycles later).

Callers of `0x0100B1C8` (loader init): `0x0100A534` with `a0=0, a3=0x01040200, a2=2688` and
`0x0100A554` with `a0=1, a3=0x0102DE00, a2=16368`.

**INTC/interrupt path: NO.** This is an ordinary guest counting-semaphore lock. `irq_attach=0` in our
run is therefore *not* implicated by D1 — sema 7 is not an interrupt-wakeup semaphore at all.

**Contradiction with the task's static claim (recorded, not smoothed over).**
`pcsx2_read_memory(0x01047A80, u32_array)` on hardware = `0x0000000a, 0x00000001, 0x00000001,
0x01051a40, 0, …`. `FUN_01000940` stores its `CreateSema` return at `0x01047A80` (`sw v0,31360(v1)`,
`v1=0x01040000`). Hardware's value there is **10, not 7**. So on hardware `FUN_01000940` created
semaphore **10**; the "7" the lead's static pass names is a *second*, different semaphore that lives in
the EE scratchpad. Two distinct semaphores share the id-space and ours has them crossed.

#### D2 — what `0x80075000` is (and `0x00075000` cannot be read directly)

**Gotcha first:** `pcsx2_read_memory(0x00075000)` returns **all zeros** even when the blob is present.
Read the KSEG0 alias `0x80075000` instead.

`pcsx2_disassemble(0x80075000, count=110)` — PCSX2's own disassembler, live:

* `0x80075000` = **GetEntryAddress (syscall 0x5B)**, a pure **6-entry lookup** over the table at
  `0x80075300`: `lui v0,0x8007; dmove a1,zero; addiu v1,v0,0x5300`; loop `lw v0,(v1); bne a0,v0`
  (delay `addiu a1,1`); match → `jr ra` with delay `lw v0,4(v1)`; `sltiu v0,a1,6; bnez`; miss →
  `jr ra` with `dmove v0,zero`.
  **Contract: `GetEntryAddress(a0 = number) → v0 = paired handler address, 0 if absent.`**
* Live table at `0x80075300` (u32 pairs read from hardware): `0x55→0x80075038`,
  `0x56→0x800750c8`, `0x57→0x80075108`, `0x58→0x80075158`, `0x59→0x800751a8`,
  `0x03→0x80075330`; and `0x80075330` holds the argv pointer array
  `{0x80075370, 0x80075371, 0x80075384, 0}` whose strings are `"cdrom0:\CORE.GT4;1"` and `"hot"`.
* `0x80075038` **TLBWR (0x55)**: segment guard on EntryHi `(a1>>24)&0xF0` against
  0x10/0x20/0x30/0x40/0x50, invalid → `li v0,-1`; valid → `mtc0 PageMask/EntryHi/EntryLo0/EntryLo1;
  SYNC; tlbwr; SYNC; tlbp; SYNC; mfc0 v0,Index; jr ra`.
  **Contract: `TLBWR(PageMask=a0, EntryHi=a1, EntryLo0=a2, EntryLo1=a3) → v0 = Index, or -1.`**
* `0x800750c8` **TLBWI (0x56)**: `sltiu v0,a0,0x30`, else `li v0,-1`; valid → `mtc0 Index/PageMask/
  EntryHi/EntryLo0/EntryLo1; tlbwi; jr ra` delay `dmove v0,a0`.
  **Contract: `TLBWI(Index=a0<0x30, PageMask=a1, EntryHi=a2, EntryLo0=a3, EntryLo1=t0) → v0 = a0 or -1.`**
* `0x80075108` **TLBR (0x57)**: `mtc0 Index; tlbr; mfc0 PageMask→(a1), EntryHi→(a2), EntryLo0→(a3),
  EntryLo1→(t0); jr ra` delay `dmove v0,a0`.
* `0x80075158` **TLBP (0x58)**: `mtc0 EntryHi; tlbp; mfc0 Index; bgez`; hit → `tlbr`, then
  PageMask→(a1), EntryLo0→(a2), EntryLo1→(a3); `jr ra` delay `dmove v0,a0`. `-1` on miss.
* `0x800751a8` **0x59**: larger prologue (`addiu sp,-0x30; sd s0,0x10(sp); dmove s0,a0;
  andi v0,s0,0x0FFF`) — a composite TLB helper, not fully disassembled.

This **confirms the builder's T2 static read byte-for-byte** (same lookup shape, same table, same
handlers, same contracts). No amendment needed to T2 §2–§4.

#### D3 — does hardware park on sema 7? NO. And hardware's real parks are inside the ENGINE.

Hardware timeline of the loader phase (all from breakpoints on hardware):

| cycle | event |
|---|---|
| 116379691 | `WaitSema` stub entry, `a0 == 7` |
| 116383141 | `SignalSema` stub entry, `a0 == 7`, `ra=0x0100B2C4` — **the only one ever** |

Exactly one `continue` separates them. So at `0x0100B2A0` hardware **did not block** (count > 0, the
3450-cycle gap is the DMA work), and the loader then calls `ExecPS2(0x00100008,
argv={"cdrom0:\CORE.GT4;1","hot"})` and the EE restarts at the engine entry.
At the engine-entry stop hardware's `TID 1: PC=0x00100008`.

**Hardware's real park is in the ENGINE image, not on the loader's sema 7.** Two thread-table reads:

* earlier: 7 threads — TID 1 `0x005adce8` w=1; TID 4/6/8 `0x005adce8` w=1; TID 5/7 `0x005adbc8` w=2
* later (cycles 1211540030): **20 threads** — TID 1 moved to `0x005adbc8` w=2; TID 4/6/8/10/13/20 at
  `0x005adce8` w=1; TID 5/7/9/11/12/14/15/16/18/19/21/22 at `0x005adbc8` w=2

Both PCs are the `jr ra` of stubs in the ENGINE's **own** syscall table (`0x005ADB60..0x005ADCFC`,
the same `li v1,N; syscall; jr ra; nop` 16-byte pattern as the loader's `0x0101F4xx` table), verified
byte-level with `pcsx2_read_memory(0x005ADCE0, u32_array) = 0x24030044, 0x0000000c, 0x03e00008, 0`:

* **`0x005ADCE0 = li v1,0x44` (68) = `WaitSema`** — `waitType=1`
* **`0x005ADBC0 = li v1,0x32` (50) = `SleepThread`** — `waitType=2`

Syscall names are PCSX2's own authoritative table (`pcsx2/R5900OpcodeImpl.cpp:85` `bios[256]`), not
inference: index 0x44 = "WaitSema", 0x32 = "SleepThread".

**Hardware is alive and progressing, not wedged:** between the two reads the EE thread count went
7 → 20 and TID 1 moved from the WaitSema stub to the SleepThread stub. This is the answer to W250's
"PCSX2 was wedged at EE kernel idle pc=0x81FC0" — `0x81FC0` **is** the healthy idle thread (TID 0,
status=1); a wedged-looking `pc=0x81FC0` is just the kernel idle loop, and the game's real threads are
the ones parked at `0x005ADxxx`.

#### The first divergence this seat can name

Same instruction, both sides: **the WaitSema reached at loader `0x0100B2A0`, stub `0x0101F460`, with
`a0 = 7`.**

* **hardware**: returns immediately (count > 0) → `SignalSema(7) @0x0100B2BC`, `ExecPS2`, engine runs,
  20 engine threads.
* **ours**: blocks forever — the harness is parked on `WaitSema pc=0x0101f468, a0=7, woken=0`, and the
  engine image is entered **0 times** (W251 measurement, unchanged).

But the *identity* of semaphore 7 differs, and that is measurable: hardware's `FUN_01000940` sema is
**10** (`*(0x01047A80)`), while ours is **7**; hardware's sema 7 is the EE-scratchpad DMA guard
(`*(0x70002074)`) signalled at `0x0100B2BC`. **The earliest divergence is therefore upstream of the
park — it is the semaphore id-space: our `CreateSema`/`SignalSema` ids and/or the scratchpad slot
`0x70002074` do not match hardware's.** Compare ours and hardware at `CreateSema` return
(stub `0x0101F420`, `jr ra` at `0x0101F428`, `v0` = new id) before chasing anything after the park.

#### Note on the lead's follow-up ask (tid2's waker) — not measured, needs a boot re-run

The lead asked for the oracle to confirm who wakes tid2 (`WakeupThread` vs INTC). That cannot be read
from the current live state: hardware's `ExecPS2` **destroys the loader thread set**, so the loader's
tid2 no longer exists in the thread table. Answering it requires re-arming `0x0101f350`
(WakeupThread, `li v1,51`) and re-booting from the ELF entry. Named here rather than guessed.

#### CORRECTION — the first divergence is tid2's DMA-completion poll, not the id-space

The paragraph above ("The first divergence this seat can name") is **superseded by this measurement**.
It named the semaphore id-space; the run log locates a *behavioural* divergence one layer earlier, and
it is the one that actually explains the halt. Kept above rather than deleted, per the record rule.

Evidence, all from `/mnt/ssd/vulcan4-build/run/boot_w274b.log` (the W274b 20 s run, 6,337,791 B):

```
line 66206: halt=guest_blocked
  tid1:status=2:wait=sema#7:woken=0:pc=0x0101f468:ra=0x01000de8:invocations=0
  tid2:status=2:wait=sleep#0:woken=0:pc=0x0101f348:ra=0x0100afa8:invocations=0
  inv_by_kind=[intr=94,dmac=0,override=0,other=0]   serviced_invocations=130
line 66210: 0x32 sce_SleepThread calls=15576
  ra_count=0x0100b680x12652,0x0100afa8x2924     <- tid2 sleeps 2924x, ra=0x0100AFA8
line 66216: 0x44 sce_WaitSema calls=59
  ra_count=...,0x010009c8x1,0x01000de8x1        <- tid1's park is the one 0x01000de8 call
line 66215: 0x42 sce_SignalSema calls=57
  arg=0x0100b1a0/a0=5x25, 0x01000b74/a0=6x25, ..., 0x0100b2c4/a0=4x1, 0x0100b2c4/a0=5x1
```

Read those together with the loader disassembly (`FUN_01000940`, `FUN_01000DC0`, `0x01000BA0`,
`0x0100AF00`–`0x0100AFB4`):

* tid1's park `ra=0x01000de8` is **`FUN_01000DC0`'s own `WaitSema(*(0x01047A80))` at `0x01000DE0`** —
  i.e. it is waiting on exactly the semaphore `FUN_01000940` created at `0x010009B0` and took at
  `0x010009C0`. So the blocked wait and the created semaphore are the *same* id in our run (7); the
  id-space framing above is a real observation but it is **not** the cause of this halt.
* tid2's park `ra=0x0100afa8` is inside the **DMA-completion poll** at `0x0100AF50`. tid2 calls
  `SleepThread` from `0x0100AFA0` and returns to `0x0100AFA8`, then `0x0100AFAC bnezl v1,0x100af50`
  re-enters the poll — 2,924 times in 6.4 s.
* The decisive test is the DMA channel status bit **`0x100`** at `0x0100AF6C` (`beqz v0,0x100AF94`) and
  `0x0100AF8C` (`bnez v1,0x100AF78`), read from `*(0x01032DF8 + s5*4)`; the clear is the write-back at
  `0x0100AF80` (`sw v0,0(a0)` with `a1 = ~0x100`).
* Structurally, **tid2 must reach `0x01000D6C jal 0x101f440 SignalSema(*(0x01047A80))` to wake tid1.**
  It never leaves the poll, so it never signals; tid1 stays parked; the engine image is entered 0 times.
  Ours shows **`dmac=0`** — zero DMA events serviced — while hardware's loader completed the 640x448
  transfer kicked at `0x01000D60` (`jal 0x0100AB20`, a0=0, a1=1, a2=640, a3=448) and went on to
  `ExecPS2`.

**Named first divergence (final):**

| side | instruction | value |
|---|---|---|
| hardware | `0x0100AF6C beqz v0,0x100AF94` — bit `0x100` of `*(0x01032DF8 + s5*4)` | **0** → poll exits → tid2 reaches `0x01000D6C` |
| ours | same instruction, same address | **1 forever** → `dmac=0`, tid2 spins in `SleepThread`, never signals |

Hardware is the side that clears it. The divergence is **one bit of one DMA channel register**, and
everything downstream of it (tid1's park, `guest_blocked`, 0 engine entries) is symptom.

Secondary observation, recorded because it is measured and not the cause: our semaphore ids are
uniformly 3 lower than hardware's at the same call sites — DMA slot-0 guard ours 4 (`0x0100b2a8`/
`0x0100b2c4`) vs hardware 7, and `*(0x01047A80)` ours 7 vs hardware 10. W251's run had the DMA slot-0
id at 6, so our own ids drift run to run. Every id is loaded from memory, so a uniform shift is
harmless on its own; **do not chase it before the DMA bit.**

#### Instrument fact — the loader region is unreadable through the oracle after `ExecPS2`

`pcsx2_read_memory(0x01000D00, 32)` and `pcsx2_read_memory(0x01032DF8, 32)` both return all zeros in the
current post-`ExecPS2` state, while `0x005ADCE0` (inside the engine image) reads its real bytes. So on
this DebugServer build the loader's region `0x01000000..` is not readable once `ExecPS2` has run — the
same class of gotcha as "read `0x80075000`, not `0x00075000`". Loader static truth comes from the ELF
(`/tmp/w274m/loader.asm`), not from a live read. Any future oracle claim about loader code must be
taken *before* `ExecPS2` or from the ELF.

#### D4 — the register and the bit, NAMED (from the loader ELF, not inference)

The poll reads through `s4 = 0x01032DF8 + s5*4` (`0x0100AF3C`/`0x0100AF40`), i.e. a **3-entry pointer
table indexed by the DMA slot**. `0x01032DF8` lives in the loader ELF's second `PT_LOAD`
(`/mnt/ssd/gt4/work/SCUS_973.28`, vaddr `0x0102DC80`, filesz `0x13AA4`), so it is readable from the file
even though the oracle returns zeros for that region post-`ExecPS2`:

```
0x01032df8 = 0x10008000     <- slot 0
0x01032dfc = 0x10009000     <- slot 1
0x01032e00 = 0x1000a000     <- slot 2
0x01032e04 = 0
```

Those are the DMAC channel control registers — PCSX2 `Hw.h:178-193`:
`D0_CHCR = 0x10008000` (VIF0), `D1_CHCR = 0x10009000` (VIF1), `D2_CHCR = 0x1000A000` (GIF).

And the tested bit is **CHCR.STR**, exactly. PCSX2's own bitfield, `Dmac.h:113-121`:
`DIR:1, _reserved1:1, MOD:2, ASP:2, TTE:1, TIE:1, STR:1` → **STR is bit 8 = `0x100`**, with PCSX2's
comment on it: *"Start. 0 while stopping DMA, 1 while it's running."*

So `0x0100AF6C andi v0,v0,0x100` is literally **"is this DMA channel still running?"**, and
`0x0100AF80 sw v0,0(a0)` with `a1 = ~0x100` is the *force-stop* branch PCSX2 documents at
`Dmac.cpp:223-231` (*"The DMA may not stop properly just by writing 0 to STR"*).

**The first divergence, fully named:**

| | |
|---|---|
| hardware's value | `D0_CHCR.STR` (bit 8 of `0x10008000`) = **0** — transfer finished, hardware clears STR |
| ours | same bit = **1** forever; `inv_by_kind=[...dmac=0...]` — no DMA is ever serviced |
| first instruction they disagree at | **`0x0100AF6C andi v0,v0,0x100`** in the loader, then the branch it feeds, `0x0100AF6C beqz v0,0x100AF94` |

Hardware is the side that clears it. Everything after — `0x0100AFA8`'s re-entry, the 2,924
`SleepThread`s, `tid1` parked at `0x01000DE0`, `halt=guest_blocked`, engine entered 0 times — is
symptom. **Fix the EE DMAC (serve the transfer kicked at `0x01000D60`, channel 0), not the semaphore
ids and not the scheduler.**

### T4 — builder — Seat findings — angle 3 (kernel audit) (2026-10-08)

READ-ONLY audit. No build, no commit, no source change. Scope: our own hand-written runtime,
`tools/PS2Recomp/ps2xRuntime/` (fair game — not the game, not the oracle).

**Answer, in one line: no path in our runtime ever increments semaphore 7 in this run, and the
semaphore implementation itself is correct — the counter is 0 because the guest never reaches a
`SignalSema(7)` call site. The wall is upstream, in the EE DMAC. Angle 3 CONFIRMS the measurer's D4.**

---

#### 1. Where it lives (file : function : line — all verified by grep this session)

| what | site |
|---|---|
| syscall dispatch | `src/lib/Kernel/Syscalls/Dispatcher.cpp:14` `dispatchNumericSyscall` — 0x40 @156, 0x42 @165, iSignalSema 0x43 @168, 0x44 @171, 0x45 @174, 0x47 @180; 0x32 SleepThread @124, 0x33 WakeupThread @127, 0x35 @133; 0x16 EnableDmac @43, 0x12 AddDmacHandler @31 |
| CreateSema (0x40) | `Syscalls/Sync.cpp:69` → `EeScheduler::createSemaphore` `EeScheduler.cpp:942` |
| SignalSema (0x42) | `Sync.cpp:99` → `signalSemaphoreImpl` `Sync.cpp:33` → `EeScheduler::signalSemaphore` **`EeScheduler.cpp:987`** |
| iSignalSema (-0x43) | `Sync.cpp:104` → same impl, `interruptSafe=true` (`Sync.cpp:106`) |
| WaitSema (0x44) | `Sync.cpp:109` → `EeScheduler::waitSemaphore` **`EeScheduler.cpp:1031`** |
| PollSema (0x45) / ReferSemaStatus (0x47) | `Sync.cpp:114` → `pollSemaphore` `EeScheduler.cpp:1014` / `Sync.cpp:125` |
| **the semaphore table (where counts live)** | `std::unordered_map<int, EeSemaphore> m_semaphores`, `include/runtime/ee_scheduler.h:571`; struct `EeSemaphore` at `ee_scheduler.h:157`, field `count`; the integer maps to `tid - 1` id space internally |
| SleepThread (0x32) | `EeScheduler::sleepCurrent` **`EeScheduler.cpp:730`** |
| WakeupThread (0x33) | `EeScheduler::wakeupThread` **`EeScheduler.cpp:775`** |
| the only runtime-internal signalling path | `signalRpcCompletionSema` `Syscalls/RPC.cpp:145`, call sites 635, 639, 749 |

#### 2. Exact trace of `SignalSema(7)` in our runtime — is it correct?

`EeScheduler.cpp:987` `signalSemaphore(id, interruptSafe)`, in order:

1. look up `id` in `m_semaphores`; miss → `KE_UNKNOWN_SEMID` (returned, never silently swallowed);
2. **`if (!object->waiters.empty())`** → pop `waiters.front()`, `makeReady(*waiter, id, interruptSafe)`,
   `return id;` — the waiter's blocked `WaitSema` returns 7. **A waiter takes precedence over an increment.**
3. else `if (object->count == object->maxCount) return KE_SEMA_OVF;` (this is the ONLY non-increment path,
   and it is reported, not silent);
4. else `++object->count; publishSnapshot(); return id;`.

`makeReady` (`EeScheduler.cpp:2396`) → `enqueueReady` (`:2275`) puts the thread on a priority ready queue and
`requestPreemptionIfHigher` sets `m_rescheduleRequested` **only for a strictly higher priority**; the
non-interrupt-safe wrapper then calls `ee.transferIfRequested(interruptSafe)` (`Sync.cpp:42`), which throws
`EeDispatcherTransfer`, caught at `EeScheduler.cpp:242/277/357/1401/1469` — the mode switch. `iSignalSema`
skips the throw (`interruptSafe=true`).

**Compared to ps2tek (`resources/09-ps2tek.md`, "42h SignalSema"):** *"If a thread has called WaitSema on this
semaphore, this forces a thread rescheduling. Otherwise, the semaphore's count is incremented by one."* — and
for `43h iSignalSema`: *"Similar to SignalSema, except this does not reschedule threads... Returns -1 if
unsuccessful and -2 if the thread is released from its wait state."* Steps 2→4 are exactly that, and the
`interruptSafe` flag is exactly the "does not reschedule" clause. `44h WaitSema`: *"If the semaphore's count
> 0, count is decremented. Else, a thread rescheduling occurs"* — `waitSemaphore:1031` does
`if (object->count != 0) { --object->count; setReturnS32(id); return; }` else push-to-waiters + `blockCurrent`.
**The semantics match ps2tek. I found no deviation.**

#### 3. Is there a bug that would DROP a `SignalSema(7)` or leave a `SleepThread`ed thread un-woken?

**No — plainly. `signalSemaphore` has no silent-discard path: it either wakes a waiter, returns `KE_SEMA_OVF`,
returns `KE_UNKNOWN_SEMID`, or increments. `blockCurrent`/`makeReady`/`enqueueReady`/`selectReady`/`makeRunning`
move threads between states correctly and I traced every one. The implementation is correct and the guest simply
never reaches the `SignalSema(7)` call site — the bug is upstream in the EE DMAC, not the kernel.**

Measured, from our own run log `/mnt/ssd/vulcan4-build/run/boot_w274b.log` (syscall summary block, lines
66207-66245):

- `0x42 sce_SignalSema calls=57`, argument set **{2, 4, 5, 6} — never 7**. The loader's guard pair fires with
  `ra=0x0100b2c4 a0=0x00000004 x1` and `a0=0x00000005 x1`, and its `WaitSema` partner at `ra=0x0100b2a8` with the
  same 4 and 5 — **each wait matched by a signal.** Nothing was dropped; the ids are just different from
  hardware's (the id is loaded from memory at `*(0x70002074)` — `MMIO 0x70002074 accesses=3` — never an immediate,
  so a uniform shift is harmless, exactly as the measurer found).
- `0x44 sce_WaitSema calls=59`, tid1's park at `ra=0x01000de8 a0=7`, occurring **once**.
- `0x40 sce_CreateSema calls=7`; no `SignalSema` with `a0==7` anywhere in the run.
- The only runtime-internal signalling path, `signalRpcCompletionSema` (`RPC.cpp:145`), signals SIF RPC
  completion ids taken from `client->hdr.sema_id`, guarded `semaId == 0 || semaId > 0xFFFF → false`. It cannot
  reach id 7 in this run: no SIF RPC call is in the syscall summary's top entries and the RPC path logs no
  completion. (Stated as "not observed"; a probe would be needed to call it impossible.)
- `grep -c` for a hardcoded 7 in the semaphore code: **0 hits.** No id is special-cased anywhere.

So semaphore 7's counter is 0 because **the signaler thread (tid2) never gets to its `SignalSema` instruction**.
tid2 is the one spinning: `halt=guest_blocked`, `tid2 wait=sleep#0 woken=0 pc=0x0101f348 ra=0x0100afa8`, with
`0x0100afa8/a0=0x00000008 x2924` in the SleepThread distribution — i.e. **2,924 timed 8-microsecond sleeps**,
all from the same poll loop.

#### 4. SleepThread / WakeupThread — what wakes a sleeper, and does the DMA/INTC path deliver?

**Mechanisms, all present in source:**

- **Untimed `SleepThread(0)`** — blocks in `EeWaitReason::Sleep`; woken only by `wakeupThread`
  (`EeScheduler.cpp:775`), which does `makeReady` if the target is `Waiting`/`WaitingSuspended` **and**
  `wait.reason == EeWaitReason::Sleep`, else latches `++target->wakeupCount` for the next `sleepCurrent`
  (`:730`, consumed at its first `if (self->wakeupCount != 0u)`); plus `releaseWait` (`:900`), thread
  deletion, `SuspendThread`.
- **Timed `SleepThread(n)`** — `sleepCurrent:730` computes `deadline = m_eeCycle + n*295` and blocks with an
  `EeTimedSleepWait`; the waker is **`completeTimedSleeps` (`EeScheduler.cpp:1915`)**, driven off the
  `accountCycles()` hot path (no host thread, no interrupt needed).
- **DMAC completion delivery — mechanism EXISTS.** `processPendingTransfers` → `queueCompletedDmacCause`
  (`ps2_memory.cpp:1992`) → `drainCompletedDmacHandlers` (`ps2_runtime.cpp:6046`) →
  `dispatchDmacHandlersForCause` (`Syscalls/Interrupt.cpp:68`) → `EeScheduler::dispatchIrq(true, cause)`
  (`EeScheduler.cpp:1697`), which filters `m_dmacHandlers` at `:1779` and queues a
  `GuestInvocationKind::Interrupt`. The guest **did** register for it: `0x12 sce_AddDmacHandler calls=3`
  (a0=0,1,2; a1=0x0100DAE0) and `0x16 sce_EnableDmac calls=3` (a0=0,1,2).

**Measured, so the answer is not a guess:**

- `inv_by_kind=[intr=94, dmac=0, override=0, other=0]` — **not one DMAC cause was ever completed or
  delivered.** The mechanism is present and unused, not absent.
- `0x33 sce_WakeupThread` — **zero calls in the whole 6.4 s run.** The `wakeupThread` path is never
  exercised. The guest does call `0x35 CancelWakeupThread calls=52` (a0=2 x27 from `0x0100b6dc`, a0=2 x24 +
  a0=1 x1 from `0x0100b018`), i.e. it is tid2 cancelling a wakeup latch it never needed.
- **tid2's sleeper is NOT starved.** 2,924 iterations of `completeTimedSleeps` fired and re-readied it —
  measured by the sleep count itself. tid2 is not blocked; it loops. **This confirms the measurer's
  structural read and refutes the "missing wakeup" framing of the question.**
- Corroboration that the *INTC* half works while the DMAC half does not: the cause-2 handler `0x100D838` is
  dispatched (`[w111:irq838] totalDispatch=1..6` at log lines 961,1152,1279,1287,1402,1415) and the scratchpad
  word it maintains is moving (`flag@0x70002050 = 0,1,2,3,4,5`), with `MMIO 0x70002050 accesses=12887` — the
  spin word. VBlank delivery is healthy; DMAC delivery has never fired once.

#### 4b. Two named candidates for the silent DMA, found on the DMAC path (source-verified)

1. **The DMAE gate drops CHCR *start* writes silently — `ps2_memory.cpp:1376-1386`** (inside
   `writeIORegister`, `:1209`):
   ```cpp
   if ((address & 0xFF) == 0x00 && (value & 0x100)) {
       const auto dctrlIt = m_ioRegisters.find(0x1000E000u);
       const bool dmacEnabled = (dctrlIt == m_ioRegisters.end()) || ((dctrlIt->second & 0x1u) != 0u);
       if (!dmacEnabled) { return true; }          // CHCR is NOT stored, nothing is enqueued
   ```
   Measured: the guest touched **D_CTRL (0x1000e000) exactly once** in the whole run (`MMIO 0x1000e000
   accesses=1`), D_STAT (0x1000e010) twice, VIF0 CHCR (0x10008000) 4 times. If that single write left bit 0
   (DMAE) clear, **every** CHCR start is dropped from then on. `sceDmaReset` (`Stubs/DMA.cpp:173`) deliberately
   writes D_CTRL `0` then `1`, so the intended flow does set DMAE — but this log does not count HLE stub entry
   (`mem.writeIORegister` from a stub is not a guest MMIO access), so **whether `sceDmaReset` ran is Unknown.
   One probe settles it: print D_CTRL inside `sceDmaReset` and print `dmacEnabled` at the gate.**
2. **An empty chain is accepted with STR left set — `ps2_memory.cpp:1615-1636`.** Chain mode walks the tag list;
   the CHCR write-back preserves bit `0x100` from the written value and only pushes a `PendingTransfer` under
   `if (!chainBuf.empty())`. With an empty chain, `hadVif0` (`:1830`) stays false, so the STR clear at `:1976-1980`
   is skipped and STR stays 1 forever with nothing enqueued.

#### 4c. A record contradiction angle 3 is obliged to flag (LAW 3 — no plausible-looking state)

`readIORegister` (`ps2_memory.cpp:2411`) **strips CHCR.STR on every read AND persists the stripped value** —
`:2489-2497`:
```cpp
if (address >= 0x10008000 && address < 0x1000F000) {
    if ((address & 0xFF) == 0x00) {
        uint32_t channelStatus = m_ioRegisters[address] & ~0x100u;   // STR masked OFF
        m_ioRegisters[address] = channelStatus;                      // ...and written back
        return channelStatus;
    }
}
```
Every guest load reaches it — `read8/read16/read32/read64` all route `isIoRegister()` to `readIORegister`
(`:759`, `:794`, `:844`). On real hardware a `lw` of CHCR returns STR=1 while the transfer runs
(PCSX2 `Dmac.h:113-121`, *"1 while it's running"*).

**Consequence: through any guest `lw`, our runtime can NEVER report a DMA in flight, and any read destroys the
bit.** So the record's "ours bit8(0x10008000) = 1 forever" cannot have come from a guest read of that address —
it must have come from a raw/IO-map read that bypasses `readIORegister`. **This does not overturn D4's
conclusion** (the transfer genuinely never completes — `dmac=0` is the same fact), but it changes the *shape*:
the guest does not see STR=1; it reads STR=0, believes the transfer finished, checks the completion word the
transfer was supposed to produce, finds it wrong, and retries with `SleepThread(8)` — 2,924 times. That loop
shape fits every measured number in this run and fits `ra=0x0100afa8` in the SleepThread distribution.
**NEED from the measurer: re-state the "STR=1" observation with the accessor named** (`readIORegister` vs a
raw `m_ioRegisters` / host-RAM dump). Until then it is Unknown which of the two arms is live.

**Verdict for angle 3:** the semaphore/thread kernel is correct and is not the wall. The defect is in the EE
DMAC, and the three named lines above are the only candidates on that path — `ps2_memory.cpp:1381-1386`
(DMAE gate), `:1615-1636` (empty chain, STR never cleared), `:2489-2497` (reads erase STR). **Do not touch
`EeScheduler.cpp` semaphore code; nothing there is wrong.**

---

## Seat findings — angle 4 (HLE comparison) — general-purpose (2026-10-08)

READ-ONLY. No build, no commit, no source change. Answered against the local PCSX2 source checkout
(`/mnt/ssd/tools/pcsx2-src`, v2.9.96 @ 144a19b — cited file:line), our runtime
(`tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/`), and the ps2tek contract PCSX2 executes.

**Answer in one line: PCSX2 does NOT implement WaitSema/SignalSema/CreateSema/SleepThread/WakeupThread in
host code at all — it re-dispatches them to the BIOS ROM's EE kernel via `cpuException(0x20)`. So there is
no PCSX2-authored semantic to diverge from; the correct comparison target is the BIOS kernel contract
(ps2tek). Against that contract our semaphore and sleep/wakeup-count semantics MATCH, and the single
concrete divergence is SleepThread's `$a0` argument — which makes tid2 loop *more*, not less, and is not
the W274 wall. The wall stays upstream in the EE DMAC (angles 1–3).**

### 1. PCSX2's EE syscall path (all file:line verified this session)

| step | cite |
|---|---|
| SYSCALL HLE entry, reads `$v1` | `/mnt/ssd/tools/pcsx2-src/pcsx2/R5900OpcodeImpl.cpp:908` (number read `:912-915`) |
| HLE switch | `R5900OpcodeImpl.cpp:920` (cases `:922-1197`) |
| the 15 HLE'd ids only | enum `Syscall : u8` at `R5900OpcodeTables.h:10-26` |
| **semaphore/sleep fall through** | `R5900OpcodeImpl.cpp:1200-1201` (`default: break;`) then `:1204-1205` `cpuRegs.pc -= 4; cpuException(0x20, cpuRegs.branch);` |
| recompiler = same path | `x86/ix86-32/iR5900.cpp:780-796` `recSYSCALL()` → `recCall(...SYSCALL)` `:794` |
| `cpuException` vectors to BIOS | `R5900.cpp:95-166`; cause 0x20 → offset 0x180 `:114`; pc = `0x80000180` `:160-163` |
| syscall name table (strings only, not code) | `R5900OpcodeImpl.cpp:84` `R5900::bios[256]`; 0x32 "SleepThread" `:102`, 0x44 "WaitSema" `:108` |

`enum Syscall : u8` (`R5900OpcodeTables.h:10-26`) holds **only 15 ids**: SetGsCrt(2), ExecPS2(7),
SetVTLBRefillHandler(13), StartThread(34), ChangeThreadPriority(41), RFU060(60), SetOsdConfigParam(74),
GetOsdConfigParam(75), SetOsdConfigParam2(110), GetOsdConfigParam2(111), sysPrintOut(117),
sceSifSetDma(119), Deci2Call(124), GetMemorySize(127). **WaitSema(0x44), SignalSema(0x42),
CreateSema(0x40), PollSema(0x45), SleepThread(0x32), WakeupThread(0x33), CancelWakeupThread(0x35),
ReferThreadStatus(0x30), RotateThreadReadyQueue(0x2B), ReleaseWaitThread(0x2D) are NOT in it.** A whole-tree
grep for `Sema|SleepThread|WakeupThread|RotateThreadReadyQueue` over `/mnt/ssd/tools/pcsx2-src/pcsx2`
returns ONLY two name tables (`IopModuleNames.cpp:677-750` — IOP export strings; `R5900OpcodeImpl.cpp:99-109`)
plus unrelated host-side `Threading::*Semaphore` in `MTGS.cpp`/`MTVU.h`. `StartThread`/`ChangeThreadPriority`
are "HLE'd" only to sniff the BIOS thread-list address for the debugger (`R5900OpcodeImpl.cpp:1065-1101`);
they do not schedule. T3 §D3's "syscall names are PCSX2's own table `R5900OpcodeImpl.cpp:85`" is exactly
that name array — not an implementation. There is no `pcsx2/System/SysThreads.cpp` in this tree (host
threading now lives in `common/`); the task's file hint is the pre-2013 layout.

### 2. PCSX2's exact semantics for (a)–(d) — the BIOS kernel contract it executes

Because the semaphore/sleep syscalls fall through to `cpuException(0x20)`, PCSX2's runtime semantics ARE
the BIOS ROM's EE kernel — i.e. the ps2tek contract the board already pinned
(`~/.config/opencode/skills/ps2-recomp-Agent-SKILL/resources/09-ps2tek.md`):

- (a) **SignalSema (42h)** with a WAITING thread: wake the waiter (re-add to ready list, reschedule);
  count is NOT incremented. No waiter → `count++`. `09-ps2tek.md:5160-5164`.
- (b) **WaitSema (44h)** count>0 → decrement and return; count==0 → caller status set WAIT, reschedule
  (parked on the sema's wait queue). `09-ps2tek.md:5173-5177`. The "woken" field in our snapshots is
  `wakeup_count`, a *SleepThread*-only concept — a sema waiter has no woken flag.
- (c) **SleepThread (32h)** `void` (no argument): wakeup_count>0 → decrement; else RUN/READY → WAIT +
  reschedule. `09-ps2tek.md:5065-5070`. **WakeupThread (33h)**: WAIT(sleeping)→READY+reschedule;
  WAITSUSPEND(sleeping)→SUSPEND; READY/SUSPEND/WAIT/WAITSUSPEND(semaphore)→`wakeup_count++`; other→no
  effect. `09-ps2tek.md:5072-5082`. **CancelWakeupThread (35h)**: wakeup_count→0, return old value.
  `09-ps2tek.md:5089-5093`. ("ThreadPark" is internal — parking = removing a thread from the ready list
  onto an object's wait queue inside `WaitSema`/`SleepThread`; there is no syscall named ThreadPark.)
- (d) **Wakeup is kernel-call driven, never INTC/DMAC driven.** PCSX2's INTC/DMAC C++ (`Counters.cpp`,
  `Dmac.cpp`, `HwWrite.cpp`) only raises the CPU interrupt so the BIOS interrupt handler runs; it never
  touches PS2 thread state. A thread wakes only when BIOS kernel code executes `WakeupThread`/`SignalSema`
  — called directly by the game or from inside a BIOS interrupt handler.

### 3. Diff against OUR runtime (file:line vs the contract above)

| syscall | contract (ps2tek) | ours | verdict |
|---|---|---|---|
| SignalSema w/ waiter | wake waiter, no count++ `:5162-5164` | `EeScheduler.cpp:995-1004` pop front waiter → `makeReady` | **MATCH** |
| SignalSema no waiter | `count++` `:5163-5164` | `EeScheduler.cpp:1005-1011` (`KE_SEMA_OVF` at max `:1005-1008`) | MATCH (error code differs: ours `-420`/`-408` vs hw `-1`) |
| iSignalSema released | returns -2 `:5171` | same `signalSemaphore` returns the sema id | minor divergence (return code only) |
| WaitSema count>0 | count-- `:5175` | `EeScheduler.cpp:1042-1050` | **MATCH** |
| WaitSema count==0 | park → WAIT `:5175-5177` | `EeScheduler.cpp:1051-1055` push waiter + `blockCurrent` | **MATCH** |
| SleepThread | `void`, no arg `:5065` | **reads `$a0` as microseconds** `Syscalls/Thread.cpp:654`; nonzero → timed sleep `EeScheduler.cpp:763-772` | **DIVERGES** |
| SleepThread wakeup_count>0 | decrement, no park `:5067-5068` | `EeScheduler.cpp:749-754` | MATCH |
| WakeupThread WAIT(sleep) | →READY+resched `:5076-5077` | `EeScheduler.cpp:791-795` `makeReady` | MATCH |
| WakeupThread WAITSUSPEND(sleep) | →SUSPEND `:5077-5078` | `makeReady` sets Suspended when `suspendCount!=0` `EeScheduler.cpp:2415-2419` | MATCH |
| WakeupThread sema/other | `wakeup_count++` `:5080-5081` | `EeScheduler.cpp:796-799` `++wakeupCount` | MATCH |
| CancelWakeupThread | reset, return old `:5092-5093` | `EeScheduler.cpp:804-820` | MATCH |

The SleepThread divergence is already admitted in our own tree: `Syscalls/Thread.cpp:650-653` (W161) —
"ps2sdk says `SleepThread(void)` takes NO argument, so reading a0 as a duration is a deviation" — and
hardware (`09-ps2tek.md:5065`) agrees with ps2sdk. Its effect on THIS run is the opposite of a missing
wakeup: GT4's tid2 calls `SleepThread` with `a0=8` (`boot_w274b.log` `0x32 … a0=0x00000008 x2924`), so ours
turns each into an 8µs timed sleep that `completeTimedSleeps` (`EeScheduler.cpp:1915-1969`) re-readies
2,924 times — tid2 is *more* active than hardware, not less. It cannot be why the signal never arrives.
(On hardware those `a0=8` calls are untimed parks, so after the DMAC fix the unwind must also reconcile
W161 — flag it, do not chase it now.)

### 4. Verdict for angle 4

**Semantics match; the divergence is elsewhere.** No host path in PCSX2 implements these syscalls
(`R5900OpcodeImpl.cpp:1200-1205` is the whole of PCSX2's "implementation" for them), and our semaphore +
sleep/wakeup-count semantics match the BIOS kernel contract PCSX2 executes. The one concrete divergence
(SleepThread `$a0`-as-duration) makes tid2 loop more, not less, and is not the W274 wall. This CONFIRMS
angle 3 and the measurer's D4: the signal never arrives because tid2 never reaches its
`SignalSema(*(0x01047A80))` — the EE DMAC never completes the channel-0 transfer, so tid2 stays in its
poll. Do not touch the semaphore/sleep code.

---

## Seat findings — angle 1 (Ghidra static)

**Seat:** architect. **Method:** static only — grep + parse of the authoritative recompiled loader
`/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp` (707 functions, 0x01000008..0x0102DBEC),
cross-checked against the runtime syscall dispatcher
`/home/or/vulcan4/tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp`. No oracle, no build.

### 0. Loader syscall-number table (the stubs' identity, confirmed from Dispatcher.cpp + stub `li $v1,N`)

The loader's stubs at `0x0101Fxxx` use PS2Recomp's CUSTOM numbering, not the BIOS numbering:

| stub addr | v1 | syscall |
|---|---|---|
| 0x0101F220 | 0x20 | CreateThread |
| 0x0101F240 | 0x22 | StartThread |
| 0x0101F260 | 0x24 | ExitDeleteThread |
| 0x0101F340 | 0x32 | **SleepThread** |
| 0x0101F350 | 0x33 | WakeupThread |
| 0x0101F420 | 0x40 | **CreateSema** |
| 0x0101F430 | 0x41 | **DeleteSema** (not "SignalSema-adjacent" — it deletes) |
| 0x0101F440 | 0x42 | **SignalSema** |
| 0x0101F450 | -0x43 | iSignalSema |
| 0x0101F460 | 0x44 | **WaitSema** |

The sema-7 id lives ONLY at global `0x01047A80`. It is written exactly ONCE, by `CreateSema`'s return at
`0x10009C4` (`sw $v0,0x7A80($v1)`, v1=lui 0x104). So "SignalSema(7)" == a `jal func_101F440` whose `$a0` is
loaded from `0x01047A80` (offset 0x7A80). Grep for every load of that global: exactly four reads exist —
`0x1000D70`, `0x1000DDC`, `0x1000DEC`, `0x1000E20` — and no literal-7 SignalSema exists anywhere.

### 1. THE DEFINITIVE ANSWER — complete set of PCs that can SignalSema(7)

Exactly **two**, and they are the two threads' only sema-7 signallers:

| PC | function | a0 source | thread |
|---|---|---|---|
| **0x1000D6C** | `sub_01000BA0` (FUN_01000BA0) | `lui $v0,0x104; lw $a0,0x7A80($v0)` → `*(0x01047A80)` | **tid2** (created thread, entry 0x01000BA0) |
| **0x1000DE8** | `sub_01000DC0` (FUN_01000DC0) | `s0=0x01047A80; lw $a0,0($s0)` → `*(0x01047A80)` | **tid1** (main thread) |

**tid2's SignalSema(7) is at 0x1000D6C. tid1's SignalSema(7) is at 0x1000DE8.**

### 2. The full sema-7 lifecycle (who creates / waits / signals / deletes it)

| PC | op | function | thread |
|---|---|---|---|
| 0x10009B0 / 0x10009C4 | CreateSema → store id to 0x01047A80 | `sub_01000940` (FUN_01000940) | tid1 |
| 0x10009C0 | WaitSema(a0=returned id = 7) | `sub_01000940` | tid1 |
| 0x1000DE0 | WaitSema(7) | `sub_01000DC0` | tid1 |
| **0x1000D6C** | **SignalSema(7)** | `sub_01000BA0` | **tid2** |
| **0x1000DE8** | **SignalSema(7)** | `sub_01000DC0` | **tid1** |
| 0x1000E1C | DeleteSema(7) (`jal func_101F430`) | `sub_01000E00` | tid1 |

### 3. Thread topology (static call graph)

- **tid1 root** `sub_01000008` (ELF entry) → `sub_01000558`@0x1000210 → calls, in order:
  `FUN_01000940`@0x100057C (CreateSema(7) + WaitSema(7) + **CreateThread(entry=0x01000BA0)**@0x1000A00),
  `FUN_01000E30`@0x1000608, `FUN_01000DC0`@0x1000610 (WaitSema(7)+SignalSema(7)), `sub_01000E00`@0x1000618
  (FUN_01000DC0 then DeleteSema(7)). So the whole create→wait→cleanup runs on **tid1**.
- **tid2** = thread created at `0x1000A00` (`jal func_101F220` CreateThread; entry field `sp+0x24 = 0x01000BA0`
  built at `0x10009E4 addiu $v1,$v1,0xBA0`). FUN_01000BA0 is **never `jal`'d** anywhere — it is only the thread
  entry. Its body ends `SignalSema(7)`@0x1000D6C then `ExitDeleteThread` (`jal func_101F260`=0x24)@0x1000D74.
- The handoff: tid2 loads the engine (FUN_01000BA0 calls `func_100B6F8`/`func_100B108`/`func_100B500`/`func_100D710`
  etc.), signals sema 7, and deletes itself. tid1, parked at WaitSema(7), wakes and cleans up. **tid2 is the waker.**

### 4. Exhaustive SignalSema-site audit (all 31 `jal/j func_101F440`; only 2 can be 7)

| site PC | function | a0 delay-slot | can=7? |
|---|---|---|---|
| 0x1000D6C | sub_01000BA0 | `lw $a0,0x7A80($v0)` (v0=0x1040000) | **YES (tid2)** |
| 0x1000DE8 | sub_01000DC0 | `lw $a0,0($s0)` (s0=0x01047A80) | **YES (tid1)** |
| 0x100A6D0 | sub_0100A348 | (tail j, a0 set earlier, not 0x7A80) | no |
| 0x100AE70 | sub_0100AE48 | (tail j) | no |
| 0x1014240 | sub_01014218 | `lw $a0,0x3260($s0)` | no |
| 0x101460C | sub_01014408 | `lw $a0,0x3268($s6)` | no |
| 0x10146C8 | sub_01014408 | `lw $a0,0x3268($s6)` | no |
| 0x10147A4 | sub_01014728 | `lw $a0,0x3268($s0)` | no |
| 0x10148E0 | sub_01014728 | `lw $a0,0x3268($v0)` | no |
| 0x10148FC | sub_01014728 | `lw $a0,0x3268($v1)` | no |
| 0x1014AA4 | sub_01014A28 | `lw $a0,0x326C($s0)` | no |
| 0x10150C0 | sub_01014F18 | `lw $a0,0x326C($s3)` | no |
| 0x10150F8 | sub_01014F18 | `lw $a0,0x326C($s3)` | no |
| 0x1016E90 | sub_01016DF8 | `lw $a0,0x4E5C($s2)` | no |
| 0x1016F50 | sub_01016DF8 | `lw $a0,0x4E5C($s0)` | no |
| 0x1016F60 | sub_01016DF8 | `lw $a0,0x4E5C($s0)` | no |
| 0x101B66C | sub_0101B620 | `lw $a0,0($s1)` | no |
| 0x101B704 | sub_0101B620 | `lw $a0,0($s1)` | no |
| 0x101B7BC | sub_0101B750 | `lw $a0,0x5364($s2)` | no |
| 0x101B7EC | sub_0101B750 | `lw $a0,0x5364($s2)` | no |
| 0x101BB40 | sub_0101BAC0 | `lw $a0,0x5364($s3)` | no |
| 0x101BB84 | sub_0101BAC0 | `lw $a0,0x5364($s3)` | no |
| 0x101BC34 | sub_0101BAC0 | `lw $a0,0x5364($s3)` | no |
| 0x102392C | sub_010238D0 | `lw $a0,0x5330($s1)` | no |
| 0x102399C | sub_01023950 | `lw $a0,0x5330($s1)` | no |
| 0x10239B8 | sub_01023950 | `lw $a0,0x5330($s1)` | no |
| 0x1023AF8 | sub_01023AC8 | `lw $a0,0x5330($s1)` | no |
| 0x1023B14 | sub_01023AC8 | `lw $a0,0x5330($s1)` | no |
| 0x1023F7C | sub_01023F78 | `lw $a0,0x532C($v0)` | no |
| 0x1025E74 | sub_01025658 | `lw $a0,0x5330($s0)` | no |
| 0x1026D30 | sub_01026158 | `lw $a0,0x5334($s5)` | no |

All 29 "no" sites load a0 from a DIFFERENT global (0x3260/0x3268/0x326C, 0x4E5C, 0x5364, 0x5330/0x5334/0x532C,
0x110, or a pointer field) — none touches 0x01047A80, none is literal 7. There are no register-indirect
(`jalr`) calls to the sema stubs; all calls are direct `jal`/tail-`j`.

### 5. Thread-park sites

- **WaitSema(7) parks** (block on sema 7): `0x10009C0` (sub_01000940, tid1) and `0x1000DE0` (sub_01000DC0, tid1).
  tid2 does NOT WaitSema — it only signals.
- **SleepThread (0x32, func_101F340) parks**: `0x100AFA0` (sub_0100AE78), `0x100B678` (sub_0100B628),
  `0x1011330` (sub_010112E0), `0x101175C` (sub_010116A8). Reachability: sub_0100AE78/sub_0100B628 are the
  file-loading poll loop reached from **both** threads (tid2's FUN_01000BA0 calls sub_0100B6F8/sub_0100B108;
  tid1's path reaches the same loaders); sub_010112E0 is tid1-side (called from sub_01011650/sub_010116A8/
  sub_010118B8). None of the four lies on the sema-7 handoff itself.
- Other WaitSema sites (34 of 36) park on DIFFERENT semas (ids in 0x3260/0x5330/etc.) inside the engine-loader
  subgraph — none blocks on 7.

### Bottom line for the dynamic seat

The waker the lead asked about is **tid2 itself**: `SignalSema(7)` at **0x1000D6C** in FUN_01000BA0, immediately
followed by `ExitDeleteThread` at 0x1000D74. The only other sema-7 signaller is tid1's `SignalSema(7)` at
**0x1000DE8** (FUN_01000DC0), which fires only AFTER tid1 has already been woken from its WaitSema(7). So on the
static record the tid2 signal is the sole waker of tid1's park.

---

## Seat findings — angle 2 (oracle dynamic #2)

**Seat:** measurer #2 (dynamic). **Method:** live PCSX2 DebugServer :21512 reads + Ghidra
(SCUS_973.28, port 8192) static. All numbers below are measured, not inferred. Instrument facts are
labelled as instrument facts. Nothing was reset, no breakpoint of measurer #1 was touched, java was
untouched.

### 0. Instrument facts measured before any conclusion (rule: a BP that can't hit is evidence about the instrument)

| fact | value | why it matters |
|---|---|---|
| `pcsx2_status` | `Paused: true, Cycles 1211540030, EE PC 0x00081FC0` | the inherited "PAUSED at 0x00100008" is **stale**; hardware is parked on the kernel idle loop `0x81FC0` (`b ->0x81FC0` at 0x81FD8) |
| `pcsx2_list_breakpoints` | 1 — `0x0101F460 ✅ [cond: a0 == 7]` | measurer #1's BP sits on **loader** memory |
| `pcsx2_read_memory(0x0100ACA8, 32)` | **all zero** | the SCUS_973.28 loader image is **reclaimed** after `ExecPS2` |
| `pcsx2_read_memory(0x0100DAE0, 64)` | **all zero** | same — loader code is gone |
| `pcsx2_read_memory(0x01047A80, 16)` | **all zero** | the "sema id global = 7" is a **load-time** value; it is dead now |
| `pcsx2_read_memory(0x005ADB60, 96)` | live: `li v1,-0x2C` / `li v1,0x2D` / `li v1,0x32` / `li v1,0x33` / `li v1,-0x34` … | hardware is running the **engine's own** stub table, at engine VA 0x005ADxxx |
| `pcsx2_get_threads(ee)` | **20 threads: 13 @ PC=0x005ADBC8 waitType=2 (SleepThread), 6 @ PC=0x005ADCE8 waitType=1 (WaitSema)** | hardware **does** park threads — but in the ENGINE stubs, never in the loader |

**This matters:** the board's D3 ("hardware does NOT park on sema 7") is right about id 7, but the
generalisation "hardware does not park on sema" is **wrong** — hardware has **6 threads parked on
`WaitSema` right now**, at the engine stub `0x005ADCE8`. Verified stubs, raw bytes:
`0x005ADBC0 = 0x24030032` (`li v1,0x32` SleepThread), `0x005ADCE0 = 0x24030044` (`li v1,0x44` WaitSema).
PC+8 = the stub's `jr ra`, which is exactly where PCSX2 reports a thread blocked inside a syscall.

**Therefore measurer #1's BP at `0x0101F460` is dead instrumentation** — it points at zeroed loader
memory and can never fire again. Its `✅` is a set-state, not a hit. (Its own evidence line —
`116379691 → 116383141` — predates the image reclaim.)

### 1. The counter word the lead asked for — and it is not the sema counter

The lead's brief assumed a *kernel semaphore counter* behind `SignalSema(7)`. On hardware the sema ids
are **not 7**: `pcsx2_read_memory(0x70002060, 64)` gives the live per-DMA-channel structs in EE
scratchpad (stride 12 B):

| word | value | meaning (from `FUN_0100ac10`) |
|---|---|---|
| `0x70002074` | **0x16 = 22** | VIF0 DMA channel semaphore id |
| `0x70002080` | **0x17 = 23** | VIF1 DMA channel semaphore id |
| `0x7000208C` | **0x18 = 24** | GIF DMA channel semaphore id |
| `0x70002070` / `0x7000207C` / `0x70002088` | 0 | per-channel waiter-list heads |
| `0x7000206C + ch*12` | 0 | per-channel flag byte |
| **`0x7000206D + ch*12`** | **0x00** | **per-channel DMA BUSY byte — THE COUNTER** |
| `0x70002050` | 0x00000D2C = **3372** | DMA/GS completion count, incremented by the handler |
| `0x70002090` | `0x006DE670` | global waiter-list head (engine VA) |

`FUN_0100ac10(ch, chcr)` creates each channel's **binary** semaphore (`CreateSema{init=1,max=1}`) and
stores its id at `(&DAT_70002074)[ch*3]`. So the semaphore is a *DMA-channel lock*; the lead's "sema 7"
is a loader-phase global, not this mechanism.

### 2. THE TWO VALUES, WHICH IS HARDWARE, AND THE FIRST DISAGREEING INSTRUCTION

- **Counter word:** byte `0x7000206D + 12*ch` in EE scratchpad (per-DMA-channel busy flag).
- **HARDWARE (oracle):** `0x00` for all three channels — measured `0x70002060..0x7000208F` = all zero.
- **OURS:** non-zero — our tid2 is sitting *inside* the retry loop that only runs when the byte is ≠ 0
  (`pc = SleepThread stub, ra = 0x0100AFA8`).
- **First instruction they disagree at — `0x0100AED4  bne v1,zero,0x0100AEF0`**, on the byte loaded by
  **`0x0100AED0  lbu v1,0x0(s1)`**, `s1 = 0x7000206D + 12*ch` (built at `0x0100AEC4 addiu s1,v0,0x1D`,
  `v0 = 12*ch`, base `s8 = 0x70002050`).

  Hardware: byte == 0 ⇒ `bne` **not taken** ⇒ `0x0100AED8 move v0,zero` ; `0x0100AEDC b 0x0100B020` ⇒
  **returns 0 immediately**; the `SleepThread` at `0x0100AFA0 jal 0x0101F340` is **never executed**.
  Ours: byte ≠ 0 ⇒ `bne` **taken** ⇒ `0x0100AEF0` registers the thread in the channel's waiter list
  (`0x70002070+12*ch`) and enters the ≤120-iteration loop, sleeping at `0x0100AFA0 jal 0x0101F340`
  (`ra = 0x0100AFA8` — exactly our tid2 park). Loop tail: `0x0100AFAC bnel v1,zero,0x0100AF50`
  re-reads `s0 = s1`; on exhaustion `0x0100AF5C sb zero,0xD(s1)` clears the flag, aborts CHCR bit
  `0x100`, returns `-1`.

### 3. The PC + function that WRITES that counter

Ghidra `xrefs_list` on `0x7000206D` gives exactly 7 writers/readers:

| PC | function | write |
|---|---|---|
| `0x0100B0B8` | `FUN_0100B050(ch,addr,size)` | **sets busy = 1**, then starts GIF-style DMA (`(*p & 0xfffffef0) \| 0x101`) |
| `0x0100DEBC` | `FUN_0100DE58(ch,addr)` | **sets busy = 1**, then VIF-style DMA (`(*p & 0xfec0) \| 0x10000105`) |
| `0x0100DCF0` | **`FUN_0100DAE0`** (the DMAC-completion handler) | **`sb zero,0xD(a0)` — CLEARS busy = 0**, then walks `0x70002070+12*ch` and wakes each waiter via `FUN_010202E8` |
| `0x0100AC78` | `FUN_0100AC10(ch,chcr)` | channel teardown: clears both `0x7000206C` and `0x7000206D` |
| `0x0100AF5C` | `FUN_0100AE78(ch)` | timeout path: clears own flag, aborts CHCR |
| `0x0100AED0` / `0x0100AF1C` | `FUN_0100AE78(ch)` | reads only |

**The handler that clears it is `FUN_0100DAE0`**, and it is *registered by pointer*, which is why
Ghidra has no function at `0x0100DAE0`/`0x0100DB40`:

```
FUN_0100ACA8:
  FUN_0100ac10(0,0x40); FUN_0100ac10(1,0xc0); FUN_0100ac10(2,0x80);
  REG_DMAC_CTRL = 1;  SYNC(0);
  DAT_700020f4 = AddDmacHandler(0, 0x100dae0, 0);   <-- THE HANDLER
  DAT_700020f8 = AddDmacHandler(1, 0x100dae0, 0);
  DAT_700020fc = AddDmacHandler(2, 0x100dae0, 0);
```

The handler ids come back **4 / 5 / 6**, measured live: `0x700020F4 = 4`, `0x700020F8 = 5`,
`0x700020FC = 6` (and `0x700020F0 = 7` is the neighbouring INTC handler id). Inside `FUN_0100DAE0`,
the clear is `0x0100DCF0 sb zero,0xD(a0)` with `a0 = 0x70002050 + 0x10 + 4*(a0+a3)` ⇒ `0x7000206D+12*ch`.

Separately, `FUN_0100D838` is the GS-completion handler (it is what increments the 3372 counter):
it does `DAT_70002050 += 1`, sets `DAT_70002064 = (GS_CSR >> 13) & 1`, calls the callback at
`0x70002060`, then walks the waiter lists at `0x70002070`/`0x7000207C`/`0x70002088` and
`0x70002090`, calling `FUN_010202E8(waiter[1])` (= WakeupThread) on each; ends `EI(); return 1`.
`FUN_0100D838` does **not** touch `0x7000206D` — the busy-flag clear is `FUN_0100DAE0`.

### 4. Why ours tears: the whole chain, one line

`FUN_0100B1C8` = the DMA-chain sender: `WaitSema(0x70002074[ch*3])` → kick → `FUN_0100AE78(ch)`
busy-wait → `SignalSema(...)`. The busy byte is cleared **only** by the DMAC-completion handler
`FUN_0100DAE0`. **Our runtime never dispatches that handler**: the board's angle-3 measurement is
`inv_by_kind=[…, dmac=0]` and `sce_WakeupThread calls=0` in our log. So the byte stays 1, the ≤120-loop
never exits on the flag, tid2 sleeps at `0x0100AFA0`, and no `SignalSema(22/23/24)` is ever reached —
which is the observed "parked on sema" + "sleep#0" pair. **The sema park is a SYMPTOM; the root is
that our DMAC completion path is never entered, so the busy byte at `0x7000206D+12*ch` is never
cleared.**

Supporting from our side (already on the board, §T4, `docs/W274-RESULTS.md`): our
`readIORegister` returns `m_ioRegisters[addr] & ~0x100u` **and writes it back**
(`ps2xRuntime/src/lib/Memory/ps2_memory.cpp:2489-2497`), so no guest load can ever observe
`CHCR.STR=1` — a second, independent reason the guest's DMA state machine can never close.

### 5. Explicit refusals (law 3 — refused, not guessed)

- **Not claimed:** that the recomp's `EeScheduler` semaphore ids (allocated from 1) are the cause.
  They are not: hardware's ids are 22/23/24 and the divergence is upstream of any semaphore.
- **Not claimed:** anything about `0x01000BA0` / `SignalSema(7)@0x1000D6C` as the waker of our tid2.
  Our tid2's `ra = 0x0100AFA8` puts it in `FUN_0100AE78`, not in `FUN_01000BA0`. The static record's
  angle-1 story is about the loader-phase sema 7; the dynamic park is the DMA busy-wait. Both are
  true of different phases; the dynamic one is the live wall.
- **Not measured:** whether our runtime ever reaches `0x0100AFA0` with a *stale* byte vs a byte our
  own `FUN_0100B050`/`FUN_0100DE58` set. Either way the fix is the same (dispatch the completion),
  so I did not spend oracle time on it.
- **STUCK (instrument, named):** the assigned "boot re-run with a sema-counter watchpoint" is
  unreachable without relaunching PCSX2 and killing measurer #1: `find / -name "*.p2s"` → **empty**,
  no save state exists, and Pine/DebugServer expose no reset. I did not relaunch. Converted the method
  to its correct analogue (the DMA busy byte) instead.
  `STUCK: no save state / no reset → cannot re-run boot. / TRIED: find *.p2s; pcsx2_load_state; reset
  over Pine+DebugServer. / BLOCKED BY: measurer #1 holds the only instance, no checkpoint on disk. /
  NEED: a fresh `pcsx2-qt -debugger` boot owned by one seat, with BP at 0x0100AED4.`


---

## Lead's state — 2026-10-08 (end of W274 debug run)

### What was DONE this dish
- **T1 link**: full 19,404 engine relinked (347 MB, 20,127 `nm -C`). Boot unchanged → link necessary, NOT sufficient.
- **T2 0x5b**: `emulateGuestGetEntryAddress` (98 lines) applied + built; patch `ps2recomp-linux-w274-0x5b-getentryaddress.patch`. Boot unchanged (0x5b 6× → 1 LOUD line) → 0x5b NOT the blocker.
- **T3 sema#7 → DMAC root**: 5 independent angles converged (oracle ×2, Ghidra static, kernel audit, HLE diff).

### The wall, precisely (all measured this session)
1. tid2 (FUN_01000BA0) is the ONLY SignalSema(7) waker (Ghidra static).
2. tid2 parks in the DMA-completion poll (0x0100AF50), waiting for D0_CHCR.STR (bit 8 of 0x10008000) and the busy byte 0x7000206D.
3. The VIF0 ch0 transfer (CHCR=0x145, mode=1 chain, TADR=0x1fffdb0, 3 tags → 2704 bytes) **COMPLETES**: `[w274:dmac] queued/drain cause 0 ×1`, STR cleared.
4. The DMAC handler FUN_0100DAE0 (0x100dae0) is registered (AddDmacHandler 0/1/2) and matches dispatchIrq (cause=0 enabled=1 hasFn=1).
5. **FUN_0100DAE0 is NEVER entered** (0 `target_pc=0x100dae0` in the log) → the interrupt invocation is queued but never serviced → busy byte never clears → tid2 loops → tid1 WaitSema(7) → ExecPS2=0.

### Precise bug (handoff)
The DMAC interrupt invocation (GuestInvocationKind::Interrupt, queued by dispatchIrq) is **not serviced**. The INTC invocations (94) run; the single DMAC one does not. Fix is in EeScheduler invocation servicing (`serviceInvocations` / dispatch loop), NOT the transfer and NOT ps2_memory.

### Temporary probes (OFF by default, need law-8 capture or revert before "done")
`VULCAN4_W274_DMA`-gated `[w274:dma]/[w274:dma2]/[w274:tag]/[w274:dmac]/[w274:irq]` logs in:
- `ps2xRuntime/src/lib/ps2_memory.cpp` (3 sites)
- `ps2xRuntime/src/lib/ps2_runtime.cpp` (1 site)
- `ps2xRuntime/src/lib/Kernel/EeScheduler.cpp` (1 site)
These are diagnostic-only; must be captured in a patch or removed. Working tree has uncommitted changes.

### Not reached
`ExecPS2=0`, `halt=guest_blocked`, picture still the disclaimer. The wall is now named to ONE unserviced DMAC interrupt invocation.
