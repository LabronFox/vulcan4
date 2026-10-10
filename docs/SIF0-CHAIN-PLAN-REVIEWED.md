# SIF0 CHAIN ROUTING — IMPLEMENTATION PLAN (VULCAN 4)

W279, 2026-10-10. **PLAN ONLY — not applied, not built, not committed.**

Goal: make the EE-side destination of a SIF0 (IOP→EE) transfer come from the DMA chain
tag's **word 1** (`MADR = tag[1]`) exactly as PCSX2 does, instead of from any field of the
IOP's 0x40-byte packet (whose `dest` is 0 on every GT4 transfer).

Every PCSX2 claim below is a quote from the file and line named beside it. Every VULCAN 4
claim is a quote from our tree at the line named beside it.

---

## (a) WHERE our runtime short-circuits the SIF0 transfer

There is no SIF0 chain walk anywhere on the EE side. The reply is hand-built and written
straight into a guest buffer, then the guest's completion handler is called directly.

**A1 — the syscall that should build the chain is HLE'd, whole.**
`tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp:1143`

```cpp
    void sceSifSetDma(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dmatAddr = getRegU32(ctx, 4);
        const uint32_t count = getRegU32(ctx, 5);
```

It reads the guest's **descriptor list** (a flat `{src,dest,size,attr}` array,
`Ps2SifDmaTransfer` at `SIF.cpp:64-71`) and does the EE→IOP copy itself — no tag, no QWC,
no channel. `SIF.cpp:1273-1290`:

```cpp
        if (ok)
        {
            for (uint32_t i = 0; i < pendingCount; ++i)
            {
                const Ps2SifDmaTransfer &xfer = pending[i];
                ...
                if (!readEeRange(rdram, xfer.src, payload.data(), sizeBytes) ||
                    !runtime->writeIopMemory(xfer.dest, payload.data(), payload.size()))
```

**A2 — the SIF0 reply is written by direct `memcpy`, not by a chain.**
`SIF.cpp:1000-1012` (inside `rawRpcDeliverReply`, defined at `SIF.cpp:863`):

```cpp
            uint8_t *dst = getMemPtr(rdram, g_rawRpcRecvBuffer);
            if (!dst)
            {
                ...
                                << " is not mapped RDRAM, so the SIF RPC reply cannot be delivered."
                ...
                return false;
            }
            std::memcpy(dst, rend, sizeof(rend));
```

**A3 — the destination is an out-of-band guest buffer, not a tag.**
`g_rawRpcRecvBuffer` is captured by parsing the guest's `ca_pkt` payload, not by any DMA.
`SIF.cpp:1064-1068`:

```cpp
                    // struct ca_pkt.buf at +0x10: the guest's own SIF0 receive buffer.
                    const uint32_t buf = rawRpcReadWord(packet, 0x10u);
                    if (buf != 0u)
                    {
                        g_rawRpcRecvBuffer = buf;
                        g_rawRpcRecvBufferKnown = true;
```

**A4 — the completion is invoked directly, again with no tag.**
`SIF.cpp:1014-1022`:

```cpp
            GuestInvocation invocation{};
            invocation.kind = GuestInvocationKind::Interrupt;
            invocation.tag = commandId;
            invocation.context = runtime->cpu();
            invocation.context.pc = 0x005B1328u;
            SET_GPR_U32(&invocation.context, 4, g_rawRpcRecvBuffer);
```

**A5 — the code already knows the packet header cannot supply the address.**
`SIF.cpp:1124-1125`:

```cpp
                // The header's `dest`, by contrast, is 0 on every GT4 transfer, so it can never
                // supply the address.
```

and it even sets that field to NULL when it builds a reply (`SIF.cpp:984`:
`const uint32_t header = kRawRpcPacketSize; // psize = 0x40, dsize = 0, dest = NULL`).

**A6 — the EE-side DMA-register path cannot help, because it doesn't model SIF at all.**
`ps2xRuntime/src/lib/ps2_memory.cpp:1502-1526`: a channel kick with `STR=1` is only serviced
for the three GS/VIF bases —

```cpp
            const bool w275EnterGs =
                (channelBase == 0x1000A000u || channelBase == 0x10009000u || channelBase == 0x10008000u) &&
                (m_gsVRAM || channelBase == 0x10008000u);
```

SIF0 (`0x1000C000`, `kDmaChannelBases[5]`) and SIF1 (`0x1000C400`) are in the
`kDmaChannelBases` list (`Helpers/Support.h:1233-1235`) but have no transfer branch — so a
guest-programmed SIF CHCR would be silently swallowed, and no `VULCAN 4 LIMITATION:` is
emitted. That is a law-2 hole worth fixing alongside this plan.

### The PCSX2 contract we must reproduce (quoted)

`/mnt/ssd/tools/pcsx2-src/pcsx2/Sif0.cpp:81-88` — the tag is what sets the destination:

```cpp
	alignas(16) static u32 tag[4];
	tDMA_TAG& ptag(*(tDMA_TAG*)tag);

	sif0.fifo.read((u32*)&tag[0], 4); // Tag
	SIF_LOG("SIF0 EE read tag: %x %x %x %x", tag[0], tag[1], tag[2], tag[3]);

	sif0ch.unsafeTransfer(&ptag);
	sif0ch.madr = tag[1];
```

`Sif0.cpp:26-47` — and the FIFO then drains into EE RAM at that MADR:

```cpp
	const int readSize = std::min((s32)sif0ch.qwc, sif0.fifo.size >> 2);
	...
	ptag = sif0ch.getAddr(sif0ch.madr, DMAC_SIF0, true);
	...
	sif0.fifo.read((u32*)ptag, readSize << 2);
	...
	sif0ch.madr += readSize << 4;
	sif0ch.qwc -= readSize;
```

Tag word layout (`pcsx2/Dmac.h:72-84`):

```cpp
union tDMA_TAG {
	struct {
		u32 QWC : 16;
		u32 _reserved2 : 10;
		u32 PCE : 2;
		u32 ID : 3;
		u32 IRQ : 1;
	};
	struct {
		u32 ADDR : 31;
		u32 SPR : 1;
	};
```

`TAG_END = 7` (`Dmac.h:29`). So **tag[0] = QWC(low 16) | ID(28..30) | IRQ(31)**, **tag[1] = MADR**.

---

## (b) WHAT the minimum change is

### Is the chain programmed by the guest's kernel via DMA registers, or through the HLE?

**Answer: the chain does not exist in our runtime, and cannot — because syscall 119 is
HLE'd whole (A1), so the kernel code that would program `CHCR/TADR/MADR/QWC` never runs.**
Two independent sources agree:

- Our own tree: `SIF.cpp:1143` *is* the entire handler for syscall 119; there is no code
  path from it to any IO register (`runtime->memory().writeIORegister`) — it only calls
  `readEeRange` / `writeIopMemory` / `rawRpcDeliverReply`.
- PCSX2's own EE-kernel HLE *also* only logs this syscall and never performs a transfer —
  `pcsx2/R5900OpcodeImpl.cpp:1102-1125`:

```cpp
		case Syscall::sceSifSetDma:
			// The only thing this code is used for is the one log message, so don't execute it if we aren't logging bios messages.
			if (TraceActive(EE.Bios))
			{
				...
					BIOS_LOG("bios_%s: n_transfer=%d, size=%x, attr=%x, dest=%x, src=%x",
							R5900::bios[cpuRegs.GPR.n.v1.UC[0]], n_transfer,
							dmat->size, dmat->attr,
							dmat->dest, dmat->src);
				}
			}
			break;
```

(PCSX2 is an emulator: the *real* transfer is carried out by GT4's own kernel running the
SIF channels. We cannot do that — we replaced the kernel with the HLE.)

**Consequence.** To make the *chain* authoritative we must have the runtime *emulate the
kernel's contract*: form the SIF0 chain tag and route by it. The tag itself is not written
to EE RAM on hardware — `ProcessEETag` consumes it from the FIFO
(`Sif0.cpp:84-88`) and only the *data* lands at `MADR`. So the minimum change is:

> **Route the SIF0 reply by `tag[1]`, where `tag[1]` is the guest's own SIF0 receive
> address read from the modelled SIF0 `D_MADR` register (`0x1000C010`), instead of the
> hard-wired `g_rawRpcRecvBuffer`.** Both the write and the `_request_end` a0 use the same
> `dest`. Log `tag[0]`, `tag[1]`, `qwc` as values.

### How 0x00874300 and its tag at 0x00874304 are supposed to be filled

- `0x00874300` is the EE-side receive **buffer** (the game's SIF0 receive MADR — the docs
  record `0x00654A84` holds the uncached alias `0x20874300` on both hardware and our
  runtime, `docs/W279-TODAY-SIF0-ROUTING.md:14-15`).
- The **chain tag** (the EE tag the EE reads first) carries `tag[1] = 0x00874300`, so
  `sif0ch.madr` becomes `0x00874300` (`Sif0.cpp:88`) and `WriteFifoToEE` drains the packet
  data there (`Sif0.cpp:33-40`). **The tag QW itself is never written to EE RAM.**
- `0x00874304` is a word **inside the delivered data** (MADR + 4): the command tag
  `0x010B2400 / 0x010B0000` that the dispatcher `sub_00560778` polls
  (`ps2_runtime_macros.h:1056-1057`). It is filled when the packet data — whose command
  word sits at offset +4 — is drained into `0x00874300`.

So the single fix is: **the data must land at `0x00874300`**. Today `g_rawRpcRecvBuffer`
resolves to `0x00886740` (`SIF.cpp:807` comment: "measured 0x00886740"), not the
dispatcher's `0x00874300` — which is exactly why `0x00874304` never gets its tag. Making
`tag[1]` authoritative (sourced from the guest's SIF0 `D_MADR`) is what closes that gap.

---

## (c) THE EXACT NEW CODE

File: `tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp`.
Two edits. Opt-in: **OFF** unless `VULCAN4_SIF0_CHAIN` is a non-empty value that is not
`0/off/false/no` (same idiom as `rawRpcProbeEnabled`, `SIF.cpp:816-833`).

**Edit 1 — new helpers, inserted inside the anonymous namespace immediately after
`rawRpcProbeEnabled()` (which ends at `SIF.cpp:833`, just before `rawRpcReadWord` at line 835).**

```cpp
        // W279. SIF0 (IOP->EE) CHAIN ROUTING. On hardware the EE-side destination of a SIF0
        // transfer is word 1 of the DMA chain tag read out of the SIF0 FIFO first, NOT any field
        // of the IOP's 0x40-byte packet:
        //   pcsx2/Sif0.cpp:88   sif0ch.madr = tag[1];
        //   pcsx2/Sif0.cpp:33   ptag = sif0ch.getAddr(sif0ch.madr, DMAC_SIF0, true);  ... fifo.read(...)
        // Our runtime answers an RPC by writing the reply straight to a guest buffer with no tag,
        // no QWC and no channel (see rawRpcDeliverReply below), so the chain the dispatcher polls
        // for never forms. Syscall 119 is HLE'd whole (sceSifSetDma, below), so the kernel that
        // would program CHCR/TADR/MADR/QWC never runs -- the chain has to be emulated here.
        // OFF unless VULCAN4_SIF0_CHAIN is set to a value that is not 0/off/false/no.
        bool sif0ChainEnabled()
        {
            static const bool enabled = []() -> bool {
                const char *value = std::getenv("VULCAN4_SIF0_CHAIN");
                if (value == nullptr)
                    return false;
                std::string text(value);
                for (char &c : text)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return !(text.empty() || text == "0" || text == "off" || text == "false" ||
                         text == "no");
            }();
            return enabled;
        }

        // SIF0 chain SADR/MADR: the EE's SIF0 (DMA channel 5) MADR register. ps2tek/PS2 layout
        // puts Dn_MADR at channelBase+0x10; channel base for SIF0 is 0x1000C000
        // (Helpers/Support.h:1233-1235 kDmaChannelBases[5]); the same +0x10 offset is used by the
        // runtime's own DMA stub (Helpers/Support.h:1404). In chain mode hardware overwrites this
        // with tag[1] (Sif0.cpp:88); the guest seeds it with the uncached alias 0x20874300
        // (docs/W279-TODAY-SIF0-ROUTING.md:14), so mask to the physical window.
        constexpr uint32_t kSif0MadrRegister = 0x1000C010u;

        // The chain-tag word 1 (MADR): tag[1] = the EE-side destination, exactly as
        // pcsx2/Sif0.cpp:88 assigns it. Read from the guest's own SIF0 MADR register so the guest
        // stays authoritative; fall back to the out-of-band receive buffer only if the register
        // holds nothing usable, and say so out loud.
        uint32_t sif0ChainDest(PS2Runtime *runtime, uint32_t fallback)
        {
            uint32_t madr = runtime ? runtime->memory().readIORegister(kSif0MadrRegister) : 0u;
            const uint32_t phys = madr & 0x1FFFFFFFu; // 0x20874300 -> 0x00874300
            if (phys == 0u || phys >= PS2_RAM_SIZE)
            {
                std::cerr << "VULCAN 4 LIMITATION: SIF0 chain tag has no usable MADR (SIF0 D_MADR=0x"
                          << std::hex << madr << std::dec
                          << "), so tag[1] cannot be read from the guest; falling back to the"
                             " published receive buffer 0x" << std::hex << fallback << std::dec
                          << std::endl;
                return fallback;
            }
            return phys;
        }

        // The chain-tag word 0: QWC(low 16) | ID(28..30) | IRQ(31) -- pcsx2/Dmac.h:72-84 and
        // pcsx2/Dmac.h:29 (TAG_END = 7). PCSX2's EE tag for SIF0 ends the transfer with TAG_END
        // (Sif0.cpp:108-110 sets sif0.ee.end on TAG_END), and the SIF0 'REFE'/CNTS STS update is
        // Sif0.cpp:52. We log this word so the routing decision is readable as a value.
        uint32_t sif0ChainTagWord0(uint32_t dataBytes)
        {
            constexpr uint32_t kTagEnd = 7u;
            const uint32_t qwc = dataBytes >> 4; // whole quadwords
            return (qwc & 0xFFFFu) | (kTagEnd << 28u);
        }
```

**Edit 2 — in `rawRpcDeliverReply`, replace the destination block (`SIF.cpp:1000-1022`)
so the write and the `_request_end` argument both use the chain tag's MADR, and log the
routing values. The `rend` bytes themselves do not change.**

Replace `SIF.cpp:1000-1012` (the `uint8_t *dst = getMemPtr(...)` block) and `SIF.cpp:1019`
(the `SET_GPR_U32(... 4, g_rawRpcRecvBuffer)` line) with:

```cpp
            // W279. Route the SIF0 reply by the chain tag's word 1 (MADR = tag[1]). Unset =
            // g_rawRpcRecvBuffer, byte for byte today's behaviour.
            const uint32_t dest = sif0ChainEnabled()
                                      ? sif0ChainDest(runtime, g_rawRpcRecvBuffer)
                                      : g_rawRpcRecvBuffer;
            if (sif0ChainEnabled())
            {
                const uint32_t tag0 = sif0ChainTagWord0(kRawRpcPacketSize);
                auto flags = std::cerr.flags();
                std::cerr << "[SIF0] tag[0]=0x" << std::hex << tag0
                          << " tag[1]=0x" << dest
                          << " qwc=" << std::dec << (kRawRpcPacketSize >> 4)
                          << " -> EE write 0x" << std::hex << dest << std::dec << std::endl;
                std::cerr.flags(flags);
            }

            uint8_t *dst = getMemPtr(rdram, dest);
            if (!dst)
            {
                if (g_rawRpcLimitationCount++ < 8u)
                {
                    std::cerr << "VULCAN 4 LIMITATION: SIF0 receive buffer 0x" << std::hex
                              << dest << std::dec
                              << " is not mapped RDRAM, so the SIF RPC reply cannot be delivered."
                              << std::endl;
                }
                return false;
            }
            std::memcpy(dst, rend, sizeof(rend));
```

and change `SET_GPR_U32(&invocation.context, 4, g_rawRpcRecvBuffer);` to
`SET_GPR_U32(&invocation.context, 4, dest);`.

Unified diff (context from the current file):

```diff
--- a/tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp
+++ b/tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp
@@ (after rawRpcProbeEnabled(), before rawRpcReadWord)
+        // W279. SIF0 (IOP->EE) CHAIN ROUTING ... [helpers sif0ChainEnabled /
+        // kSif0MadrRegister / sif0ChainDest / sif0ChainTagWord0 -- full text above]
@@ rawRpcDeliverReply
-            uint8_t *dst = getMemPtr(rdram, g_rawRpcRecvBuffer);
+            const uint32_t dest = sif0ChainEnabled()
+                                      ? sif0ChainDest(runtime, g_rawRpcRecvBuffer)
+                                      : g_rawRpcRecvBuffer;
+            if (sif0ChainEnabled())
+            {
+                const uint32_t tag0 = sif0ChainTagWord0(kRawRpcPacketSize);
+                std::cerr << "[SIF0] tag[0]=0x" << std::hex << tag0
+                          << " tag[1]=0x" << dest
+                          << " qwc=" << std::dec << (kRawRpcPacketSize >> 4)
+                          << " -> EE write 0x" << std::hex << dest << std::dec << std::endl;
+            }
+            uint8_t *dst = getMemPtr(rdram, dest);
             if (!dst)
             {
                 if (g_rawRpcLimitationCount++ < 8u)
                 {
                     std::cerr << "VULCAN 4 LIMITATION: SIF0 receive buffer 0x" << std::hex
-                              << g_rawRpcRecvBuffer << std::dec
+                              << dest << std::dec
                               << " is not mapped RDRAM, so the SIF RPC reply cannot be delivered."
                               << std::endl;
                 }
@@
             invocation.context.pc = 0x005B1328u;
-            SET_GPR_U32(&invocation.context, 4, g_rawRpcRecvBuffer);
+            SET_GPR_U32(&invocation.context, 4, dest);
```

`runtime->memory()` and `readIORegister` are already used from this layer
(`Helpers/Support.h:1389`, `1404`, `1507`), so no new include is needed.

**Optional follow-on (not part of the minimum):** in `ps2_memory.cpp`'s channel-kick path
(`1472-1526`), add a `VULCAN 4 LIMITATION:` line when a SIF0/SIF1 CHCR kick falls through,
so law 2 holds for the path we are deliberately not modelling.

---

## (d) HOW TO VERIFY

Rebuild (do **not** run this in the plan phase — it is the reviewer's step):

```bash
cd /home/or/vulcan4
VULCAN4_ENGINE_DIR=/mnt/ssd/vulcan4-build/recomp_engine_r30 bash tools/harness/build_harness.sh
```

Boot with the SIF0 chain routing ON and a capture of the streaming window, plus a halt
dump of the dispatcher buffer (`VULCAN4_RDRAM_DUMP=<hexaddr>:<hexlen>[:path]`, implemented
at `tools/harness/vulcan4_harness.cpp:3089`):

```bash
cd /mnt/ssd/vulcan4-build/run
VULCAN4_SIF0_CHAIN=1 \
VULCAN4_RDRAM_DUMP=874300:40:/mnt/ssd/vulcan4-build/run/sif0_window.bin \
  bash /home/or/vulcan4/tools/harness/run_capture.sh w279_sif0chain 2000000 12 3
```

(For the SIF0 RPC path to be exercised at all, the run also needs `VULCAN4_SIFRPC=1`,
which turns on `rawRpcDeliverBatch` at `SIF.cpp:1333-1335`; the legacy path at
`SIF.cpp:1337` writes to a fixed 0x00081F20 header instead. Run both ways and compare.)

The ExecPS2-hand-off dump variant (fires the instant the bootstrap hands control over,
`vulcan4_harness.cpp:2843`) is for the decode lane, not this one — but it is the control
that proves the harness itself is dumping the right window:

```bash
VULCAN4_RDRAM_DUMP_AT_HANDOFF=100000:40:/mnt/ssd/vulcan4-build/run/handoff_text.bin \
  bash /home/or/vulcan4/tools/harness/run_capture.sh w279_handoff 1 3 3
```

**The single observable that says the fix worked:** a **non-zero command tag at
`0x00874304`** in the halt dump of `0x00874300..0x00874340` — i.e.

```bash
od -An -tx4 -j 4 -N 4 /mnt/ssd/vulcan4-build/run/sif0_window.bin   # expect 0x010b2400 / 0x010b0000
```

and, in the log, the routing line:

```
[SIF0] tag[0]=0x... tag[1]=0x00874300 qwc=4 -> EE write 0x00874300
```

A changed picture (≠ disclaimer) is the secondary proof; a *non-zero tag at 0x00874304*
with a still-stale picture means the routing worked and the next wall is downstream — which
is exactly the value the plan is meant to expose.

---

## RISKS / UNKNOWNS I could not resolve from the sources

1. **Provenance of `tag[1] = 0x00874300` is unproven in-tree.** The plan sources `tag[1]`
   from the SIF0 `D_MADR` register (`0x1000C010`). I could not find any line in our tree
   that proves the guest *writes* that register with `0x20874300` (only that it stores
   `0x20874300` at EE RAM `0x00654A84`, `docs/W279-TODAY-SIF0-ROUTING.md:14`). If the
   register reads 0, the helper refuses into a `VULCAN 4 LIMITATION:` line and falls back —
   so the risk is "no change + a loud line", not a wrong answer, but the fix may not fire
   on the first try.
2. **`0x00886740` vs `0x00874300`.** `g_rawRpcRecvBuffer` is measured as `0x00886740`
   (`SIF.cpp:807`) while the dispatcher polls `0x00874300`. I could not confirm from source
   that these are the *same* buffer on hardware or that the REND packet's command word lands
   at MADR+4 (I infer it from the poll address `0x00874304`). If they are different buffers,
   routing to `0x00874300` may need the REND *shape* adjusted too, beyond this minimum.
3. **`rawRpcDeliverReply` is only reached on the `VULCAN4_SIFRPC=1` path.** The default
   (legacy) path at `SIF.cpp:1337` builds a different header at `0x00081F20`; the change
   above does not touch it. Whether the fix needs to be mirrored there is unresolved.
4. **The IOP side (ps2xIOP) has no chain either.** `ps2xIOP/.../iop_rpc.cpp:42-136`
   (`sceSifSetDma`) copies to `transfer.destination` directly with no tag
   (`iop_rpc.cpp:117` logs `src/dst/size`). A chain that is authoritative on the EE side
   only is half a chain; the IOP-side tag builder (`Sif0.cpp` `ProcessIOPTag`,
   `pcsx2/Sif0.cpp:116-144`) is not modelled. I did not design that half here.
5. **Whether the guest jumps over the HLE.** If GT4's kernel contains its own
   `sceSifSetDma`/DMA-chain code (not the BIOS syscall), the HLE would not be entered and
   the whole premise shifts. The evidence (`docs/W279` line 33-38: `0x005AE064` is
   `syscall v1=0x77`) indicates the HLE *is* the path, but I could not rule out a second,
   non-syscall SIF kicker.
6. **`readIORegister(0x1000C010)` semantics (partly resolved).** Confirmed: for a non-CHCR
   channel register, `readIORegister` returns `m_ioRegisters[address]` verbatim
   (`ps2_memory.cpp:2727-2731`), and a guest store into an IO register is recorded via
   `writeIORegister(physAddr, value)` (`ps2_memory.cpp:1153-1155`). So *if* the guest writes
   `D_SIF0_MADR`, the read returns it. What I did **not** verify: that `isIoRegister`
   (`ps2_memory.cpp:1153`) covers the `0x1000C0xx` window. If it does not, the guest's write
   never lands in `m_ioRegisters`, the read is 0, and risk 1 fires. Verify with a one-line
   probe on `writeIORegister` for `address == 0x1000C010` before trusting the fix.
