# W225 — W224's "inlined" claim is REFUTED: the emitted call is real; its CALLER never runs

**Dish:** `24-w224-consumer-contract` / W225. **Result:** ❌ no picture change; gate fails.
**Outcome (P2, option-b variant):** the emitted code at `0x1008FFC` is a **real
`dispatchGuestBranch` DirectCall** to `0x10088E8` — **not inlined**. The reason the probe sees no
dispatch is that the **containing function `sub_01008C50` is never entered**; `sub_010088E8` runs only
via the **resume/scheduler** path.

## The emitted code at `0x1008FFC` (the evidence)

From `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`, inside the emitted body of
`sub_01008C50` (`Address: 0x1008c50 - 0x1009068`, line 45157):

```
46383: // 0x1008ff8: 0x8ea60004  lw  $a2, 0x4($s5)
46384: ctx->pc = 0x1008ff8u;
46385: ... SET_GPR_S32(ctx, 6, ...READ32(ADD32(GPR_U32(ctx, 21), 4)));   // a2 = [s5+4]
46386: // 0x1008ffc: 0xc40223a  jal  func_10088E8
46387: ctx->pc = 0x1008FFCu;
46388: SET_GPR_U32(ctx, 31, 0x1009004u);
46389: ctx->pc = 0x1009000u;
46390: ctx->in_delay_slot = true;
46393: // 0x1009000: 0x8e640004  lw  $a0, 0x4($s3) (Delay Slot)
46393: ... SET_GPR_S32(ctx, 4, ...READ32(ADD32(GPR_U32(ctx, 19), 4)));    // a0 = [s3+4]
46395: ctx->pc = 0x10088E8u;
46396: if (::vulcan4W221JalrProbe()) { ::vulcan4W221JalrPrint(0x1008FFCu, 0x10088E8u, ...); }
46397: if (!runtime->dispatchGuestBranch(rdram, ctx, 0x10088E8u, 0x1008FFCu, 0x1009004u,
                                         PS2Runtime::GuestBranchKind::DirectCall, "JAL")) return;
```

**It is a `dispatchGuestBranch` DirectCall, not an inline.** The `jal` target computes to
`0x01000000 | (0x223A << 2) = 0x01008CE8`? No — the emitted call passes the **already-resolved**
`0x10088E8u` (Rabbitizer resolved the jal), so the target is `0x10088E8` exactly. **W224's "the emitter
inlines it (`emitDirectFunctionJumpIfAvailable`)" is REFUTED** — that helper only applies to
`StaticBranchKind::Jump`, never `Call` (`control_flow_emitter.cpp:218-219`).

## Why the probe sees no dispatch — the CALLER never runs

The emitter-layer W221 probe (armed) records, in a full run (`w250`, `livelocked_in_syscall`):

- Targets in `0x10088xx`: **0** (so `0x10088E8` is never a dispatch target).
- `target=0x01008c50` (`sub_01008C50`): **0** — the caller itself is **never dispatched**.
- Sources in `0x1008xxx`: only the spin's own internal sites `0x010089c8` (15) / `0x010089d4` (21).

At halt, **tid1's frame is `pc=0x010057F0 ra=0x01005898`** — inside `sub_010088E8` (the spin), reached by
the **resume/scheduler** (the generated body's `switch (ctx->pc)`), not by a call from `sub_01008C50`.

**So: the emitted call is real; the path that would execute it (`sub_01008C50` → `0x1008FFC`) is never
entered.** `sub_010088E8` is reached by resume, which is why dispatch-level and emitter-level probes both
see `0x10088E8`/`0x1008FFC` **zero times** — a real zero, now explained, not a blind probe.

## The four streams (from the emitted `[sN+4]` loads, static)

At `0x1008FFC` the streams are `a0=[s3+4]`, `a1=[s4+4]`, `a2=[s5+4]`, `a3=[s2+4]`. The one the spin
merges (`func_1005870`'s `a0 = 0x1FFFBA0`, W218) has bound `+8=1`, base `+0x14=0x1895480` — the
single-element, all-zero stream with no producer (W219/W220/W223c). Because the caller never runs, no
producer is invoked to raise any stream's bound; the merge is fed whatever `sub_010088E8` was **resumed**
with.

## Positive control

The W221 emitter probe fires **87 times/30 s** with the knob on and **0** with it off (W221), and here it
records real dispatches (`0x10089c8`, `0x10089d4`, `0x1028xxx`) — so its zero for `0x10088E8`/`0x1008C50`
is a **real zero**, not a blind probe.

## Verdict

**`sub_010088E8` is not inlined; its caller `sub_01008C50` is simply never entered in these runs, and the
merge runs from a resumed frame.** The missing producer is therefore not a broken callback or an inlined
call — it is that the parser step that would feed the merge (`sub_01008C50`, a game-logic step, W224) does
not run on this boot path. This is a **game-state** wall, consistent with W183/W184/W224.

## Gate (authoritative, honest)
`GATE FAIL` (v3, byte-identical disclaimer). Suite **497/497**.

## NEXT
Find what should call `sub_01008C50` (`0x10082DC` in the decoder `sub_01008080`) and why that path is
not reached on this boot — i.e. why `sub_01008080`'s call at `0x10082DC` never runs. If it is gated on
input/state absent from the build, name it; that is the game-state step the campaign is waiting on.
