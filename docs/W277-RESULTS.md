# W277 — the LOADFILE wall answered, and the next wall named

## What was asked

The engine asks the IOP for its data via LOADFILE (sid `0x80000006`, raw SIF RPC `SIF_CMD_RPC_CALL
0x8000000A`). Our IOP did not answer. Get the packet from the PCSX2 oracle, implement the answer,
make the picture change.

## What the oracle said (step 1, measured not designed)

PCSX2 (live EE, `-debugger`, GT4 SCUS-97328 v2.00, already running pid 1239841) at the LOADFILE
client `0x889e80`. The last packet (`pkt_addr=0x20886a40`) decodes:

| field | value |
|---|---|
| cid | `0x8000000a` (SIF_CMD_RPC_CALL) |
| rpc number | `0` (SifLoadModule, ps2tek 09 L6583) |
| send size | `0x200` (512) |
| recvBuf | `0x889c80`, recvSize `8` |
| reply word 0 | `0x00000000` (success) |

The module path is **INLINE in the send buffer at offset 8**, not a pointer:
`cdrom0:\IRX\SIO2MAN.IRX;1`. Confirmed twice — the oracle string read at `0x889c88` and our own
`[LOADFILE:send]` dump (`mode=0 arg1=0 arg2="cdro" arg3="m0:\"`). So the inpacket is
`{ mode@0, arg_len@4, path@8... }`, not ps2sdk's pointer inpacket.

## What was implemented (step 2)

Two source changes, one patch `tools/patches/ps2recomp-linux-w277-loadfile-sifloadmodule.patch`:

1. `IopHost::loadModule(path, args, size)` — default-fails so test stubs stay source-compatible;
   `PS2IopHostAdapter` overrides it to `m_runtime.loadIopModule(...)`, the same loader the EE
   syscall `sceSifLoadModule` uses. (`ps2xIOP/include/ps2x/iop/iop_host.h`,
   `ps2xRuntime/src/lib/ps2_iop_host.{h,cpp}`)
2. `loadfile.cpp` rpc=0 handler — reads the inline path from send+8, loads the IRX, replies the
   moduleId (positive on success, -1 on failure).

## Result (r30 engine, probe ON, run `boot_w277r30.log`)

The guest now loads **16 IOP drivers** through SifLoadModule, in order: SIO2MAN, MCMAN, MCSERV,
SIO2D, DBCMAN, DS2U_D, LIBSD, USBD, INET, NETCNF, DEV9, INETCTL, LIBPDI, MSIFRPC, PDICDVD, SMAP.
Every one returns a positive moduleId. dbcman (`0x80001300`) binds and its check-version
(`0x80001363`) answers; cdvd (`0x80000592/93`) and `0x80000400` also bind and answer.

The `0x58dfe0` dispatch miss (a real function START Ghidra missed — FUN_0058df18 ends at 0x58dfd8,
nop, then a new routine at 0x58dfe0) is closed at the tool input: `w276-engine_r30.toml` adds
`"func_58DFE0@0x0058DFE0"` (entry_points 883→884), emit `19404/19404`, 0 errors.

```
halt=wallclock_deadline functions_entered=16210 true_guest_entries=2957742
gs_packets=41 gs_frame_reg_writes=25 frames_presented=855 missing_functions=0 bios_files=0
```

**Missing functions: 0.** The run is no longer a dispatch miss — it runs to the deadline.

## The next wall, NAMED (stop condition)

`[SIFRPC] bind a=0x86cb84 b=0x50434456` → **no server**, retried forever. `0x50434456` = the
4-byte string "VDCP" — the SIF RPC service GT4's **PDICDVD.IRX** registers. The game loads
PDICDVD.IRX (moduleId 21) and immediately binds this sid, so it needs the DVD-streaming service the
real module provides. Our IOP emulator loads the IRX as a physical blob but does not run its init,
so no server is registered and no HLE route exists.

This is the mission's step-2 "stream GT4.VOL" in its true form: **VDCP is the DVD command/streaming
layer** dbcman sits on. It is a new service, not a refusal or a missing function. It needs the
oracle (PCSX2 is alive, paused at 0x005c1004, Cycles 1815531795) to decode its RPC function table —
the same A/B the cdvd service used.

## Not claimed

The picture did NOT change to a verified GT4 screen. The gate's raw picture is
`/mnt/ssd/vulcan4-build/run/w277r30-capture.png` (sha256 `a1a674fa…`, 18924 bytes, differs from
`.disclaimer-reference.png` `caff8337…`) but a HUMAN has not confirmed it is GT4 content, not a
cleared/coloured framebuffer. Per the captain's "FALSE WIN" ruling, that is not reported as the
menu.

## Numbers that matter (raw)

- LOADFILE rpc=0xff still answers `0x30303033` (the R29 oracle word) before the module loads begin.
- `missing_functions=0` on r30 (was 1 on w277mod: `pc=0x0058dfe0`).
- Runs compared: `boot_w277mod.log` (halt=missing_function, 15 gs_packets, the pink screen the
  captain rejected) vs `boot_w277r30.log` (wallclock_deadline, 41 gs_packets, 0 missing).
