# CODE + DOC REVIEW — 2026-10-04, audited against the PS2 hardware corpus

**Method.** Every claim below was checked three ways: (1) the installed corpus at
`/mnt/HDD/Projects/agent-reference/ps2-recomp-Agent-SKILL/resources/`, (2) **ps2dev/ps2sdk source
fetched live**, and (3) the project's own live boot log. A finding that could not be reproduced
from data is marked UNPROVEN and is not claimed.

**Scope.** 24,818 lines of docs, the GS layer (5,480 lines), the syscall dispatcher (109 cases),
`ps2_memory.cpp`, and `/mnt/ssd/vulcan4-build/run/boot_w153_3.log`. Harness and VU1 not audited.

---

## 🔴 F-1 — W112's "real fix" is INVERTED. NLOOP is 15 bits, the code reads 11.

**Status: PROVEN, load-bearing, and it is the project's own regression.**

`docs/W112-RESULTS.md` is titled *"NLOOP IS 11 BITS, PROVEN BY MEASUREMENT"* and it changed six
decode sites from `& 0x7FFF` to `& 0x7FF`. Three independent sources say the opposite:

| Source | Says | Where |
|---|---|---|
| **ps2tek** (corpus `09-ps2tek.md:1395`) | `0-14 NLOOP` = **15 bits**, `15 EOP` | the RE corpus |
| **ps2sdk `gif_registers.h:58`** | `u32 nloop : 15; u32 eop : 1;` | struct, not a macro |
| **ps2sdk `gif_tags.h:80` `GIF_SET_TAG`** | `(NLOOP)&0x00007FFF << 0` and `EOP << 15` | the encoder |

**And the project's own measurement refutes it.** W112's proof was arithmetic: *"7168/1024 = exactly
7, and 7168 × 16 = 114,688 — the packet I called impossible."* That arithmetic is the **smoking
gun against itself**:

```
real GT4 tag   rawLo = 0x0800000000009c00        (from boot_w153_3.log, [gs:w112tag])
  15-bit NLOOP  = 7168   →  7168 × 16 = 114,688 bytes  = the EXACT packet size ✅
  11-bit NLOOP  = 1024   →  1024 × 16 =  16,384 bytes                  ❌
```

The 11-bit read does not match the packet. The 15-bit read matches **exactly**. W109's "impossible
packet" was never misread — it was correct all along, and the fix hid a real number by truncating it.
**1 of the 3 distinct real GT4 tags in the live boot log is truncated by this mask.**

**Why it is load-bearing, not cosmetic:** `gs_frontend.cpp:80` feeds it straight into the packet
advance —

```cpp
tag.nloop = static_cast<uint32_t>(tag.lo & 0x7FFu);   // line 80
const uint64_t payloadBytes64 = tag.nloop * tag.nreg * 16ull;   // line 85-86
```

That is the packet-size computation. Six sites use the mask (L80, 914, 962, 1075, 1441, 1484); the
IMAGE-path ones at 1441/1484 decide how many texels are owed.

**What W115 then built on top of it.** W115 concluded *"the IMAGE tag's count is not authoritative;
the transfer is"* and delivered the whole payload packet anyway — which papered over the truncation
and turned a decode bug into a workaround that *works*. So the current window (16 colours) is not
evidence the bug is harmless; it is evidence a workaround is holding it up.

**Not a one-line revert.** Reverting alone drops GT4 back to W109's bounds wall. The correct fix is
15-bit NLOOP **plus** honouring the packet's real size. Recommend a red test first: decode
`rawLo=0x0800000000009c00` and assert NLOOP == 7168.

## 🔴 F-2 — EOP is read from two different, both-wrong bits.

ps2sdk: EOP is **bit 15** (`GIFTAG0.eop`, and `GIF_SET_TAG ... << 15`). `gs_frontend.cpp` reads it
**twice, differently, and neither is 15**:

- `:915` — `(lo >> 16) & 1u`  → bit 16
- `:1068` — `((tagLo >> 63) & 1u)` → bit 63, **which is the top bit of REGS**

The `:1068` one is not just wrong, it asserts a fact about a different register in the comment above
it: *"per ps2sdk: ... REGS bits 60-63, EOP bit 63"* — the comment states two mutually exclusive
claims. Any tag analysis built on either line is noise. Note this is also **why W112's tag-vs-PS2SDK
comparison table looked clean**: both sides decoded the same wrong field.

## 🟡 F-3 — The syscall-number conflicts are a corpus defect, NOT a code defect.

First pass of this audit found 45 "conflicts" between our dispatcher and ps2sdk's `syscallnr.h`.
**37 of them are my parser's fault** (it mishandled `-0xNN` negatives and `#define` alias chains such
as `__NR_SetupThread __NR_RFU060`). After fixing both, the *real* finding is the opposite of what it
looked like:

**Our dispatcher is RIGHT and the community corpus is WRONG.** The installed
`resources/db-syscalls.md` claims `0x5B = GetThreadTLS` and `0x5A = QueryBootMode`. ps2sdk proves
otherwise in two independent places:

- `ee/kernel/src/libosd.c:37-40` — `SyscallPatchEntries[] = { {0x5A, &kCopy}, {0x5B, ...} }`
- `ee/kernel/src/setup_syscalls.S` — `Copy: li $v1, 0x5A` / `GetEntryAddress: li $v1, 0x5B`

This is exactly the number VULCAN 4 once burned a day on, and our code had it right. **Do not "fix"
the dispatcher toward `db-syscalls.md`.** Consider patching our local copy of that file with a
header saying ps2sdk wins, so the next audit doesn't repeat this.

**Genuinely unresolvable from source:** 0x3C/0x3D (we say `SetupThread`/`SetupHeap`; ps2sdk says
`RFU060`/`RFU061` and only aliases them) and 0x54/0x56/0x57/0x59 (we say event-flag ops; ps2sdk says
`xlaunch`/TLB/`ExpandScratchPad`). Both readings appear in the wild. **But it does not matter today:**
GT4's live log issues `SYSTABLE n=0x83` and `n=0x5a` only. Verified — no live wall depends on it.

## 🟢 F-4 — W117's retraction is CORRECT and the corpus confirms it.

W116 reported "TEX0.CBP bits 41-54" as a confirmed bug; W117 retracted it, saying the code
deliberately implements the MANUAL layout. The corpus agrees with W117 — `db-registers.md:823`:
`| 50:37 | CBP | CLUT base pointer |`. **Our code is right; the retraction was right.** Worth noting
in `LIMITATIONS.md`, which still carries the old CBP/DBW items in a way that reads as open.

## ⚪ F-5 — What is NOT wrong (checked, so nobody re-checks)

- **GS register enum + PRIM layout** — `gs_types.h:46-` matches `db-registers.md:753-765` exactly
  (PRIM 2:0, IIP 3, TME 4, FGE 5, ABE 6, AA1 7, FST 8, CTXT 9, FIX 10). Correct.
- **TEX0 field layout** — TBP0 13:0, TBW 19:14, PSM 25:20, TW, TH, TCC, TFX, **CBP 50:37**, CPSM,
  CSA — all match the corpus. Correct.
- **Scratchpad / KSEG0-KSEG1 / RAM mask** — `0x70000000-0x70003FFF`, `addr & 0x1FFFFFFF`,
  `PS2_RAM_SIZE 0x02000000` — match `db-memory-map.md`. Correct.
  ⚠️ Caveat: that corpus file cites `ps2_memory.cpp` as a source, i.e. **it partly documents OUR code**,
  so it is not independent evidence for those rows.
- **Suite 493/493**, the 16-colour window, `functions_entered=2287` in `boot_w153_3.log` — real,
  and the live numbers are far past what `STATUS.md` (written 2026-09-30, "functions_entered=25")
  still claims.

## 📌 THE ONE-LINE SUMMARY FOR THE CAPTAIN

The project's own discipline — *measure, retract, don't fake* — is genuinely excellent, and F-4 proves
it works. But **W112 is the exception: a real fix that is actually a regression**, adopted on
arithmetic that contradicted the spec it was checking against, and then stabilised by a workaround
(W115) so the symptom stopped looking like the cause. Fix F-1 with a red test, not a revert.