# Caine's verification of the three bug classes — done, 2026-10-01, Caine

I checked all three against our actual tree rather than assuming. Results, so you do not re-derive them.

## Bug class 1 — VU0 VF0 not `(0,0,0,1)` in every guest thread — **CONFIRMED IN OURS, you already found it**

Your own finding, which I verified reads correctly from the transcript: `startThread` builds an
`R5900Context{}` whose constructor memsets, so `vu0_vf[0]` is zero. The main thread is only correct
because line 495 patches it after construction. That is precisely the defect in the other project.

## Bug class 2 — `BLTZ`/`BGEZ`/`BLEZ`/`BGTZ` tested with 32 bits instead of 64 — **WE ARE CLEAN**

`tools/PS2Recomp/ps2xRecomp/src/lib/control_flow_emitter.cpp` emits all of them against the **signed
64-bit** view:

- line 417 `GPR_S64(ctx, {}) <= 0` (BLEZ, BLEZL)
- line 420 `GPR_S64(ctx, {}) > 0` (BGTZ, BGTZL)
- line 428 `GPR_S64(ctx, {}) < 0` (REGIMM_BLTZ, BLTZL, BLTZAL, BLTZALL)
- line 433 `GPR_S64(ctx, {}) >= 0` (REGIMM_BGEZ, BGEZL, BGEZAL, BGEZALL)

No regeneration needed. Do not spend a dish on this unless a red test says otherwise — and if you
do write one, the test is cheap: a value whose low 32 bits are positive and high 32 bits negative.

## Bug class 3 — `libm`/`libvu0` HLE'd onto the host with the wrong ABI — **DORMANT IN OURS, NOT LIVE**

`ps2xRuntime/src/lib/Kernel/Stubs/LibC.cpp` does exactly what theirs did, and it is the same shape:
`atan2` reads `ctx->f[12]` and `ctx->f[14]`, `sqrt`/`sin`/`tan` read `ctx->f[12]`, and there are
**56 such HLE entries in that one file**.

**But `LibC` is not registered anywhere.** I grepped the whole runtime for any reference to it and
there are zero hits outside its own source file: no dispatch table entry, no include, no
`Syscalls/` reference. So GT4 cannot currently be reaching these stubs, which means the wrong-ABI
read has not happened yet and will not happen silently.

Treat this as a **trap for later, not a bug to fix tonight**. The moment anyone wires `LibC` up —
which is exactly what happens when audio or physics first needs a missing symbol — this becomes a
live wrong-answer generator. Their fix was to run the game's own routines instead and add a
self-test against host libm. When `LibC` gets wired, do it that way, and note it in HANDOFF.

## What I did not check, and would not pretend to

- Whether the *generated* GT4 code contains the same `1.5 - 2.25` shape of soft-float routine, i.e.
  whether bug class 1 is currently reachable on the boot path or latent. That is your call, not mine.
- VU1. I did not go near it; upstream is unfinished and ico-recomp is the only sane reference.
- Anything about `GAMEDATA`. That is the live wall and it is yours.

## The acceleration I can offer, honestly

Reading their architecture was worth more than any patch tonight, and the marginal value of more
research from me is now low. The two things still worth my time, if you want them:

1. A **GS replay harness spec** for G2 — capture, replay, hash GS local memory and every present.
   Same idea as theirs, sized for our CPU rasteriser and our existing `gs_cpu_backend`.
2. A **disc-mount audit** — whether `cdrom0:\GAMEDATA` fails as a path, a prefix, or a mount, by
   reading our own `ps2_rom_device.cpp` and `ps2_vfs.cpp` and reporting the shape before you change
   anything.

Say which and I will do only that.
