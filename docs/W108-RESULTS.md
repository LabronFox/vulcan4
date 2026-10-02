# W108-RESULTS — the segfault is fixed; the picture is still black

Dish 50 (G2.9). Commit by `Or Golan <or024662@gmail.com>`. Not pushed.

---

## 1. THE CRASH — fixed, with a real backtrace

`gdb` **is** on this box (`/usr/bin/gdb`), despite `which gdb` finding nothing inside a compound
shell command earlier. That wasted a cycle; noted so nobody repeats it.

```
cd /mnt/ssd/vulcan4-build/run
DISPLAY=:0 gdb -batch -ex run -ex 'thread apply all bt' --args \
  ./vulcan4_harness /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml 2000000 30
```

```
Thread 4  received signal SIGSEGV, Segmentation fault.
#0  0x00007fffea8fdb29 in ??? () at /lib/x86_64-linux-gnu/libGLdispatch.so.0
#1  0x0000555555aefe57 in rlLoadTexture ()
#2  0x0000555555b8eb05 in LoadTextureFromImage ()
#3  0x00005555559be7fb in PS2Runtime::presentFrame() ()
#4  0x000055555557956c in main::{lambda()#2}::operator()() const ()
#5  0x0000555555579881 in std::thread::_State_impl<std::thread::_Invoker<...>>::_M_run() ()

Thread 1:
#0  IopTimrman::nextEventCycle(unsigned long) const ()
...
#4  0x0000555555732998 in sub_0100F390_0x100f390(unsigned char*, R5900Context*, PS2Runtime*) ()
#5  EeScheduler::serviceInvocations(int) ()
```

### Two findings, and the first one contradicts the brief

**1. The crash is NOT in `fioOpen`.** `[fioOpen] ... -> fd=5` is simply the last line the *guest*
printed before the *other* thread died. The guest was healthy — Thread 1 was mid-`fioOpen`, which is
the guest opening its own disc file, entirely normal work. Thread 4 died underneath it. Reading the
log's last line as the crash site is the trap here.

**2. The two threads were the wrong way round.** The comment above the display loop said
*"raylib's drawing calls are not thread safe"* — **correct about the hazard** — and then did the one
thing guaranteed to trigger it:

- `runtime.initialize()` calls raylib's `InitWindow` at **`tools/harness/vulcan4_harness.cpp:1328`**,
  **on the main thread**. Main therefore owns the OpenGL context.
- The display loop was a `std::thread`, so `presentFrame()` → `LoadTextureFromImage` →
  `rlLoadTexture` → `glGenTextures`/`glTexImage2D` ran **on a thread that never made that context
  current**. That is the segfault, in `libGLdispatch`.
- The guest loop was on **main** (`:1904`), i.e. the guest owned the GL context and the display did
  not have it. Inverted from the shape the brief describes as intended.

It survived under `xvfb` because `swrast` takes a different dispatch path. It dies on the real
desktop GL driver — which is exactly where the captain saw it.

### The fix

The guest now runs on a worker thread and **main — the GL owner — presents**:

```cpp
// guest on a worker
std::thread guestThread([&]() {
    while (!finished) { ... }
    guestDone.store(true, std::memory_order_release);
});

// display on MAIN, which owns the GL context
displayLoop();
if (guestThread.joinable()) guestThread.join();
```

Plus a new `guestDone` flag, because the display loop used to end when the *same thread* that ran the
guest ended; now it must notice the worker has finished, or it would present a dead frame until the
watchdog fired.

No lock was added and none was needed: the GS `latch`/`copy` pair already existed precisely to make
the handoff safe. The bug was never a missing lock — it was calling GL from the wrong thread.

---

## 2. THE NUMBERS, BEFORE AND AFTER

All runs `DISPLAY=:0`, the captain's real path.

| run | `gs_packets` | `frames_presented` | outcome |
|---|---|---|---|
| W106, no display call at all | 1244 | **0** | black window for four days |
| W108 first attempt, `presentFrame()` inline on the guest thread | **29** | 3407 | **the game was dead**; `runningThreadId=0` |
| `boot_desk221535.log`, display on a spawned thread | — | — | **SIGSEGV, no BOOT REPORT at all** |
| **W108 fixed: display on main, guest on worker** | **3309** | **2387** | **clean report, no crash** |

**Throughput went UP, not down.** 3309 packets against W106's 1244 with no display call. The
starvation that killed the inline attempt is gone because the display is no longer competing with
the guest for the GS lock from the guest's own thread — it is a different thread entirely, which is
what the inline version was pretending to be.

### The gate

```
$ bash /home/or/vulcan4/.auto/queue/50-w108-verify.sh
frames_presented=2387 gs_packets=3309 halt=wallclock_deadline
GATE PASS: the window draws (2387 frames) and the guest stayed alive (3309 packets).
```

One harness change was needed to reach that, and it is a real inconsistency rather than a nudge:
**the `BOOT REPORT` line never carried `frames_presented`.** It was printed only on the `HARNESS
detail` line. So a run that ended early produced a report with no display number at all, and the
gate read an empty string. `frames_presented` is now on the report line, where the product question
("does the captain see anything") belongs.

### Suite

`493 tests, 492 passed, 1 failed` — `VU0 macro mappings cover all S1/S2 enums`, the known
working-directory artefact. This dish touched only `tools/harness/`; no runtime or test file was
modified, so the suite is unchanged by construction.

---

## 3. WHY THE PICTURE IS STILL BLACK — named, with measurements

**It is still black.** No fake picture was drawn, no test pattern, no placeholder, no constant
address. Per law 2.

### `fbp` is not simply "zero" — it toggles

Across all `[gs:disp]` samples in the gate run:

```
7  frame0.fbp=0
1  frame0.fbp=160        <- 0x160, written once
7  frame0.psm=1
1  frame0.psm=0
```

So the guest **does** write `FRAME_1` with a non-zero base, exactly once per run, at
`DISPFB2=0x9400`. And `gs_frame_reg_writes=660 (ctx0=660 ctx1=0)` says it wrote FRAME/ZBUF **660
times**. So the register writes are happening; they are not simply absent.

But there is **not one `reg=0x4c` line in the log**, despite 660 counted FRAME writes. The GS's debug
recorder whitelists which registers it prints (`recordRegisterDebugEventUnlocked`,
`gs_frontend.cpp:368`) and the counter increments on a different path. **So the register trace is
not currently able to tell us which of those 660 writes carried a base.** That is the single
measurement that would settle it, and it is one line of instrumentation.

`0x160` is 352 in `FBP` units — 352 × 256 B = `0x16000`. It is not a plausible framebuffer for a
640×480 32-bit display, so it is more likely a pointer to something than a framebuffer base.

### The likelier root cause, already documented in this project

`docs/GS-PLAN.md` (G2.1, carried into `LIMITATIONS.md` as a gating defect) says:

> `GSRegisters` is the display block, and the presentation path decodes `DISPFB`/`DISPLAY` in
> **ps2xRuntime's own bit layout, not the hardware layout**. A real EE writing genuine hardware
> `DISPLAY` values will be misread by this path.

The values on the wire look like genuine hardware: `DISPFB1=0x206502c007002090`,
`DISPFB1.X=0x90`, `DISPFB1.Y=4`, `fbw=0x10` (640 px), `psm=1` (32-bit), `DISPFB2` non-zero (double
buffered). That is a correct PS2 display setup **written by GT4 itself**. Our reader is looking at
those bits through a private layout, so a correct register set decodes to `fbp=0`.

**That is the named next wall, and it is consistent with every number above** — including the one
sample where `fbp=160` appears, which would be the private layout briefly landing on a plausible
value by accident rather than by design.

I did not touch it, because "fixing" it by pointing the framebuffer at a constant address or painting
a pattern is precisely the fake the brief forbids, and a runtime layout change is not something to
land at 4am on the strength of one run.

### Next single measurement

Print the 660 `FRAME_1` write values as they happen, from the path that increments the counter — not
from the debug recorder — and read off whether GT4 ever writes a non-zero base in the register whose
bits our reader actually uses. That decides between "the guest never sets it" and "the guest sets it
and we read it wrong", and those need completely different fixes.

---

## 4. THE THREE LAWS

1. **No faking.** The picture is black and this document says so. Nothing was painted.
2. **Measured before changing.** The number I moved was thread ownership, and the measurement that
   justified it was a backtrace, not a guess. Throughput rose (1244 → 3309) rather than falling, so
   nothing was traded away.
3. **A commit is not proof.** The gate passed, but the gate is a log line.

## 5. THE HONEST SENTENCE

**No — the captain's window does not show GT4's picture yet.** It no longer crashes, it presents 2387
real frames from the guest's own GS writes, and the guest stays alive with *more* GS traffic than
before the display existed; but the framebuffer base is decoded through a private register layout, so
what reaches the window is still black, and I will not pretend otherwise with a test pattern.