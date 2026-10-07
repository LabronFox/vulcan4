# W248 — the 0x83 override calls a handler that our engine never emitted

## 1. OUR slot bytes (same run as the args — W247 probe)

`[0x1218C] = 0x005B73C8`, `[0x120E8] = 0x005B7390` (read via both the low and KSEG0 addresses). So the
word the first scan hunts (`a2=0x005B73C8`) IS present at `0x1218C`, inside the scan window
`[0x80000000,0x80080000)`. The memory content is NOT the divergence.

## 2. The decider — the handler is not in our image

The slots hold the ENGINE's own 0x83/0x5A handlers: `0x005B73C8` and `0x005B7390`. Checking our emitted
engine image:

- `grep '5b73c8' recomp_engine_small/register_functions.cpp` → **0** — `0x5B73C8` is NOT in the
  function table, i.e. the handler the 0x83 override dispatches to **was never emitted**. It lies
  outside the 400-function reachable frontier (and is one of the class of addresses the 80
  entry-slice limitations / the frontier cap leave out).

So when `0x5B7408` issues `syscall 0x83`, our override routes the call to `0x5B73C8`, which has no
generated function → the handler does not run its real scan, and the caller gets garbage `v0` (`0`,
then `0xFFFFFFFF`, then `a0` degrades to `3` via `s2+4`). This is the divergence, and it is **not** (a)
args, **not** the table, and **not** canonicalisation — it is a **missing handler function in our
recompiled engine** (candidate: "the handler is truncated/mis-emitted / a missing-boundary slice").

## 3. Hardware

Raw-socket read (no breakpoints): PCSX2 answered `0x1218C = 0x00000000` … but at `pc=0x00568460`, NOT
the loop point, so it is inconclusive — the instance still will not reach `0x5B7408`. It is not repaired.

## 4. Fix (source) — next, bounded

Emit `0x005B73C8` (and `0x005B7390`) in the engine image: add them to the engine TOML `entry_points`
(or raise `PS2RECOMP_MAX_FUNCTIONS`), re-emit, recompile the parts, link, boot. This is a tool INPUT
change, not a generated-output edit — the frontier cap simply excluded a function the guest calls.

## State

No fix landed this dish. Halt unchanged `guest_cycle_no_progress`; capture unchanged; suite green; the
probe is OFF by default; nothing hand-edited; java/Minecraft untouched.
