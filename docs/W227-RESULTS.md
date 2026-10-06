# W227 — PCSX2 A/B GROUND TRUTH: the merge loop IS a real divergence

**Date:** 2026-10-05. **Tool:** PCSX2 v2.9.96 + our DebugServer (Linux port) + GhydraMCP.
**Status:** the first measurement the campaign never had — *real GT4 running beside our recomp.*

> **The whole point:** for 26 dishes (W219–W226d) the campaign argued about whether the
> `sub_010088E8` merge spin was "a runtime defect" or "genuine game state", and retracted the answer
> repeatedly. This dish stops guessing: it runs **retail GT4 in PCSX2** and watches the same code.

---

## 1. PCSX2 is standing and our DebugServer port works

- BIOS: `ps2-0200a-20040614.bin` (SCPH-70012 v2.00 USA), copied into `~/.config/PCSX2/bios/`.
- Disc: `/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso`.
- PCSX2 reached the **intro/menu** — captures are **29k–40k distinct colours**, not the 16-colour
  disclaimer our recomp is stuck on.
- `[DebugServer] Listening on 127.0.0.1:21512` and `pcsx2_connect` succeeds. **The Linux DebugServer
  port is verified end-to-end** (it was unproven before this dish).
- Launched with `-debugger` → the VM pauses at the ELF entry `0x01000008` (the same PC our recomp
  starts from), so boot can be caught from instruction 0.

Reference captures (paths only, not opened):
`/mnt/ssd/vulcan4-build/run/pcsx2-gt4-ref.png` (39,717 colours) and `...-ref2.png` (29,189 colours).

---

## 2. Real GT4 reaches EVERY address our recomp spins on

Breakpoints set from `0x01000008`, real GT4 boot to menu:

| Address | Function | Real GT4 | Ours |
|---|---|---|---|
| `0x01028638` | `sub_01028638` (GT4's own FindAddress override) | **fires** (loops, as expected) | loops |
| `0x01007738` | `func_1007738` (accumulator) | **fires, then RETURNS** | called 125M×, never returns |
| `0x010088e8` | `sub_010088E8` (the merge) | **fires** | entered, spins |

**So the campaign's "the parser chain never runs / game-state wall" framing is incomplete: real GT4
reaches the identical code.** The difference is not *reach*, it is *exit*.

---

## 3. THE DECISIVE MEASUREMENT — the merge loop converges on real hardware

The loop (real disasm `0x10089c8`..`0x10089e0`):

```
0x10089c8: jal func_1007738      ; accumulate(sp)
0x10089cc: addiu s1, 1
0x10089d0: move a0, sp
0x10089d4: jal func_1005870      ; compare(sp, s2)
0x10089d8: move a1, s2
0x10089dc: bltz v0, 0x10089c8    ; loop while compare < 0
```

- **Real GT4:** first iteration `v0 = 0xFFFFFFFF` (−1) → loops. It then **EXITS** — PC reached
  `0x010089e4` after ~4,940 cycles (~1,000 iterations). `v0 = 0x00000001` (≥ 0) at exit.
- **Ours:** the campaign measured this same loop at **125,740,248** transfers, `halt=livelocked_in_syscall`.

**This is a real divergence, measured. Our recomp fails to converge where real GT4 converges.**

---

## 4. The data — real GT4's merge inputs are POPULATED; ours are empty

Real GT4, `sub_010088E8` entry (`0x010088e8`), `ra=0x01009004` (caller `sub_01008C50`):

| reg | value | struct `+8` (count) | struct `+0x14` (base) |
|---|---|---|---|
| a0 | `0x01895330` | `0` | `0x01895810` |
| a1 | `0x018952D0` | `0x20` (32) | `0x018955a0` |
| a2 | `0x01895300` | `0x20` (32) | `0x01895630` |
| a3 | `0x018956C0` | `0x20` (32) | `0x01895780` |

At the loop exit, the accumulator at `sp=0x01FFF900` has `count=0x21` and `base=0x01895830`.

The buffers are **populated with pseudo-random 32-bit words**, e.g. base `0x01895630`:
`c4531b7f 7da6ebd2 e7d2660d 0042564b 3d3704c0 dbbbad5c ...`
and base `0x01895780`: `3bace481 8259142d 182d99f2 ffbda9b4 ...`

**The campaign measured ours as empty** — W220: *"the stream's bit array `0x1895480` is written ONLY
by the rotator itself (val=0x0) — no producer ever fills it."*

**So the divergence is upstream of the loop: the producer that fills these buffers produces real data
on hardware and (all-)zeros in our recomp.** That is the wall, now located on the right side.

---

## 5. Ruled OUT (so the next dish does not re-chase them)

- **`func_1005AB8`'s `$s1` "never assigned"** (HANDOFF's claimed recompiler defect): **FALSE.** Real
  prologue at `0x1005ac0` is `dmove s1, a0`; our emitted `sub_01005AB8_0x1005ab8` line 28167 DOES
  emit `SET_GPR_U64(ctx, 17, GPR_U64(ctx, 4) + GPR_U64(ctx, 0))`. The recompiler is correct here.
- **`sub_010088E8`'s prologue translation**: our emitted body matches the real disassembly
  instruction-for-instruction (frame `-0x80`, `s2=a3`, `s3=a2`, `s4=a0`, `s6=a1`, the `sp+8`/`sp+0x10`
  accumulator copy). Not the bug.
- **`$sp` frame loss / `EeScheduler` register restore**: the campaign already retired this (W226).

---

## 6. NEXT (named, small)

1. **Find the producer of the four buffers** (`0x01895630` / `0x01895780` / `0x01895810` / `0x018955a0`).
   Cheapest: a **write watchpoint in PCSX2** on `0x01895630` from boot → the writer PC, measured, not
   inferred. Then compare that producer's behaviour in our recomp.
2. **Run our recomp and dump a0/a1/a2/a3 at `sub_010088E8` entry** — confirm the counts are `0`/empty
   (predicted) vs real `0x20`. That names whether the producer never ran, or ran and produced zeros.
3. The buffers look like **PRNG/decompression state** (pseudo-random words) — plausibly GT4 decoding
   its own compressed data (`core.gt4` is the one data file `SCUS_973.28` names). Worth naming.

**No fix landed. No speedup claimed.** What this dish bought: the wall is proven a **runtime/producer
divergence**, not "game state", and it is located at the producer, not the merge.

---

## 7. THE SIDE-BY-SIDE, MEASURED (the campaign's W219/W220 claim, now proven against hardware)

Both sides at the `sub_010088E8` entry, same four input structs (count at `+8`, data base at `+0x14`):

| reg | **Real GT4 (PCSX2)** count | **Ours** count | Real base `w0` | Ours base `w0` |
|---|---|---|---|---|
| a0 | `0x0` | `0x1` | — | `0x0` |
| a1 | **`0x20` (32)** | **`0x1`** | populated | `0x1` |
| a2 | **`0x20` (32)** | **`0x1`** | populated | `0x0` |
| a3 | **`0x20` (32)** | **`0x1`** | populated | `0x1` |

**Real GT4's parser builds 32-element populated streams; ours builds length-1, all-but-empty
streams.** This is the campaign's W219/W220 "length-1 all-zero stream" — now confirmed as a real
divergence against the console, not a game-state artifact.

Probe: `VULCAN4_W227_MERGE` in `ps2_runtime.cpp::dispatchGuestBranch` (catches the inline
`jal 0x10088e8`; the harness arrival loop never sees it). Raw: `/tmp/opencode/w227r1.log`.

### What is NOT the cause (measured this dish)

- **CORE.GT4 is read correctly.** `VULCAN4_W163_FIOREAD`: `fd=5 req=2020861 got=2020861 first: 1 1
  cc 5e 5d 0 ec bd` — the full 2,020,861 bytes land in guest RAM at `0x10d1ac0`.
- **The merge buffers are NOT raw CORE.GT4 bytes.** Searched the whole file for both real-GT4 buffers
  (`0x01895630`, `0x01895780`) and their first-8-byte prefixes: **not found**. The data is computed.
- **The refill `$s1=a0` "defect" is false** (see §5).

### NEXT, sharpened

The four structs are built by the parser chain (`sub_01005D48 → sub_01008080 → sub_01008C50`).
Something there emits **one** element where hardware emits **32**. The producer of the element list is
the target: find where the `+8` count is set, and why ours is 1. Ghidra (`sub_01008C50` and its
caller) + a PCSX2 write-watch on a real-GT4 struct base is the next measurement.

---

## 8. THE ROOT, NARROWED TO ONE LIST (measured at `sub_01008C50` entry)

Real GT4, breakpoint at the parser entry `0x01008C50` (`ra=0x010082E4`, caller `sub_01008080`):

| arg | value | meaning |
|---|---|---|
| a0 | `0x01FFFA10` | param_1 (holds `+0`=`0x01895480`) |
| **a1** | **`0x01FFF9F0`** | **param_2 — the input list** |
| a2 | `0x01FFFA00` | param_3 |
| a3 | `0x01FFFA20` | param_4 |

`param_2 + 4` = **`0x018952D0`** — a **non-null list**, whose own `+8` (count) is **`0x20` (32)**.
So `sub_01008C50`'s guard `if (*(param_2+4)==0 || *(*(param_2+4)+8)==0) bVar2=true;` is **false** on
hardware → it takes the **non-empty** path and builds the 32-element streams.

**In ours, `param_2+4` is null** → `bVar2` is true → it builds the **length-1** streams we measured in
§7. **That is the whole divergence: the input list at `0x018952D0` is populated on hardware and empty
in our recomp.**

### The list head's first writer (caught by a PCSX2 write-watch from boot)

Watchpoint on `0x018952D0..0x018952DF` fired at **PC `0x0101EA0C`**:

```
0x0101e9e8: andi  t1, a1, 0x00FF     ; fill byte = a1 & 0xFF
0x0101e9ec: sltiu t2, a2, 0x0020
0x0101ea08: pcpyld t0, v1, v1
0x0101ea0c: sq    t0, (a3)           ; <-- 16-byte store, a3 = 0x018952D0
0x0101ea10: addiu a2, -0x20
0x0101ea14: addiu a3, 0x10
0x0101ea1c: sq    t0, (a3)
0x0101ea20: beqz  v0, ->0x0101EA0C
```

That is a **128-bit MEMSET**, called from `ra=0x01010EEC`, with `a1=0` (fill zero) and `a2=0x00762D30`.
It is the **zeroing** of a large heap region, not the element producer. The producer that later writes
the 32 elements (count `0x20`, base `0x018955A0`) is the next target: re-arm the watch on the count
field `0x018952D8` and catch the **non-zero** write, or decompile the caller `0x01010EEC`.

**Reproduce:** `VULCAN4_W227_MERGE=1` + `pcsx2_connect` (see §7). Real-side raw: breakpoints at
`0x01008C50` (args) and the watch on `0x018952D0`. No fix landed.

---

## 9. THE CONTROL FLOW IS CORRECT — the divergence is DATA (final measurement)

A runtime-dispatch probe (`VULCAN4_W227_MERGE`, now catching the whole chain) proves our recomp
**calls the parser chain exactly as hardware does**, with real source PCs:

```
[w227:merge] n=1 tgt=0x1005d48 src=0x1006fb4   (sub_01005D48 called)
[w227:merge] n=2 tgt=0x1008080 src=0x10061bc   (sub_01008080 called)
[w227:merge] n=3 tgt=0x1008c50 src=0x10082dc   (sub_01008C50 called)
[w227:merge] n=5 tgt=0x10088e8 src=0x1008ffc   (sub_010088E8 called)
```

So this is **not** a resume/scheduler defect and **not** a missing call — the campaign's "the parser
chain never dispatches" (W224/W225) is definitively refuted, and W226c's "it runs" is confirmed.

The parser then **produces** the streams with `count = 0x1` on every input struct (n=5:
`a0..a3` all `count=0x1 fC=0x1`), where hardware produces `count=0x20`. **The inputs are already
wrong when they reach the parser** — the divergence is upstream, in the data-structure code that
feeds it.

Caller chain (Ghidra, xrefs): `FUN_010047c0 → FUN_01004500 → FUN_01006f90 → sub_01005D48 →
sub_01008080 → sub_01008C50 → sub_010088E8`. `FUN_01004500` reads fields `+0xc/+0x10/+0x14/+0x18/+0x20`
off a node and builds the lists — i.e. this is GT4's own list/sorted-merge library, several levels
below any loader.

**Next dish (sharp):** feed the parser a known input. Either (a) break at `FUN_010047c0`/`FUN_01004500`
in PCSX2 and read the node fields real hardware passes, vs the same in our recomp; or (b) find where
the `0x20` (32) originates and why ours is `1`. The `0x20` is the whole question.

---

## 10. THE PARSER ROOT AND A CONCRETE LEAD (32 = a 128-byte buffer)

Traced the chain to its top and caught the real invocation in PCSX2 (BP `0x010047C0`, then the buffer
read `0x010047FC`):

```
FUN_01000558 (loader top, ra=0x010005DC) → FUN_010047c0 → FUN_01004500 → FUN_01006f90
   → sub_01005D48 → sub_01008080 → sub_01008C50 → sub_010088E8 (the merge)
```

`FUN_010047c0` reads a **stream struct** passed as `param_1`; at the live call it is
`0x01FFFD10`, and its fields are:

| off | value | meaning |
|---|---|---|
| +0x00 | `0x01051a10` | vtable/obj |
| +0x04 | `0x012bf100` | buffer |
| +0x08 | `0x005d5ecc` | len |
| +0x0c | `0x012bf102` | buffer |
| +0x10 | **`0x00000080`** | **length = 128 bytes** |
| +0x14 | `0x012bf184` | **buffer — holds pseudo-random words** (`42680b29 faaceb8b …`) |
| +0x18 | **`0x00000080`** | **length = 128 bytes** |
| +0x1c | `0x012bf204` | buffer (count field = `3` here) |

**`0x80` bytes = 128 = 32 × 4 — exactly the merge's `count=0x20`.** The buffer at `0x012bf184`
holds pseudo-random words of the same kind as the merge's input. So the "32" is very likely the
**length of this stream in words**, and the divergence is that our recomp builds a length of `1` word
where hardware has `32`.

**Next measurement (small):** dump this same struct in our recomp at `FUN_01004500`
(`VULCAN4_W227_MERGE`-style probe on `targetPc==0x01004500`), and compare `+0x10`/`+0x18` (expect
`0x4` vs `0x80`) and the buffer contents. That names whether the length is miscomputed upstream or
the buffer is never filled.

---

## 11. DONE: THE PARSER INPUT STRUCT IS ALL ZEROS IN OURS (the divergence, side by side)

Probe extended to `targetPc ∈ {0x01004500, 0x010047C0}` (`/tmp/opencode/w227z1.log`):

**Ours** — `FUN_010047c0` then `FUN_01004500`, both `a0 = 0x1ffffb0`:
```
[w227:struct] i=0 p=0x1ffffb0 f4=0x0 count=0x0 fC=0x0 f10=0x0 base=0x0 f18=0x0 f1c=0x0
```

**Real GT4** — same function, `a0 = 0x01FFFD10`:
```
+0x04=0x012bf100  +0x08=0x005d5ecc  +0x0c=0x012bf102
+0x10=0x00000080  +0x14=0x012bf184  +0x18=0x00000080  +0x1c=0x012bf204
```

**So the parser's input struct is EMPTY in our recomp and POPULATED on hardware.** That is why our
parser builds a length-1 list and the merge spins — the input was never filled. This is the wall,
located to a single struct.

The struct is built in `FUN_01000558`:
```
iVar2 = FUN_010002c8();
iVar1 = *(int *)(*(int *)(iVar2 + 8) + 8);
uVar3 = (**(code **)(iVar1 + 0x14))(*(int *)(iVar2 + 8) + (int)*(short *)(iVar1 + 0x10)); // VIRTUAL CALL
FUN_0101ed00(auStack_d0, uVar3);      // fills the struct from uVar3
aiStack_50[0]=iVar2; aiStack_50[1]=0; aiStack_50[2]=0;
lVar4 = FUN_010047c0(aiStack_50);
```

**Next:** the struct is filled from `uVar3` — the return of a **virtual call** through
`(*(iVar2+8)+8)->[0x14]`. In our recomp that virtual call (or `FUN_0101ed00`) produced nothing.
Dump `uVar3` and the struct in `FUN_01000558` on both sides. That is one breakpoint and one probe.

---

## 12. THE VIRTUAL CALL, NAMED (ours) — the last link before the fix

Probe extended to the `jalr` (`sourcePc == 0x010005AC`) in our recomp (`/tmp/opencode/w227v.log`):

```
[w227:merge] n=2 tgt=0x1003b90 src=0x10005ac a0=0x10d1aa0 a1=0x10d1aa0 ra=0x10005b4 sp=0x1ffff30
[w227:merge] n=3 tgt=0x10047c0 src=0x10005d4 a0=0x1ffffb0 ...   (param_1 = sp+0x80)
[w227:merge] n=4 tgt=0x1004500 src=0x10047ec a0=0x1ffffb0 ...   (all-zero struct)
```

So in our recomp the chain is intact and the virtual method is `FUN_01003b90`, called with
`a0 = 0x10d1aa0` (a buffer next to the CORE.GT4 image at `0x10d1a50`). Ghidra: `FUN_01003b90` is a
**string builder** — it uppercases `*param_1` and concatenates a prefix/suffix (tables `0x103d558`,
`0x103d568`), caching the result in `param_1[1]` and returning it.

**The exact open question, one measurement wide:** does `FUN_01003b90` return the same string on
hardware, and does `FUN_0101ed00(sp, v0)` then fill `sp+0x80..sp+0x9c`? Ours leaves that region zero.
Next: break at `0x010005B8`/`0x010005D4` in PCSX2 and read `v0` and `sp+0x80..sp+0x9c`, vs the same
two probes in our recomp.

**Honest ceiling:** I did NOT reach the menu in this session. The divergence is now pinned to
"the parser's input struct is empty in ours" — a single struct filled by one virtual call. That is
the next dish, and it is small.

---

## 13. THE CHAIN IS IDENTICAL THROUGH `FUN_0101ed00` — so the filler is elsewhere

Checked both sides (PCSX2 `step_over` + our recomp probe):

| step | real GT4 | ours |
|---|---|---|
| virtual call target (`0x10005ac` jalr) | **`0x1003b90`** | **`0x1003b90`** |
| virtual call return (`v0`) | **`0x10d1a50`** = `"cdrom0:\CORE.GT4;1"` | **`0x10d1a50`** (same) |
| `FUN_0101ed00(a0, a1)` | `a0=sp`, `a1=0x10d1a50` | `a0=sp`, `a1=0x10d1a50` |

`FUN_0101ed00` is a **SIMD `strcpy`** (real disasm: `psubb`/`pnor`/`pand` haszero, then copy) —
**our emitted stub `ps2_stubs::strcpy` is correct.** So `strcpy(sp, "cdrom0:\CORE.GT4;1")` copies 19
bytes to `sp`; it does **not** fill `sp+0x80..sp+0x9c`, which is what `FUN_010047c0` reads.

**So the parser's stream struct at `sp+0x80` is filled by something else** — the visible
`FUN_01000558` code only writes `sp+0x80` (`iVar2`), `sp+0x84` (`0`), `sp+0x88` (`0`). The populated
fields on hardware are `+0x04=0x012bf100 +0x08=0x005d5ecc +0x0c=0x012bf102 +0x10=0x80
+0x14=0x012bf184 +0x18=0x80 +0x1c=0x012bf204` — a file/data **stream object** (two buffers of `0x80`
bytes). Who writes it is the next measurement: watch `0x01FFFD10..0x01FFFD2C` in PCSX2 from boot.

**Summary of the whole dish:** PCSX2 A/B is live and verified; the merge loop is proven a real
divergence; the parser input struct is proven empty in ours and populated on hardware. The exact
filler of that struct is the one remaining unknown.







