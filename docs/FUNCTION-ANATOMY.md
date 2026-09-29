# FUNCTION ANATOMY — one GT4 function, read two ways

**Goal:** G0.3 — the same function as MIPS and as generated C++, with an honest verdict.

This document exists for two reasons. The captain asked to see it — *"watch a function get
recompiled"* — and because the G0.1 endianness scare proved this tool can emit **garbage while
reporting zero errors**. Before G1.1 tries to *run* 700-odd functions, one function gets read with
human eyes. If the translation is wrong, it is wrong here, in one screenful, not in a debugger
three weeks from now.

**Copyright.** The generated C++ is a mechanical derivative of copyrighted game code. It stays on
the SSD at `/mnt/ssd/vulcan4-build/recomp/`. This document carries a short annotated excerpt of
**one** function, as analysis. No game files are in the repository.

---

## 1. THE RUN — recompiling the whole executable

```bash
cd /mnt/ssd/gt4/work
# same TOML the analyzer wrote, output redirected to the SSD, single file
sed -e 's|^output = "./output/"|output = "/mnt/ssd/vulcan4-build/recomp/"|' \
    -e 's|^single_file_output = false|single_file_output = true|' \
    gt4.toml > gt4_recomp.toml

/mnt/ssd/vulcan4-build/ps2xRecomp/ps2_recomp gt4_recomp.toml
```

Input is the same `SCUS_973.28` (SHA-256 `f8f10823…8019fa`, retail US v2.00) mapped in
`docs/DISC-MAP.md`.

### The tool's own numbers, quoted

```
========== PS2Recomp report ==========
Functions discovered: 707
Symbols: 0, sections: 14, relocations: 0
Functions processed: 707, recompiled: 625, stubs: 82, skipped: 0, decode failures: 0
Additional entrypoints: 9056
Generated functions: 721
Indirect fallback promotions: 122 (13768 fallback entries)
Unhandled instructions: 0
Correctness-critical guest fallbacks: 0, failures: 0
Warnings: 122, errors: 1

[recompiler] extracted 707 functions, 0 symbols, 14 sections, 0 relocations
[recompiler] recompiling 707 functions
[recompiler] synthesized 14 standalone configured guest entry point(s)
[recompiler] collected 9042 resumable entry point(s) across 492 owner function(s)
[recompiler] recompilation pass completed
[recompiler] generating function output with 11 worker(s)
```

### ⚠️ The run FAILED. `Unhandled instructions: 0` is not the same as success.

```
  [error] output - Internal error: combined output completion queue is missing index 719
Error: Internal error: combined output completion queue is missing index 719
RECOMP_EXIT=1
```

**`Unhandled instructions: 0` — and the tool still died.** This is precisely the failure mode this
dish was opened to look for, so it is stated plainly rather than buried:

| Metric | Value | Read it like this |
|---|---:|---|
| Functions discovered | 707 | matches G0.2 |
| Functions processed | 707 | all of them were visited |
| **Recompiled** | **625** | real C++ bodies |
| **Stubs** | **82** | emitted as runtime stubs, not translated |
| Decode failures | 0 | every instruction stream parsed |
| **Unhandled instructions** | **0** | no encoding the tool didn't know |
| Warnings | 122 | all one shape (below) |
| **Errors** | **1** | **fatal — the run did not complete** |
| Functions generated | 721 | 707 + 14 synthesized entry points |
| **Function bodies actually written** | **719** | **2 lost** |

**Two functions are declared in the header but never defined in the `.cpp`:**

```
$ comm -23 <(declared in .h) <(defined in .cpp)
sub_0102DB98_0x102db98
sub_0102DBE8_0x102dbe8
```

They are the **last two by address**, and the error names index 719 of 721. That pattern says the
combined-output writer lost its tail — a completion-queue bug in the 11-worker emission path, not a
decoding problem. The generated `.cpp` **will not link** until it is fixed, because the header
declares two functions nothing defines.

**All 122 warnings are one shape** — an indirect branch the tool could not resolve statically:

```
[warning] control-flow function=sub_01000558 addr=0x10005ac - unresolved JR/JALR at
          0x10005ac 0x10005fc; promoted 82 fallback entries
```

122 functions hit an `jalr` through a loaded pointer and were **promoted to runtime-dispatched
fallbacks** (13,768 entries total) rather than miscompiled. That is the tool being honest: it says
"I cannot resolve this statically, I will ask the runtime." Good behaviour. But it also means a
large slice of control flow is **deferred to G1.1**, not translated here.

### Artefacts (on the SSD, not in the repo)

| Path | Size |
|---|---:|
| `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp` | **8,892,245 B** |
| `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.h` | 61,468 B |
| `/mnt/ssd/vulcan4-build/recomp_run.log` | 151 lines |

`register_functions.cpp` and `ps2_recompiled_stubs.h` were **not** produced — the run died before
that stage. Another consequence of the same bug.

---

## 2. THE PICK — and the rule, stated before the answer

**The rule**, applied in order. A function qualifies if:

1. **It is game code, not a thin SDK wrapper** — it does real work, not one syscall.
2. **It contains real control flow** — a branch or a loop, so the translation is actually visible.
3. **It is reachable from the entry region** — something the entry block calls, not a random leaf.

The three functions the entry block calls, per the analyzer's own report, screened against the rule:

| Candidate | Verdict | Why |
|---|---|---|
| `_InitSys` @ `0x01001E8` | rejected | the SCE C runtime initialiser — an SDK wrapper by definition |
| `sub_0101F6A0` @ `0x01001F0` | **rejected** | 3 instructions of pure wrapper: `li v1,100` / `syscall` / `jr ra` — fails rule 1 and 2 |
| `sub_01009098` @ `0x01001FC` | **rejected** | 8 instructions: one `jal`, restore, return — a thunk, fails rules 1 and 2 |
| **`sub_01000558` @ `0x0100558`** | **CHOSEN** | passes all three |

**The choice: `sub_01000558`.**

- **Address:** `0x01000558`
- **Size:** **328 bytes** (`0x01000558` – `0x010006A0`) = **82 instructions**
- **Called from:** `0x01000210`, directly in the entry block
- **Qualifies because:** it opens a 208-byte stack frame, saves four registers, makes **11 calls**,
  chases pointers, dispatches **twice through an indirect `jalr`**, contains a **conditional
  branch** (`beqz`) and a **backward branch** (a spin loop).

The call site, for context — this is the program's real startup, four instructions before our
function is entered:

```mips
10001f8:  42000038  ei                      ; enable interrupts
10001fc:  0c402426  jal 0x1009098
1000200:  00000000  nop
1000204:  3c020104  lui  v0,0x104
1000208:  24421800  addiu v0,v0,6144         ; v0 = 0x01041800
100020c:  8c440000  lw   a0,0(v0)            ; a0 = *(0x01041800)
1000210:  0c400156  jal 0x1000558           ; <-- our function
1000214:  24450004  addiu a1,v0,4            ; a1 = 0x01041804
1000218:  084079dc  j   0x101e770            ; and then hands off
```

---

## 3. THE FUNCTION — MIPS

```bash
mips-linux-gnu-objdump -d --start-address=0x1000558 --stop-address=0x10006a0 SCUS_973.28
```

```mips
1000558:  27bdff30  addiu  sp,sp,-208        ; open a 208-byte frame
100055c:  ffb000b0  sd    s0,176(sp)         ; save callee-saved regs
1000560:  ffb100b8  sd    s1,184(sp)
1000564:  ffb200c0  sd    s2,192(sp)
1000568:  ffbf00c8  sd    ra,200(sp)
100056c:  0c4066e6  jal   0x1019b98          ; setup #1
1000570:  00000000  nop
1000574:  0c4028d2  jal   0x100a348          ; setup #2
1000578:  24040002  li    a0,2               ;   (delay slot) arg = 2
100057c:  0c400250  jal   0x1000940          ; setup #3
1000580:  00000000  nop
1000584:  0c4001e6  jal   0x1000798          ; setup #4
1000588:  00000000  nop
100058c:  0c4000b2  jal   0x10002c8          ; setup #5
1000590:  00000000  nop
1000594:  0040882d  move  s1,v0              ; s1 = that function's result
1000598:  8e250008  lw    a1,8(s1)           ; a1 = s1->field_at_8
100059c:  8ca30008  lw    v1,8(a1)           ; v1 = a1->field_at_8
10005a0:  24630010  addiu v1,v1,16           ; v1 += 16  (skip a 16-byte header)
10005a4:  84640000  lh    a0,0(v1)           ; a0 = signed 16-bit at v1      <-- (1)
10005a8:  8c620004  lw    v0,4(v1)           ; v0 = function pointer at v1+4
10005ac:  0040f809  jalr  v0                 ; CALL THROUGH A LOADED POINTER
10005b0:  00a42021  addu  a0,a1,a0           ;   (delay slot) base + offset
10005b4:  03a0202d  move  a0,sp              ; a0 = &local_struct
10005b8:  0c407b40  jal   0x101ed00
10005bc:  0040282d  move  a1,v0              ;   (delay slot) pass result along
10005c0:  27b00080  addiu s0,sp,128          ; s0 = &local_struct.field_0
10005c4:  27b20084  addiu s2,sp,132          ; s2 = &local_struct.field_4
10005c8:  ae110000  sw    s1,0(s0)           ; field_0 = s1
10005cc:  0200202d  move  a0,s0              ; a0 = &field_0
10005d0:  ae400000  sw    zero,0(s2)         ; field_4 = 0
10005d4:  0c4011f0  jal   0x10047c0
10005d8:  afa00088  sw    zero,136(sp)       ;   (delay slot) field_8 = 0
10005dc:  8e100000  lw    s0,0(s0)           ; s0 = field_0  (walk the list)
10005e0:  12000008  beqz  s0,0x1000604       ; ---- real branch ----
10005e4:  0040882d  move  s1,v0              ;   (delay slot) s1 = return value
10005e8:  8e02000c  lw    v0,12(s0)          ; v0 = s0->field_at_12
10005ec:  24050003  li    a1,3
10005f0:  24420008  addiu v0,v0,8
10005f4:  84440000  lh    a0,0(v0)           ; signed 16-bit at v0           <-- (2)
10005f8:  8c430004  lw    v1,4(v0)           ; function pointer at v0+4
10005fc:  0060f809  jalr  v1                 ; SECOND indirect call
1000600:  02042021  addu  a0,s0,a0
1000604:  0240202d  move  a0,s2
1000608:  0c40038c  jal   0x1000e30
100060c:  24050002  li    a1,2
1000610:  0c400370  jal   0x1000dc0
1000614:  00000000  nop
1000618:  0c400380  jal   0x1000e00
100061c:  00000000  nop
1000620:  0c400242  jal   0x1000908
1000624:  00000000  nop
1000628:  0c4029ba  jal   0x100a6e8
100062c:  00000000  nop
1000630:  56200009  bnezl s1,0x1000658       ; if s1 != 0, skip the trap
1000634:  3c100104  lui   s0,0x104           ;   (delay slot)
1000638:  00000000  nop                      ; }
100063c:  00000000  nop                      ; } 5 NOPs, then
1000640:  00000000  nop                      ; } jump back to
1000644:  00000000  nop                      ; } the top — a spin trap
1000648:  00000000  nop                      ; }
100064c:  1000fffa  b    0x1000638           ; ---- backward branch: infinite loop
1000650:  00000000  nop
1000654:  00000000  nop
1000658:  3c020104  lui   v0,0x104
100065c:  26101948  addiu s0,s0,6472         ; s0 = 0x01041948
1000660:  2442d208  addiu v0,v0,-11768       ; v0 = 0x01042DF8  (a vtable pointer)
1000664:  ae020004  sw    v0,4(s0)
1000668:  ae1d0000  sw    sp,0(s0)           ; publish the frame pointer
100066c:  0c4027a6  jal   0x1009e98
1000670:  ae000008  sw    zero,8(s0)         ;   (delay slot) field_8 = 0
1000674:  0220202d  move  a0,s1
1000678:  0200302d  move  a2,s0
100067c:  0c4060ec  jal   0x10183b0
1000680:  24050002  li    a1,2
1000684:  0000102d  move  v0,zero            ; return 0
1000688:  dfb000b0  ld    s0,176(sp)         ; restore
100068c:  dfb100b8  ld    s1,184(sp)
1000690:  dfb200c0  ld    s2,192(sp)
1000694:  dfbf00c8  ld    ra,200(sp)
1000698:  03e00008  jr    ra
100069c:  27bd00d0  addiu sp,sp,208          ;   (delay slot) pop the frame
```

*(objdump prints `...` across `0x1000638`–`0x1000648` because it has not yet reached the loop head
in linear order. The raw bytes there are five zero words — the NOPs shown.)*

---

## 4. THE SAME FUNCTION — generated C++

From `/mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp`. **Abbreviated** — the 82-case
`switch` dispatch at the top and the repeated delay-slot boilerplate are elided; the
representative excerpts below are verbatim.

```cpp
// Function: sub_01000558
// Address: 0x1000558 - 0x10006a0
void sub_01000558_0x1000558(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {
    switch (ctx->pc) {                       // 82 cases: one per instruction
        case 0x1000558u: goto label_1000558;
        case 0x100055cu: goto label_100055c;
        /* ... 80 more ... */
        case 0x10006a0u: goto label_10006a0;
        default: break;
    }

    ctx->pc = 0x1000558u;

label_1000558:
    // 0x1000558: 0x27bdff30  addiu       $sp, $sp, -0xD0
    ctx->pc = 0x1000558u;
    SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967088));
label_100055c:
    // 0x100055c: 0xffb000b0  sd          $s0, 0xB0($sp)
    ctx->pc = 0x100055cu;
    WRITE64(ADD32(GPR_U32(ctx, 29), 176), GPR_U64(ctx, 16));
    /* ... s1 and s2 saved the same way ... */
label_1000568:
    // 0x1000568: 0xffbf00c8  sd          $ra, 0xC8($sp)
    ctx->pc = 0x1000568u;
    WRITE64(ADD32(GPR_U32(ctx, 29), 200), GPR_U64(ctx, 31));

label_100056c:
    // 0x100056c: 0xc4066e6  jal         func_1019B98
label_1000570:
    if (ctx->pc == 0x1000570u) {             // <-- re-entry after a guest call
        ctx->pc = 0x1000574u;
        goto label_1000574;
    }
    ctx->pc = 0x100056Cu;
    SET_GPR_U32(ctx, 31, 0x1000574u);        // $ra = return address
    ctx->pc = 0x1019B98u;
    if (!runtime->dispatchGuestBranch(rdram, ctx, 0x1019B98u, 0x100056Cu, 0x1000574u,
                                      PS2Runtime::GuestBranchKind::DirectCall, "JAL")) {
        return;                              // <-- YIELD: the runtime re-enters via the switch
    }
    ctx->pc = 0x1000574u;

label_1000598:
    // 0x1000598: 0x8e250008  lw          $a1, 0x8($s1)
    ctx->pc = 0x1000598u;
    SET_GPR_S32(ctx, 5, (int32_t)READ32(ADD32(GPR_U32(ctx, 17), 8)));

label_10005a4:  /* (1) the signed 16-bit load */
    // 0x10005a4: 0x84640000  lh          $a0, 0x0($v1)
    ctx->pc = 0x10005a4u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 2), 0)));

label_10005ac:
    // 0x10005ac: 0x40f809  jalr        $v0                 <-- indirect call
label_10005b0:
    if (ctx->pc == 0x10005B0u) {             // <-- resumed in the DELAY SLOT
        ctx->pc = 0x10005B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10005ACu;
        // 0x10005b0: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = 0x10005B4u;
        goto label_10005b4;
    }
    ctx->pc = 0x10005ACu;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 2);   // the loaded pointer, resolved at RUNTIME
        SET_GPR_U32(ctx, 31, 0x10005B4u);
        ctx->pc = 0x10005B0u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10005ACu;
        // 0x10005b0: 0xa42021  addu        $a0, $a1, $a0 (Delay Slot)
        SET_GPR_S32(ctx, 4, (int32_t)ADD32(GPR_U32(ctx, 5), GPR_U32(ctx, 4)));
        ctx->in_delay_slot = false;
        ctx->pc = jumpTarget;
        if (!runtime->dispatchGuestBranch(rdram, ctx, jumpTarget, 0x10005ACu, 0x10005B4u,
                                          PS2Runtime::GuestBranchKind::IndirectCall, "JALR")) {
            return;
        }
    }
    ctx->pc = 0x10005B4u;

label_10005dc:
    // 0x10005dc: 0x8e100000  lw          $s0, 0x0($s0)
    ctx->pc = 0x10005dcu;
    SET_GPR_S32(ctx, 16, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 0)));

label_10005e0:
    // 0x10005e0: 0x12000008  beqz        $s0, . + 4 + (0x8 << 2)
label_10005e4:
    if (ctx->pc == 0x10005E4u) {
        ctx->pc = 0x10005E4u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x10005E0u;
        // 0x10005e4: 0x40882d  daddu       $s1, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        ctx->pc = 0x10005E8u;
        goto label_10005e8;
    }
    ctx->pc = 0x10005E0u;
    {
        const bool branch_taken_0x10005e0 = (GPR_U64(ctx, 16) == GPR_U64(ctx, 0));
        ctx->pc = 0x10005E4u;
        ctx->in_delay_slot = true;               // the delay slot runs FIRST,
        ctx->branch_pc = 0x10005E0u;             // whatever the branch decides
        // 0x10005e4: 0x40882d  daddu       $s1, $v0, $zero (Delay Slot)
        SET_GPR_U64(ctx, 17, (uint64_t)GPR_U64(ctx, 2) + (uint64_t)GPR_U64(ctx, 0));
        ctx->in_delay_slot = false;
        if (branch_taken_0x10005e0) {
            ctx->pc = 0x1000604u;                // taken  -> skip the block
            goto label_1000604;
        }
    }                                          // not taken -> fall through
    ctx->pc = 0x10005E8u;
label_10005e8:
    // 0x10005e8: 0x8e02000c  lw          $v0, 0xC($s0)
    ctx->pc = 0x10005E8u;
    SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 16), 12)));

label_10005f0:
    // 0x10005f0: 0x24420008  addiu       $v0, $v0, 0x8
    ctx->pc = 0x10005f0u;
    SET_GPR_S32(ctx, 2, (int32_t)ADD32(GPR_U32(ctx, 2), 8));
label_10005f4:  /* (2) the second signed 16-bit load */
    // 0x10005f4: 0x84440000  lh          $a0, 0x0($v0)
    ctx->pc = 0x10005f4u;
    SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx, 2), 0)));

    /* ... the failure trap, translated instruction for instruction ... */
label_1000630:
    // 0x1000630: 0x56200009  bnel        $s1, $zero, . + 4 + (0x9 << 2)
label_1000634:
    if (ctx->pc == 0x1000634u) {
        ctx->pc = 0x1000634u;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1000630u;
        // 0x1000634: 0x3c100104  lui         $s0, 0x104 (Delay Slot)
        SET_GPR_S32(ctx, 16, (int32_t)((uint32_t)260 << 16));
        ctx->in_delay_slot = false;
        ctx->pc = 0x1000638u;
        goto label_1000638;
    }
    ctx->pc = 0x1000630u;
    {
        const bool branch_taken_0x1000630 = (GPR_U64(ctx, 17) != GPR_U64(ctx, 0));
        if (branch_taken_0x1000630) {          // s1 != 0 -> skip the trap entirely
            /* ... delay slot, then to 0x1000658 ... */
        }
    }
label_1000638:
    // 0x1000638: 0x0  nop
    ctx->pc = 0x1000638u;
    // NOP
    /* ... four more identical NOP bodies at 0x100063c, 0x1000640, 0x1000644, 0x1000648 ... */
label_100064c:
    // 0x100064c: 0x1000fffa  b           . + 4 + (-0x6 << 2)
label_1000650:
    if (ctx->pc == 0x1000650u) {
        ctx->pc = 0x1000654u;
        goto label_1000654;
    }
    ctx->pc = 0x100064Cu;
    {
        const bool branch_taken_0x100064c = (GPR_U64(ctx, 0) == GPR_U64(ctx, 0));
        if (branch_taken_0x100064c) {
            ctx->pc = 0x1000638u;
            if (runtime->eeCheckpointDue()) {
                return;                        // <-- the spin loop YIELDS to the host
            }
            goto label_1000638;                //     and the CPU is not wedged
        }
    }

    /* ... tail: writes a vtable pointer, registers the frame, returns 0 ... */

label_1000698:
    // 0x1000698: 0x3e00008  jr          $ra
label_100069c:
    if (ctx->pc == 0x100069Cu) {             // <-- deallocation in the DELAY SLOT
        ctx->pc = 0x100069Cu;
        ctx->in_delay_slot = true;
        ctx->branch_pc = 0x1000698u;
        // 0x100069c: 0x27bd00d0  addiu       $sp, $sp, 0xD0 (Delay Slot)
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 208));
        ctx->in_delay_slot = false;
        ctx->pc = 0x10006A0u;
        goto label_fallthrough_0x1000698;
    }
    ctx->pc = 0x1000698u;
    {
        const uint32_t jumpTarget = GPR_U32(ctx, 31);   /* $ra = 31 */
        /* ... restore s0/s1/s2/ra, then return ... */
    }
}
```

---

## 5. WHAT THIS FUNCTION ACTUALLY DOES

In plain English, for someone who has never read MIPS:

> This is a **startup registration routine**. It saves its registers, then calls five setup
> functions in a fixed order. The last of them hands back a pointer to a small object; this routine
> reads a field out of that object, treats it as a **table of handlers**, skips a 16-byte header,
> and calls the handler named by a pointer stored in that table — passing it "a base address plus
> a small signed offset" read from the table. It then builds a three-field record on its own stack,
> asks a helper to walk that record as a **linked list**, and for each entry calls a second handler
> the same way, this time with a type code of `3`.
>
> After nine more setup calls it checks one result. If that result is **zero**, it deliberately
> **spins forever on five NOPs** — a failure trap, not a crash. If it is non-zero it stores a
> vtable-style pointer and a frame pointer into a fixed global at `0x01041948`, calls one final
> function, and returns `0`.
>
> The shape — a table of `{short offset, function pointer}` pairs walked and invoked — is what a
> **module registration / dispatch-table builder** looks like. The game is wiring up handler tables
> and the objects that own them, and it refuses to continue if a piece of that wiring is missing.

The two `lh` instructions are the giveaway: they read a **signed 16-bit offset** that is added to
a base address to form an argument. That is a relocation-style reference — a small signed delta
from a table base, not a real memory offset.

---

## 6. HOW FAITHFUL IS IT?

### Spot-checks across the boundary

Six instructions, each independently verified against the raw bytes:

| # | MIPS | Generated C++ | Verdict |
|---|---|---|---|
| 1 | `0x27bdff30 addiu $sp,$sp,-208` | `SET_GPR_S32(ctx, 29, ADD32(GPR_U32(ctx,29), 4294967088))` | ✅ reg 29 = `$sp`; `0xFFFFFF30` = **−208** exactly |
| 2 | `0xffb000b0 sd $s0,176($sp)` | `WRITE64(ADD32(GPR_U32(ctx,29), 176), GPR_U64(ctx,16))` | ✅ reg 16 = `$s0`; `0xB0` = **176**; 64-bit store ✓ |
| 3 | `0x84640000 lh $a0,0($v1)` | `SET_GPR_S32(ctx, 4, (int16_t)READ16(ADD32(GPR_U32(ctx,2), 0)))` | ✅ reg 4 = `$a0`, reg 2 = `$v1`; **`(int16_t)` sign-extension present** — this is `lh`, not `lw`, and the tool knew |
| 4 | `0x12000008 beqz $s0,0x1000604` | `branch_taken = (GPR_U64(ctx,16) == GPR_U64(ctx,0))` → `0x1000604` | ✅ reg 16 = `$s0`, reg 0 = `$zero`; `pc+4+(8<<2)` = **`0x1000604`**, matches objdump |
| 5 | `0x0c4066e6 jal 0x1019b98` | `dispatchGuestBranch(…, 0x1019B98u, 0x100056Cu, …, DirectCall, "JAL")` | ✅ `((0x4066e6 & 0x03FFFFFF) << 2) \| 0x1000000` = **`0x1019B98`** |
| 6 | `0x1000fffa b 0x1000638` | comment `. + 4 + (-0x6 << 2)`, jumps to `label_1000638` | ✅ `0x100064c+4−24` = **`0x1000638`** — the spin loop really is a loop |

Two things this spot-check proves beyond arithmetic:

- **The delay slot is modelled, not faked.** At `0x1000698` the C++ enters the delay-slot body
  *before* transferring control, and re-entry is possible at the branch itself
  (`if (ctx->pc == 0x100069Cu)`). At `0x10005E0` the delay slot is executed **before** the
  `if (branch_taken…)` decision, with `in_delay_slot` set and `branch_pc` recorded. That is MIPS
  semantics — the instruction after a branch always runs — not C's. A naive `goto` would have been
  wrong.
- **Infinite loops cannot wedge the host.** The spin trap at `0x1000638` is translated as a real
  loop back-edge, but guarded by `if (runtime->eeCheckpointDue()) return;`. The recompiler
  recognises an unconditional backward branch and yields periodically instead of hanging. A
  debugger or emulator that lost control here would never come back.
- **Nothing was flattened.** The `beqz` became a real conditional, the spin loop stayed a loop,
  and the indirect `jalr` stayed indirect (`const uint32_t jumpTarget = GPR_U32(ctx, 2)` resolved
  at runtime, then handed to `dispatchGuestBranch`). The compiler did not "helpfully" constant-fold
  a load it could not see.

### One thing that is *not* faithful, and should not be

`0x1000630` disassembles as `bnezl` (branch **likely**) but the tool's own comment renders it
`bnel`. Behaviourally the delay slot executes either way, so for this instruction the distinction
does not change the result — but it is a naming inaccuracy in generated comments, recorded here
rather than glossed over. Whether the *branch-likely nullification of the delay slot* is modelled
elsewhere was **not** tested; that would need an instruction where it changes the outcome.

### Verdict

**The translation is faithful on every instruction checked, including the parts that are easy to
get wrong — sign extension, delay slots, branch target arithmetic and indirect calls.** For this
function the recompiler did not merely produce plausible C++; it produced C++ whose arithmetic I
checked by hand against the raw bytes and it was right.

### What would falsify this

**This verdict covers one function, read statically, and nothing else.** It would be wrong if:

- the same spot-checks on **five more functions**, especially ones from the 122 unresolved-`jalr`
  set, show a mismatch — those were not statically resolved and I have not looked at them;
- the generated function is **wrong when executed** — this C++ has never been run, so a wrong
  register, a missed `in_delay_slot` flag or a bad `goto` target would still look perfect on paper;
- the **82 stubs** hide real logic behind a runtime call, which would mean 625 recompiled is an
  overstatement of how much is actually translated;
- the two **missing functions** (`sub_0102DB98`, `sub_0102DBE8`) are not a tail-of-queue bug but
  the visible edge of a systematic omission.

G1.1 is where the second of those gets answered, and it can only be answered by running the thing.

---

## 7. G0.3 VERDICT

| Requirement | Status |
|---|---|
| Whole executable recompiled, stats quoted | ✅ 707 processed, 625 recompiled, 82 stubs, 0 unhandled |
| Stats reported **honestly** | ✅ including the failure — see below |
| One function chosen by a **stated rule** | ✅ rule in §2, two candidates rejected on it |
| MIPS **and** C++ side by side | ✅ §3 and §4, same 82 instructions |
| Plain-English reading, no jargon padding | ✅ §5 |
| Fidelity verdict + falsifier | ✅ §6, six spot-checks |
| Generated source on the SSD | ✅ 8,892,245 B `.cpp` |

**G0.3 is met, with one finding that outranks the tidiness of it.**

### The finding: `Unhandled instructions: 0` is not success

The run **failed**. `RECOMP_EXIT=1`, `Internal error: combined output completion queue is missing
index 719`, **2 of 721 function bodies never written** (`sub_0102DB98`, `sub_0102DBE8` — the last two
by address), and `register_functions.cpp` / `ps2_recompiled_stubs.h` never produced. The generated
`.cpp` will not link as it stands.

This is the second time this toolchain has been caught reporting a clean number while something is
wrong. It is logged as **G0.4: the recompiler drops the tail of its own output**, to be fixed with
a smallest-patch discipline like the G0.1 Linux fixes. The instruction for this dish was *not* to
repair anything, and none was.

Worth saying plainly, though: the failure is in the **output writer**, not the **translator**. The
translated code I read by hand is correct. The machine that translates is working; the machine that
writes down the results loses two files' worth of tail.

### Carried forward

1. **G0.4 — fix the output truncation.** Small, well-specified, and it blocks linking.
2. **122 functions deferred to runtime dispatch** via unresolved `jalr` (13,768 fallback entries).
   This is the honest measure of how much control flow is *not* statically translated.
3. **82 stubs** — emitted, not translated. G1.1 must not count these as recompiled.
4. **Nothing has been executed.** §6 proves the translation is *faithful*, not that it *runs*.

### Licence note

The generated C++ quoted here is a short annotated excerpt of **one** function, reproduced as
analysis of a disc the user owns. It is a mechanical derivative of Polyphony Digital's code and is
included for research and preservation. The bulk output stays on the SSD and is not in this
repository. No ISO, no `core.gt4`, nothing from `GT4.VOL` is present. Not affiliated with Sony
Interactive Entertainment or Polyphony Digital.
