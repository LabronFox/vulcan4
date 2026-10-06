# W228 — PHASE 1: the parser stream struct's writer is `FUN_01004308`, and our emitted copy is CORRECT

**Goal:** GT4 past the 2005 Sony disclaimer → the menu.
**Gate:** `bash /home/or/vulcan4/.auto/verify-menu.sh` (capture measurably ≠ the disclaimer).
**Result of this dish:** no fix, no picture change. But the W227 "who fills the struct" question is
**answered**, and the writer is **exonerated**. The divergence is confirmed to be **upstream of
`FUN_01004308`**.

This dish was **read-only investigation** (PCSX2 + Ghidra + reading emitted C++). Nothing was rebuilt,
no product code changed. The only source edit was a tooling convenience (see §7).

---

## 1. The question inherited from W227

W227 §11–13 proved the parser's **input stream struct** is **empty in ours** and **populated on
hardware**, and named the next measurement:

> watch `0x01FFFD10..0x01FFFD2C` in PCSX2 from boot — who writes it?

The struct (hardware, at the live call) is:

| off | value | meaning |
|---|---|---|
| +0x00 | `0x01051a10` | vtable/obj |
| +0x04 | `0x012bf100` | buffer |
| +0x08 | `0x005d5ecc` | len (6119116) |
| +0x0c | `0x012bf102` | buffer |
| +0x10 | `0x00000080` | length = 128 bytes = **32 words** |
| +0x14 | `0x012bf184` | buffer — pseudo-random words |
| +0x18 | `0x00000080` | length = 128 bytes |
| +0x1c | `0x012bf204` | buffer |

`0x80` bytes = 128 = 32 × 4 = exactly the merge's `count=0x20`. That is why the empty struct makes our
merge build a length-1 stream and spin.

---

## 2. THE MEASUREMENT — the write-watch, and the writer caught

PCSX2 was already booted to the parser area (paused at `0x010043f0`). Cleared all breakpoints, then:

```
set_watchpoint  0x01FFFD10 .. 0x01FFFD2C   type=write  action=break
continue
list_watchpoints   ->  0x01fffd10-0x01fffd2c | 1 hits | last_PC=0x00000000
pause              ->  Paused at PC=0x01004404
```

`last_PC` came back `0` (the DebugServer does not populate the PC for watchpoint hits), but the
**paused PC is the tell**: `0x01004404`, which is the store `sw $v0, 0x14($s2)`.

**The writer is `FUN_01004308`**, called from `FUN_01004500` at `0x0100451c` (Ghidra xrefs confirm
this is its only caller).

---

## 3. Hardware register state at the write (PCSX2, PC `0x01004404`)

```
s0 = 0x012BF100     (the buffer)
s1 = 0x01FFFD14     (struct + 4)
s2 = 0x01FFFD10     (the struct)   s3 = 0x01FFFD10
v0 = 0x012BF184     (a buffer)
v1 = 0x012BF182     (a buffer)
a0 = 0x01FFCB70     (this frame's sp)
a2 = 0x00000100
a3 = 0x00000080     (the 0x80 length)
ra = 0x010043AC
```

The stores this function makes (disassembled live):

```
0x010043f0:  sw a2, 0x18(s2)          ; +0x18 = ushort at v1 (0x0080)
0x010043f4:  addu a2, a3, a2
0x010043f8:  lw v0, 0(s1)
0x010043fc:  addu v0, v0, a3
0x01004400:  addiu v0, v0, 4
0x01004404:  sw v0, 0x14(s2)          ; +0x14  <-- the watchpoint fired here
0x01004408:  lw v1, 4(s1)
0x0100440c:  subu v1, v1, a2
0x01004410:  addiu v1, v1, -4
0x01004414:  sw v1, 0x20(s2)          ; +0x20
0x01004418:  lw v0, 0(s1)
0x0100441c:  addu v0, v0, a2
0x01004420:  addiu v0, v0, 4
0x01004424:  jal 0x01010BD0
0x01004428:  sw v0, 0x1c(s2)          ; +0x1c (delay slot)
```

**So `FUN_01004308` IS the struct filler**, filling `+0x14/+0x18/+0x1c/+0x20` from the buffer at
`s1`/`v1` (`0x012BF1xx`). Ghidra's decompile shows it is guarded by `if (param_1[2] == 0)` — a lazy
init; on hardware `param_1[2]` (`+0x08` = `0x005d5ecc`) is already non-zero on the call caught, so the
body runs exactly once.

---

## 4. THE EXONERATION — our emitted `sub_01004308` is byte-for-byte equivalent

`/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`:

- function: `sub_01004308_0x1004308` at **line 18248**
- stores:
  - line 18488: `0x10043f0  WRITE32(s2+24, a2)`
  - line 18503: `0x1004404  WRITE32(s2+20, v0)`
  - line 18515: `0x1004414  WRITE32(s2+32, v1)`
  - line 18533: `0x1004428  WRITE32(s2+28, v0)` (delay slot of the `jal 0x1010BD0`)

Same four offsets, same source registers, same arithmetic as the hardware disassembly above. **The
filler is not the bug.**

---

## 5. CONCLUSION — the divergence is upstream of `FUN_01004308`

Since the filler code is correct, ours builds an empty struct for one of:

1. **The function is never reached** in ours (control-flow / the derail), or
2. it is reached with a **different struct pointer** (`s2` ≠ the one `FUN_010047c0` later reads), or
3. the **inputs differ** — `s1`/`v1`/`a3` point at empty or different data.

This is consistent with W227 §9's conclusion: *"the divergence is in the data-structure code that FEEDS
the parser, several levels up."*

Note on the W227 §11 comparison: ours was read at struct `0x1ffffb0` (our shallower stack), hardware at
`0x01FFFD10` — **different stack depths**, so the two addresses are not the same physical slot, but they
are the same *logical* struct (the one passed to `FUN_010047c0`). The emptiness is real.

---

## 6. NEXT — the one measurement that branches

Add a probe at our `FUN_01004308` **entry** (`targetPc == 0x1004308`), dumping `a0,a1,a2,a3,s0,s1,s2,ra,sp`
and the struct contents, and compare to hardware's `s1=0x01FFFD14 s2=0x01FFFD10 a3=0x80`.

- If it **never fires** → ours never reaches the filler → chase the control-flow derail
  (`pc_outside_generated_table`, the corrupted-`ra` bug the campaign chased in W148).
- If it **fires with different inputs** → walk the caller chain (`FUN_01004500` → `FUN_010047c0` →
  `FUN_01000558`) and find where `s1`/`v1`/`a3` come from vs hardware.

---

## 7. Tooling note (the only edit this dish)

`/mnt/ssd/tools/PCSX2-MCP/pcsx2-mcp-server/src/debug-server-client.ts` line ~174: the DebugServer
command timeout was **10s → 3s**. The server answers in milliseconds; a 10s stall per call made the
MCP tool look hung. Rebuilt with `npm run build` (takes effect on next opencode restart).

## 8. Assets / commands used

- PCSX2: `systemd-run --user --unit=pcsx2-gt4 ... run-pcsx2-gt4.sh -debugger`; DebugServer on
  `127.0.0.1:21512`; Pine `28011` not connected.
- ISO: `/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso`; ELF `/mnt/ssd/gt4/work/SCUS_973.28`.
- Ghidra (GhydraMCP port 8192): `SCUS_973.28` loaded, 870 functions.
- Emitted code: `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`.

**Suite:** unchanged. **No fix landed. No picture change.** Menu not reached.
