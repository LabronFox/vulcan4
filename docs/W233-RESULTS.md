# W233 — reachability scoping the engine (step 1 of 3)

Goal: bound the engine recompile to entry-reachable code, then split/compile/wire ExecPS2.
Input: `/mnt/ssd/vulcan4-build/w231-engine.elf` (synthesized; PT_LOAD vaddr 0x100000, entry 0x100008,
via `/mnt/ssd/gt4/work/w231-engine_recomp.toml`).

## 1. Reachability filter — TOOL change (env-gated, OFF by default)

`ps2xRecomp/src/lib/ps2_recompiler.cpp`, after the decode pass and before
`discoverAdditionalEntryPoints()`: when `PS2RECOMP_REACHABLE_ONLY` is set, compute the closure of the
ELF entry over **direct** `J`/`JAL` targets (mapping a target to the function whose `[start,end)`
contains it; seeds = ELF entry + `entry_points` hints), erase every recompilable function not in the
closure, and report the count. Off by default, so the loader ELF (which does not set it) is
byte-for-byte unchanged. Patch: `tools/patches/ps2recomp-linux-w233-reachable-only.patch` (99 lines).

Measured effect on the engine:

```
[recompiler] extracted 18903 functions
[recompiler] W233 reachable-only: kept 6484 of 18903 functions (entry=0x100008)
[recompiler] collected 129622 resumable entry point(s) across 4704 owner function(s)   (was 527553)
```

So the entry's direct-call closure is **6484 of 18903** functions (34%); resume points fall 4.1× and
the partial TU is 32.7 MB (was 199–245 MB). This is the measured step-1 number.

## 2–3. NOT DONE this dish — the recompiler still does not finish

Even scoped to 6484 functions the recompiler did not complete within a 20-minute run (`timeout 1200`,
exit 124) — it was still in `generating function output with 11 worker(s)` at 32.7 MB. The loader run
(723 functions) finishes in seconds, so ~9× the functions should be minutes; the observed cost is far
higher, i.e. the emitter/resume-collection scales worse than linearly on this image. TU splitting
(step 2) and harness wiring (step 3) were therefore not reached. The ExecPS2 path is unchanged:
`halt=execps2_unmapped_entry`, and the loud `VULCAN 4 LIMITATION` stays — nothing is faked, no
generated output was hand-edited, the loader's 723 functions are untouched (separate output dir),
suite green.

## Next (bounded)

(a) Profile/split the emitter so a 6484-function image finishes (step 2 TU splitting is the natural
fix, and it also parallelises the `-O2` compile across `-j4`); (b) then compile the engine TU(s),
link beside the loader, and make the driver's ExecPS2 relaunch dispatch into
`[0x100000, 0x617A14)` at entry `0x00100008`; (c) iterate: anything the guest reaches indirectly and
is not emitted must be re-added via `entry_points` or reported as a LOUD limitation.

## Reconcile (recorded)

The engine entry `0x00100008` is real code — `0x70000C28` is MMI `padduw rd=1,0,0` (opcode 0x1C,
function 0x28 bits[5:0], sub-op PADDUW 0x10 bits[10:6]); the `0x800` stride is `rd<<11`. The full
range is a coherent crt0. (Caine confirmed against the ISA table.)
