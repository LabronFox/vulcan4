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

## R30b — PCDV/Pcdv bound, the whack-a-mole is the PDI CDVD streamer

After the SifLoadModule fix, the remaining walls resolve in sequence, each revealing the next:

1. `bind sid=0x50434456` ("PCDV") → no server. Stub `ps2xIOP/src/modules/pdicdvd.cpp`
   (patch `ps2recomp-linux-w277-pdicdvd-service.patch`) claims it, gated on pdicdvd/libpdi aliases.
2. `bind sid=0x50636476` ("Pcdv") → no server. Same stub claims it (two sids).
3. `[PCDV:stub] rpc=0x0 send=0x86ccc0 sendSize=0x40 recv=0` — the first real DVD call, send-only.
4. missing-target `0x578288` (JALR 0x578118) — an 8-byte `jr ra;nop` Ghidra missed between
   FUN_00578230 (ends 0x578284) and the 0x578290 routine. Closed at the tool input:
   `w276-engine_r30.toml` adds `"hook_578288@0x00578288"` (entry_points 884→885), emit 19404/19404.
5. missing-target `0x5477c8` (JALR 0x578714) — THE CURRENT WALL, same class. Not yet closed.

`boot_w277r30b.log`: functions_entered=13108 halt=missing_function pc=0x005477c8, and the trace is
now DEEP in the PDI CDVD manager (0x575098, 0x578968, 0x578cf0, 0x575e60, 0x5769f0, a new tid6 at
0x5786f0). The game has finished loading drivers and is executing the DVD streamer's own code — the
streamer is no longer "stubbed at the bind", it is running and missing Ghidra-missed call targets.

## The honest verdict

The LOADFILE wall is ANSWERED (16 drivers load, all service binds resolve), and two Ghidra-missed
functions are closed at the tool input. The picture has NOT advanced to a human-verified GT4 screen.
What remains is a mechanical grind: each `missing-target` (0x5477c8 next) is an 8-byte or small
`jr ra`/leaf Ghidra missed, closed by one TOML entry + emit + rebuild. The DVD streamer RPCs
themselves are not yet decoded — that is the real protocol work after the grind.

## R30c/d/e — the missing-function grind is DONE, the wall is now the DVD streamer

Each cycle closed one Ghidra-missed leaf (all real function starts, oracle-disasm'd), then revealed
the next. Closed in `w276-engine_r30.toml` entry_points (884→889):

| addr | shape | reached by |
|---|---|---|
| 0x58dfe0 | func (lui v0,0x88) | JALR 0x5b1394 |
| 0x578288 | `jr ra;nop` | JALR 0x578118 |
| 0x5477c8 | func (addiu sp,-0x10) | JALR 0x578714 |
| 0x566df8 | func (addiu sp,-0x20) | JALR 0x565a58 |
| 0x55ab90 | `jr ra;nop` | JALR 0x55aca8 |

`boot_w277r30e.log`: **missing_functions=0**, halt=**stuck_in_syscall** (WaitSema 0x44, pc 0x5aedb0),
functions_entered=17406, gs_packets=45, gs_frame_reg_writes=29, frames=1029, **9 threads**.

The game now boots every driver, binds every service, and issues exactly ONE DVD RPC —
`[PCDV:stub] rpc=0x0 send=0x86ccc0 sendSize=0x40 recv=0` — then blocks in a WaitSema loop
(`ra=0x5aedc0`, 10177 calls). The stub answers nothing, so the DVD streamer's completion semaphores
(sema#95918/95919/95920, three parked threads) never fire. The caption: the missing-function grind
is finished; what remains is the mission's step-2 in full — implement the PCDV rpc=0 init so the
streamer signals completion. The picture is unchanged (w277r30e-capture.png `a1a674fa…`, 18924 B —
the disclaimer, not blank, not a new screen).

---

## RESUME FROM HERE (captain's stop, 2026-10-08 23:38)

**Verified state, not inferred:**
- `boot_w277r30e.log`: `missing_functions=0`, `halt=stuck_in_syscall`, `functions_entered=17406`,
  `frames_presented=1029`, `gs_packets=45`, `gs_frame_reg_writes=29`, 9 threads, `bios_files=0`.
- Picture: `w277r30e-capture.png` = sha256 `a1a674fa…`, 18924 B — STILL the ©2005 disclaimer
  (not blank, not a new screen).

**What is done (all committed, all patched):**
- LOADFILE `rpc=0` (SifLoadModule) loads 16 IRX drivers — patch `ps2recomp-linux-w277-loadfile-sifloadmodule.patch`.
- 5 Ghidra-missed leaves closed at the tool input in `/mnt/ssd/gt4/work/w276-engine_r30.toml`
  entry_points (884→889): 0x58dfe0, 0x578288, 0x5477c8, 0x566df8, 0x55ab90.
- PCDV/Pcdv service stub (sids 0x50434456 + 0x50636476) — patch `ps2recomp-linux-w277-pdicdvd-service.patch`.

**The wall, named:** the DVD streamer. The game issues ONE RPC — `[PCDV:stub] rpc=0x0
send=0x86ccc0 sendSize=0x40 recv=0` — then blocks in `WaitSema` (pc 0x5aedb0, ra 0x5aedc0) while
three threads wait on sema#95918/95919/95920. The stub answers nothing, so the streamer's
completion semaphores never fire.

**Exact next step (one action):** dump the 0x40-byte init packet (the stub now logs
`[PCDV:send] ... words=16: …` — read it from any fresh boot log), then oracle-trace what the real
PDICDVD `rpc=0` init does — specifically which of sema#95918/95919/95920 it signals, and with what
value — using the live PCSX2 (`pcsx2_connect`, paused at 0x005c1004, GT4 SCUS-97328 v2.00). Then
implement that signal in `ps2xIOP/src/modules/pdicdvd.cpp` `handleRpc`, rebuild
(`VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_r30 bash tools/harness/build_harness.sh`),
boot + capture (`VULCAN4_SIFRPC=1 VULCAN4_DISPLAY=:107 bash tools/harness/run_capture.sh …`), and
look at the picture. Blank or disclaimer = the streamer still isn't signalling; iterate.

**Build/run loop (probe ON is still required — the raw RPC delivery is gated behind
`VULCAN4_SIFRPC=1`; flipping that default is an open question):**
```
cd /mnt/ssd/gt4/work && PS2RECOMP_ENTRY_ADDR_CSV=/mnt/ssd/vulcan4-build/engine-symbols.csv \
  PS2RECOMP_TABLE_SYMBOL=g_ps2EngineFunctionTable TMPDIR=/mnt/ssd/tmp \
  nice -n 10 ionice -c3 /mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp w276-engine_r30.toml
VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_r30 bash tools/harness/build_engine.sh
VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_r30 bash tools/harness/build_harness.sh
VULCAN4_SIFRPC=1 VULCAN4_DISPLAY=:107 bash tools/harness/run_capture.sh w277 2000000 20 4
```

---

## R30g/h/i — the PCDV-init wall answered; boot now RUNS to the deadline (captain re-open)

### The wall, restated (r30f)

`halt=stuck_in_syscall`, ONE PCDV rpc — `rpc=0x0 send=0x86ccc0 sendSize=0x40 recv=0` — then a
WaitSema loop that never fires. The stub answered the rpc but never **signalled completion**.

### Root cause (measured in code, not inferred)

The PCDV `rpc=0` is a **NOWAIT** call (`mode=0x1`) with an endFunction callback (`endFn=0x5780f8`,
`endParam=0x86cc80`). `SifCallRpc` (RPC.cpp:784-792) signals the client's completion semaphore ONLY
when the HLE result sets `signalCompletion` / `signalNowaitCompletion`. `pdicdvd.cpp` set
`handled=true` but left both false — so `signalRpcCompletionSema()` never ran and the semaphore the
guest parks on never fired. Fix: set `result.signalCompletion = true` for rpc=0 (one flag, one signal).

### What it unlocked, in order (r30g → r30h → r30i)

The boot is a driver-load + service-bind grind. Each "no server" bind is a wall. Three HLE services
closed them (all in `ps2xIOP/src/modules/`, patched as `ps2recomp-linux-w277-r30-pdi-peripherals.patch`):

| sid | name | module | wall in | fixed by |
|---|---|---|---|---|
| 0x50434456 | PCDV | PDICDVD | r30e/f | `pdicdvd.cpp` signalCompletion |
| 0x50555354 | PUST | USTORAGE | r30g | `ustorage.cpp` |
| 0x53545250 | STRP | PDISTR | r30h (retried 4x) | `pdiperiph.cpp` |
| 0x534d5550 | SMUP | PDISPU2 | r30h | `pdiperiph.cpp` |
| 0x54485550/0x45535550/0x424b5550 | THUP/ESUP/BKUP | PDIUSB | r30h | `pdiperiph.cpp` |

### Numbers (raw, same engine r30, 20 s budget)

| run | halt | functions_entered | gs_packets | modules |
|---|---|---|---|---|
| r30f | stuck_in_syscall | 16640 | 39 | 16 |
| r30g | stuck_in_syscall | 18329 | 53 | 17 (USTORAGE) |
| r30h | **wallclock_deadline** | 21347 | 53 | 23 |
| r30i | wallclock_deadline | 21298 | 15 | 23 |

**halt flipped from `stuck_in_syscall` to `wallclock_deadline`** — the game is no longer parked in a
syscall; it loads all 23 IOP drivers, binds every PDI service, and runs to the 20 s deadline.

### Honest verdict (not a false win)

The picture is **STILL the ©2005 disclaimer** (`w277r30i-capture.png`, 18924 B). The boot is RUNNING
but has not reached the movie/menu. Two remaining "no server" sids are bound but not walls (game is at
deadline, not stuck on them): `0x5042474d` ("PBGM") and `0x046d046d` (numeric). The PCDV streamer has
still issued only `rpc=0` (init) — **no DVD read/seek/stream rpc yet**, so the game has not begun
loading the Adhoc scripts/movie. That is the next wall, and it is where OpenAdhoc names the files.

### Next step (one action)

Watch for the first PCDV `rpc != 0` (a DVD read). It has not happened; the remaining two sids
(0x5042474d = likely LGDEV/POWOFF service, 0x046d046d = numeric) should be claimed first with log-only
stubs so the boot finishes driver init. Then the PCDV read rpcs reveal the GT4.VOL streaming protocol,
which is decoded against the oracle + OpenAdhoc's boot/scripts file list.
