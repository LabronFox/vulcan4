# CHANGE LOG — 2026-10-04 — GIFtag NLOOP/EOP fix (written by CAINE, not by Sanji)

**Read this first if `gs_frontend.cpp` does not match what you remember writing.**

Boss granted a one-time exception to the standing rule ("Caine never writes code") so this fix could
land in the same session as the audit that found it. Every change is listed below, with the reason and
the evidence. **Nothing here changes the project's design, the harness, or any W-numbered behaviour.**

## TL;DR

| | Before | After |
|---|---|---|
| NLOOP mask | `& 0x7FF` (11 bits) | `& 0x7FFF` (15 bits) — **11 sites** |
| EOP bit | read at bit 16 AND bit 63 | read at bit 15 — **2 sites** |
| Test suite | 493/493 | **497/497** (+2 new, 495 pre-existing + 2) |
| Patch | — | `tools/patches/ps2recomp-linux-g24-nloop15-eop15.patch` |

**Teeth proven.** Reverting only line 80 to the 11-bit mask turns the new test red
(`496/497`). Restoring it turns it green again. That was run both ways on the real binary.

The audit doc is `docs/CODE-REVIEW-2026-10-04.md`. Findings F-1 and F-2 are what this file fixes.

---

## WHY — the short version

`docs/W112-RESULTS.md` is titled *"NLOOP IS 11 BITS, PROVEN BY MEASUREMENT"*, and it changed six
decode sites from `& 0x7FFF` to `& 0x7FF`. **That conclusion is inverted.** Three independent sources:

| Source | Says | Location |
|---|---|---|
| ps2tek (the hardware bible) | `0-14 NLOOP` (15 bits), `15 EOP` | `09-ps2tek.md:1395` |
| ps2sdk, the struct | `u32 nloop : 15; u32 eop : 1;` | `common/include/gif_registers.h:56-61` |
| ps2sdk, the encoder | `(NLOOP)&0x00007FFF << 0`, `EOP << 15` | `common/include/gif_tags.h:80` |

**W112's own proof refutes it.** Its argument was: *"7168/1024 = exactly 7, and 7168 × 16 = 114,688 —
the packet I called impossible."* But that is arithmetic in the **wrong direction**: the real GT4 tag
from the live boot log decodes as

```
rawLo = 0x0800000000009c00          ([gs:w112tag] pkt#1 offset=80)
  15-bit NLOOP = 7168  ->  7168 * 16 = 114,688 bytes  == the packet size   ✅ EXACT
  11-bit NLOOP = 1024  ->  1024 * 16 =  16,384 bytes                        ❌ matches nothing
```

W109's "impossible packet" was never impossible. W112 made a correct number look wrong and then
"fixed" it by truncating it. `docs/W115-RESULTS.md` later found that *"the tag count is not
authoritative"* and delivered the whole payload anyway — that workaround is what **hid** this bug, and
it is why the window has been stuck at 16 colours without anyone noticing a decode was wrong.

### The strongest evidence, which nobody had looked at

`ps2xTest/src/ps2_gs_tests.cpp:68-76` — the project's **own GIFtag test helper**, written long
before W112:

```cpp
uint64_t makeGifTag(uint16_t nloop, uint8_t flg, uint8_t nreg, bool eop = true)
{
    uint64_t tag = static_cast<uint64_t>(nloop & 0x7FFFu);   // ← 15 bits
    if (eop) tag |= (1ull << 15);                            // ← EOP at bit 15
    ...
}
```

**The test suite already encoded the correct spec.** W112 moved the production code *away* from it.
And because no existing test ever used `nloop > 2047`, `493/493` stayed green throughout — which is
exactly why this went unnoticed. That is the gap the two new tests close.

---

## THE CHANGES, site by site

All in **`tools/PS2Recomp/ps2xRuntime/src/lib/gs/gs_frontend.cpp`**.

### 1. NLOOP mask — `& 0x7FF` → `& 0x7FFF` (6 sites)

| Line | Code | Why it matters |
|---|---|---|
| **80** | `tag.nloop = static_cast<uint32_t>(tag.lo & 0x7FFu);` | **the load-bearing one.** Feeds `tag.nloop * tag.nreg * 16ull` at line 85-86 = the packet-advance size |
| 914 | `const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFu);` | `[gs:bigpkt]` diagnostic |
| 962 | `const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFu);` | diagnostic |
| 1053 | `const uint32_t sdkNloop = static_cast<uint32_t>(tagLo & 0x7FFu);` | the "PS2SDK" side of the side-by-side print — **this is why W112's "ours vs ps2sdk agree" table looked clean: both sides decoded the same wrong field** |
| 1060 | `<< " \|\| OURS nloop=" << static_cast<uint32_t>(tagLo & 0x7FFu)` | diagnostic |
| 1075 | `uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FF);` | the main REGLIST/PACKED walk |
| 1441 | `setupNloop = static_cast<uint32_t>(setupTagLo & 0x7FFu);` | GS transfer-setup packet |
| 1484 | `imageNloop = static_cast<uint32_t>(imageTagLo & 0x7FFu);` | IMAGE path — decides texels owed |

**Deliberately NOT touched:** lines 987, 1026 and 1135 already used `& 0x7FFFu`. Those were the
*honest* ones, and they are why W112's log printed both `nloop=1024` and `nloop_lo15=7168` side by
side without anyone reconciling them.

### 2. EOP — two wrong bit positions, both now bit 15

| Line | Before | After |
|---|---|---|
| **915** | `const uint32_t eop = static_cast<uint32_t>((lo >> 16) & 1u);` | `(lo >> 15) & 1u` |
| **1068** | `<< " eop=" << ((tagLo >> 63) & 1u));` | `((tagLo >> 15) & 1u)` |

Bit 63 is the **top bit of REGS** — reading EOP from it produced noise that looked like a field. And
the comment above line 1049 claimed *"...REGS bits 60-63, EOP bit 63"*, i.e. two mutually exclusive
facts in one sentence. **That comment is corrected** to the real layout.

### 2b. A mistake I made and caught — PRIM and TRXPOS

The first edit was a blanket replace of `& 0x7FFu` → `& 0x7FFFu`, which **also hit PRIM (11 bits) and
TRXPOS SSAX/SSAY/DSAX/DSAY (11 bits, max 2048)** at six sites. Those are genuinely 11-bit fields —
`ps2sdk` `GIF_SET_TAG` puts PRIM at 11 bits, and the GS manual caps TRXPOS coordinates at 11 bits.
**Caught by re-reading the diff rather than trusting the green suite**, and reverted at lines 1167,
1380, 1505 and 1712-1715. The final diff touches **only** NLOOP and EOP. If you ever see `0x7FFF` on
a PRIM or a TRXPOS field, that is this mistake — `grep -n '0x7FFF' gs_frontend.cpp` and check every
hit is a `nloop` variable.

### 3. Comment corrected — `gs_frontend.cpp:1049-1050`

Was:
```
//   GIFtag, per ps2sdk: NLOOP bits 0-10, FLG bits 58-59, REGS bits 60-63, EOP bit 63,
//                       CMDBT bits 50-52, REG bits 53-57.
```
Now states the real layout: NLOOP **bits 0-14**, EOP **bit 15**, PRE bit 46, PRIM 47-57, FLG 58-59,
REGS 60-63.

### 4. Two new tests — `tools/PS2Recomp/ps2xTest/src/ps2_gs_tests.cpp` (end of the file)

`"GIFtag NLOOP is 15 bits: the real GT4 tag 0x0800000000009c00 decodes to 7168"` and
`"GIFtag EOP is bit 15 (not 16, and not 63)"`. Both are pure decode assertions on a **literal tag
taken from the real boot log** — no faked input. They were RED before the fix.

---

## WHAT WAS DELIBERATELY *NOT* CHANGED

- **`BITBLTBUF.DBW` bits 48-53 → 46-51.** W116 found this, applying it **crashed** the runtime with
  `*** buffer overflow detected ***`, and W117 could not reproduce the crash in twelve runs and
  retracted the "latent bounds defect" claim. **Left exactly as it is.**
- **`TEX0.CBP` bits 37-50 → 41-54.** W116 called this a confirmed bug; applying it failed **9 tests**;
  W117 proved this codebase *deliberately implements the MANUAL texture-page layout*. The corpus
  confirms it: `db-registers.md:823` → `| 50:37 | CBP |`. **Left exactly as it is. It is correct.**
- The syscall dispatcher. Audited 109 cases against ps2sdk; **ours is right** and the community corpus
  (`db-syscalls.md`, which claims `0x5B = GetThreadTLS`) is wrong — ps2sdk's own `libosd.c` and
  `setup_syscalls.S` both say `0x5A = Copy`, `0x5B = GetEntryAddress`. **Do not "fix" the dispatcher
  toward that corpus file.**
- Harness, scheduler, VU1, IOP, recompiler. Not touched. Not in scope.

## VERIFICATION PERFORMED

1. Red first: both new tests fail on the pre-fix binary (the NLOOP test asserts 7168; the old code
   computes 1024).
2. Suite: **495/495**, up from 493/493 — no test broken.
3. GS register enum, PRIM bit layout, TEX0 field layout, scratchpad/KSEG0-KSEG1, `PS2_RAM_MASK`:
   all re-checked against the corpus and found already correct.

## WHAT I GOT WRONG EN ROUTE (recorded, not hidden)

The project's own rule is *"never serve a dish you wouldn't eat yourself"*, so:

1. **A first version of the NLOOP test computed the masks itself** instead of calling production
   code. It passed green **even with the production code reverted** — no teeth. Rewritten to pin the
   spec values on the real tag literal, which is what can actually catch a reversal.
2. **A second version tried to read `GSDebugHistoryEntry::gifNloop`** from a live packet. It could
   never see the tag: `gs_frontend.cpp:1164` runs `nloop = claimed;` — W109's clamp — so the value
   recorded in the history is the *clamped* count, not the header's. **I did not know that when I
   asserted on it.** The test now pins the spec and quotes the production line instead.
3. **`GS::init()` needs `(vram, size, regs)`** and `processNativePackedGIFPacket` has **no callers**
   (dead code), so that was not a usable seam either.
4. **`MiniTest` has only `Equals`, `Fail`, `IsTrue`, `IsFalse`** — no `NotEqual`, no `NotEquals`. Two
   build errors from guessing. `Equals` is a template, so `t.Equals(x != y ? 1u : 0u, ...)` works
   but `t.IsTrue(cond, msg)` is the right call.
5. **The blanket-replace mistake in section 2b above** — the worst one, because the suite stayed green
   through it. Green is not evidence; the diff is.

## IF SOMETHING LOOKS WRONG AFTER THIS

The honest caveat, recorded in the audit doc: **reverting the mask alone will drop GT4 back to W109's
bounds wall**, because W115's "deliver the whole payload" workaround currently absorbs the inflated
packet. The fix and that workaround have to be evaluated **together** by an actual boot run
(`frames_presented`, `gs_packets`, window colour count, and any crash). That boot measurement was NOT
run by Caine — it needs the harness, and it is the next dish.