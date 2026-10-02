# GOAL: W106 — DO NOT ENTER THE EXCEPTION VECTOR. DISPATCH INSTEAD.

## THE RESEARCH, ALREADY DONE. DO NOT RE-DERIVE IT.

Upstream PS2Recomp is BIOS-free BY DESIGN. Its own ps2xIOP/README.md says:
"IOP kernel providing imports without a PS2 BIOS." And upstream's code generator
test (ps2xTest/src/code_generator_tests.cpp) asserts exactly this shape:

    const size_t continuation = generated.find("ctx->pc = 0x9004u;");
    const size_t dispatch    = generated.find("runtime->handleSyscall(rdram, ctx, 0x44u);");
    t.IsTrue(continuation < dispatch, ...);

i.e. **a SYSCALL publishes the next guest PC and dispatches to a C handler. It never
vectors to 0x80000080.** Upstream's own docs state the same: for syscalls "the
primary action is dispatching to the handler rather than jumping to an exception
vector." The vector exists in the runtime (selectExceptionVector /
raiseCop0Exception, ps2_runtime.cpp ~line 189 and ~280) but is used for REAL
hardware faults -- TLB refill, address error, reserved instruction, overflow --
never for interrupts and never for syscalls.

**So commit 8963df9, which made dispatchIrq raise the COP0 interrupt, took the
interrupt on the wrong path.** It is what turned a 60-second boot into a 71-function
corpse. Measured, from the captain's own run:

    VULCAN4 WILDPC dead=0x80000080 last_good=0x0100f800 ra=0x01010a70 sp=0x01ffc760
    functions_entered=71 distinct_pcs=139 serviced_invocations=22 interrupts_raised=4
    interrupts_delivered=8 halt=pc_outside_generated_table pc=0x80000080

224 vblanks were being processed and delivered correctly BEFORE that change, through
the invocation path (serviced_invocations=22 proves that path works). The COP0
raise is an ADDITIONAL edge on top of a path that already worked. Nothing in the
guest ELF defines code at 0x80000080 -- it is a KSEG1 kernel address -- so entering
it can only ever halt the guest.

## THE FIX, IN ONE SENTENCE

**Revert the COP0 raise added by 8963df9 and deliver the interrupt through the
existing, already-working invocation path only.**

Do not delete the mask check in dispatchIrq. Do not touch processDueDeadlines --
an attempt to change its host-clock gate was tried, made the guest 3x SLOWER
(287M -> 89M cycles/s) and fixed nothing; that reasoning is recorded in the W104
comment in that function and the change was reverted. Do not write a handler at
0x80000080 and do not introduce BIOS files: bios_policy=none is the project's
standing decision and the evidence above says it is the right one.

If 8963df9 did something ELSE besides the raise that is genuinely needed, keep that
part and remove only the raise. Read the commit before reverting it wholesale.

## ALREADY IN YOUR HANDS, DO NOT RE-ADD

- VULCAN4 EVDUE type=N deadline_cy= now_cy= guest_short= host_short=
- VULCAN4 WILDPC dead= last_good= ra= sp= v0= s0=
- vblanks_processed= in the BOOT REPORT, beside interrupts_raised/delivered
- FRAMES= on PROGRESS is a real atomic read, not a count of printed lines

## SUCCESS, EXACTLY

The captain runs the game by double-clicking an icon on his desktop. Success is:
the window stays alive past 71 functions, and the numbers in BOOT REPORT move the
right way -- interrupts_raised > 0, interrupts_delivered == interrupts_raised, and
functions_entered back in the hundreds of thousands rather than 71.

If the guest then dies at a DIFFERENT address, that is progress and it is the next
wall: report it with the WILDPC line. A measured new wall beats a guessed fix.

Report STUCK / TRIED / BLOCKED BY / NEED the first time you are actually stuck.

## RULES

No fake numbers. W92's "3.6% of PS2" and "28x slower" are BOTH withdrawn; measured
is 0.93x PS2 (EE cycles per wall second vs kEeClockHz=294,912,000). Never divide
function ENTRIES by a clock rate. Do not touch docs/ or the harness window title.
Commit as Or Golan <or024662@gmail.com>, no push. -j4, nice -n 10, ionice -c3 -- a
live Minecraft server shares this box. Keep the suite at 493/493.