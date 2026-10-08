#!/usr/bin/env bash
# Build the W8 driver (tools/harness/vulcan4_harness.cpp) against the runtime and the
# generated translation unit, exactly as docs/FIRST-BOOT.md section 6 records it.
#
# Why this is a script: the harness is NOT in the CMake tree (PS2Recomp has no
# vulcan4_harness target), so its build is a four-line g++ invocation that every dish
# has to re-type. Typo it and you link against a stale libps2_runtime.a, measure the
# OLD runtime, and report a fix that is not in the binary. That has happened.
#
# -j2 + nice: a live Minecraft server shares this box (AGENTS.md law 11).
set -euo pipefail

# W276 R26. RESOLVE THIS SCRIPT'S OWN DIRECTORY BEFORE ANY `cd`.
#
# The gen_syscall_names.py call below used `dirname "$(readlink -f "$0")"`, but this script does
# `cd "$B/run"` first -- so an invocation with a RELATIVE path (`bash tools/harness/build_harness.sh`)
# resolved $0 against the NEW cwd, where `tools/harness/` does not exist, and `readlink -f` printed
# nothing. dirname "" is ".", so the generator was looked for as $B/run/gen_syscall_names.py, was not
# found, and the build stopped with "syscall names would be stale". MEASURED 2026-10-08 on exactly
# that invocation. Absolute invocations never hit it, which is why it survived this long.
SCRIPT_DIR=$(cd -- "$(dirname -- "$(readlink -f -- "$0")")" && pwd)

R=${VULCAN4_REPO:-/home/or/vulcan4}/tools/PS2Recomp
B=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}
G=$B/recomp
# W101: raylib.h, so the harness can call SetWindowTitle() and put the live FPS/speed in the
# game window's own title bar. It was already on the LINK line below (libraylib.a), it just
# was never on the COMPILE line -- the runtime gets it transitively through
# ps2_host_backend.h, but nothing outside the runtime can. Same library, both halves.
INC="-I$G -I$R/ps2xRuntime/include -I$R/ps2xRecomp/include -I$R/ps2xRuntime/src/lib/Kernel -I$R/ps2xIOP/include -I$B/_deps/raylib-src/src"
# W97. LOGGING IS OFF UNLESS ASKED FOR, AND THIS IS THE LARGEST COST IN THE PROGRAM.
#
# Measured with strace on a 20 s boot:
#     write     843,367 calls          42,168 per second
#     getpid     13,828 calls
#     futex        646 calls     297,185 us/call  (idle, not cost)
#
# 843,367 writes in 20 seconds is 42,168 per second, and PS_LOG_ENTRY compiles to ps2_log::log_entry
# plus a guard whose destructor calls log_exit -- and BOTH end in log_stream().flush(). Two write(2)
# syscalls for every guest function entry. 843,367 / 2 is 21,084 guest function entries per second:
# the real rate this guest runs at, and it matches the ~33K/s the build script's own benchmark claims.
# It was never 21/s -- the harness counter was counting outer dispatcher iterations, which inline
# callees, so it under-reported by about a thousand.
#
# So the default build must not log. VULCAN4_LOGS=1 for a run that wants the trace.
# W98. PS2_FUNCTION_LOG_TRACKER IS NOT THE LOGGING. Keeping them apart is what makes the project's
# headline number measurable at all.
#
# Every generated function is written as
#     #ifdef PS2_FUNCTION_LOG_TRACKER
#         PS_LOG_ENTRY("sub_...");
#     #endif
# and PS_LOG_ENTRY, since W97, ALWAYS does one relaxed atomic increment and only does I/O when
# PS2_RUNTIME_LOGS/AGRESSIVE_LOGS are on. So the tracker define is now a ~1 nanosecond counter and the
# logging defines are the expensive part. Dropping the tracker define with the logging -- which is
# exactly what I did first -- silently removed the counter from all 639 generated functions and
# true_guest_entries read 0 for a whole build-and-run cycle.
DEFS="-DPS2_FUNCTION_LOG_TRACKER=1 -DPS2X_ENABLE_IOP_RPC_TRACE=1 -DPS2X_HAS_FFMPEG=1"
if [ "${VULCAN4_LOGS:-0}" = "1" ]; then
  DEFS="$DEFS -DPS2_RUNTIME_LOGS=1 -DAGRESSIVE_LOGS=1"
  echo "build_harness: PS2_RUNTIME_LOGS ON (diagnostic build)"
fi
# W213. ROUTE GENERATED `jr $ra` RETURNS THROUGH dispatchGuestBranch, so a return becomes a MEASURED
# edge instead of an inference (the W210 scoreboard's `returns` counter reads 0 otherwise). OFF by
# default: it is a diagnostic build and it changes codegen for every function in the game.
if [ "${VULCAN4_RETURNS:-0}" = "1" ]; then
  DEFS="$DEFS -DPS2X_STRICT_RETURN_DIAGNOSTICS=1"
  echo "build_harness: PS2X_STRICT_RETURN_DIAGNOSTICS ON (return edges measured)"
fi

# W96. FRAME POINTERS, ON REQUEST ONLY. The sampling profiler needs to walk the stack to say WHO
# called the libc function that takes 75% of the CPU, and glibc's memcpy/memset variants are not leaf
# functions, so [RSP] is not the return address and guessing it produced garbage callers. -fno-omit-
# frame-pointer changes codegen for the whole program, so it is a diagnostic build, not the default:
# VULCAN4_FRAMEPTR=1 for a profile run, nothing for a normal one.
FRAMEPTR=""
if [ "${VULCAN4_FRAMEPTR:-0}" = "1" ]; then
  FRAMEPTR="-fno-omit-frame-pointer"
  echo "build_harness: frame pointers ON (diagnostic build)"
fi

mkdir -p "$B/run"
cd "$B/run"

# W275-R3. REGENERATE syscall_names.h -- IT WAS A GENERATED ARTIFACT WITH NO GENERATOR IN THE BUILD.
#
# vulcan4_harness.cpp:50 includes runtime/syscall_names.h to NAME the syscalls in the no-BIOS
# backlog. The header lives in the (gitignored) upstream tree, so it is a build artifact -- but
# nothing here ever regenerated it, and the copy on disk had been frozen since Sep 30 with 58
# entries while the dispatcher had grown to 114. Measured cost: every syscall wired after that date
# printed as `sce_unnamed_syscall`, so a *wiring gap* and a *genuinely unknown syscall* looked
# identical in the log. That is the most expensive kind of wrong a log can be -- it is how 0x79,
# 0x7A, 0x78, 0x77, 0x2F (45,894 calls) and 0x07 read as unnamed in /mnt/ssd/vulcan4-build/run/
# boot_w275r3.log.
#
# There is no timestamp check here on purpose: the script it writes is 140 lines of pure parse and
# costs milliseconds, and a staleness check is the thing that failed. Run it, then the generated unit
# is compiled from whatever it wrote. If the generator itself errors the build stops (set -e).
if ! python3 -I "$SCRIPT_DIR/gen_syscall_names.py"; then
    echo "build_harness: gen_syscall_names.py FAILED -- syscall names would be stale" >&2
    exit 1
fi

# The runtime archive is a CMake target; rebuild it so a runtime change is never
# hidden behind a stale .a.
nice -n 10 cmake --build "$B" --target ps2_runtime -j2 >/dev/null

for unit in "harness:/home/or/vulcan4/tools/harness/vulcan4_harness.cpp:harness.o" \
            "register:$G/register_functions.cpp:register_functions.o"; do
    name=${unit%%:*}; rest=${unit#*:}; src=${rest%%:*}; obj=${rest#*:}
    if [ ! -f "$src" ]; then
        echo "MISSING $src -- is the recompiler output present?" >&2
        exit 1
    fi
    nice -n 10 g++ -std=c++20 -O1 -msse4.1 $INC $DEFS -c "$src" -o "$obj"
done

# The generated unit is the biggest translation unit in the project, so its optimisation level is
# a measured decision, not a habit.
#
# It used to be -O0, justified here by "costs nothing at 20M guest entries a second". That claim was
# never measured and it is false by three orders of magnitude: a 45 s boot enters 1,501,874 guest
# functions, i.e. ~33K/s. Measured head to head at the same 45 s wall clock, same binary otherwise:
#
#   -O0 : functions_entered=1501874  distinct_pcs=149  vsync_tick=27
#   $FRAMEPTR -O2 : functions_entered=1747058  distinct_pcs=152  vsync_tick=31     (+16.3 %)
#
# So $FRAMEPTR -O2 is a strict improvement -- more work done and slightly further into the guest -- and it is
# what this script now uses. But note what 16% means: the translated arithmetic is NOT the
# bottleneck, or $FRAMEPTR -O2 would have won by much more. The per-entry cost is dominated by the dispatch
# machinery, and above all by the generated code's habit of writing `ctx->pc` on EVERY guest
# instruction -- a store per instruction that the optimiser cannot remove because `ctx` escapes. If
# throughput ever becomes the wall rather than correctness, that store is the thing to attack, not
# this flag. Do not "optimise" this line again on a hunch; re-run the 45 s pair above and put the
# numbers in docs/HANDOFF.md.
#
# G1.8h, and it is the SAME trap this script was written to kill, one level deeper: the .cpp is
# not the only input. Every READ32/READ64/WRITE32 expands a macro from
# ps2xRuntime/include/ps2_runtime_macros.h, so changing that header changes the generated code's
# MEANING without changing the .cpp's mtime. A timestamp check on the .cpp alone silently keeps the
# old object, and a probe built into the macros then never fires -- which reads exactly like "the
# guest never does the thing", which is the most expensive kind of wrong.
# W98. THE HEADER LIST WAS AN INVENTORY, AND IT WAS INCOMPLETE.
#
# The four -nt tests below used to name the .cpp and two headers. ps2_log.h was missing, so changing
# PS_LOG_ENTRY -- which is expanded inside every one of the 639 generated functions -- did NOT rebuild
# the generated unit. W97 spent a full build-and-run cycle measuring true_guest_entries=0 and the
# cause was not the counter, it was that the object being linked predated the change by an hour. The
# script's own comment, forty lines above, explains this exact failure mode and then misses a header.
#
# So: do not keep a list. Take the NEWEST mtime under ps2xRuntime/include and compare that. Every
# header in there is transitively included by the generated unit, so this cannot come back, and it is
# one `find` instead of a list that has to be edited every time somebody adds a header.
NEWEST_HEADER=$(find "$R/ps2xRuntime/include" -name '*.h' -newer ps2_recompiled_functions.o -print -quit 2>/dev/null || true)
# W98b. A STAMP, NOT A TIMESTAMP. The flags are an input to the object, and the only place they exist
# is this file, so mtime comparisons on the .cpp and the headers cannot see them. Dropping
# -DPS2_FUNCTION_LOG_TRACKER from DEFS removed the entry counter from all 639 generated functions and
# true_guest_entries read 0 for a full build-and-run cycle, with a green build and no warning.
#
# So the flags are written next to the object and compared. If DEFS, the frame-pointer knob, any
# runtime header, or the generated .cpp changes, the object is rebuilt. If none of them do, it is not,
# and the 640-function compile is not paid for a nothing.
STAMP=ps2_recompiled_functions.defs
NEWEST_HEADER=$(find "$R/ps2xRuntime/include" -name '*.h' -newer ps2_recompiled_functions.o -print -quit 2>/dev/null || true)
REBUILD=0
[ -f ps2_recompiled_functions.o ] || REBUILD=1
[ "$G/ps2_recompiled_functions.cpp" -nt ps2_recompiled_functions.o ] && REBUILD=1
[ -n "$NEWEST_HEADER" ] && REBUILD=1
[ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$DEFS $FRAMEPTR" ] || REBUILD=1
if [ "$REBUILD" = "1" ]; then
    echo "$DEFS $FRAMEPTR" > "$STAMP"
    if [ -n "$NEWEST_HEADER" ]; then
        echo "build_harness: rebuilding the generated unit ($(basename "$NEWEST_HEADER") is newer)"
    fi
    nice -n 10 g++ -std=c++20 $FRAMEPTR -O2 -msse4.1 $INC $DEFS -c "$G/ps2_recompiled_functions.cpp" -o ps2_recompiled_functions.o
fi

FFMPEG=$(pkg-config --libs libavcodec libavformat libavutil libswresample libswscale)
# W240. The runtime-loaded ENGINE image (second recompilation target) links BESIDE the loader. Its
# objects come from the bounded-TU emit (recomp_engine_small) and use DISTINCT table symbols.
#
# W251. WHICH emit, named explicitly, because the default must not move. The W251 authoritative
# engine (19,404 functions from Ghidra's ground truth) is a DIFFERENT object set in a different
# directory, and silently linking the old 758-function set while believing you measured the new one
# is exactly the failure this variable exists to prevent. Unset → recomp_engine_small, byte-for-byte
# the previous build.
ENGINE_DIR=${VULCAN4_ENGINE_DIR:-"$B/recomp_engine_small"}
echo "build_harness: engine objects from $ENGINE_DIR"
ENGINE_OBJS=""
for o in "$ENGINE_DIR"/ps2_recompiled_functions_*.o "$ENGINE_DIR"/register_functions.o; do
    [ -f "$o" ] && ENGINE_OBJS="$ENGINE_OBJS $o"
done
if [ -z "$ENGINE_OBJS" ]; then
    echo "build_harness: NO engine objects in $ENGINE_DIR -- the engine image would be unreachable" >&2
    exit 1
fi
nice -n 10 g++ -o vulcan4_harness harness.o register_functions.o ps2_recompiled_functions.o $ENGINE_OBJS \
  "$B/ps2xRuntime/libps2_runtime.a" "$B/ps2xIOP/libps2_iop.a" \
  "$B/_deps/raylib-build/raylib/libraylib.a" $FFMPEG -lpthread -ldl -lm -lrt -lX11

# W276 R26. RECORD WHICH EMIT THE BINARY HOLDS, BESIDE THE BINARY.
#
# The `# generated=` line run_boot_named.sh writes describes the LOADER's emit
# ($B/recomp/ps2_recompiled_functions.cpp). It says NOTHING about the engine objects linked in from
# $ENGINE_DIR above -- but a reader, and a peer reviewer, takes it as the run's provenance. Measured
# 2026-10-08: that exact misreading happened -- boot_w276r26c.log's header named the loader emit, was
# read as naming the engine, and the r26 engine fix was reported as "booted and still failing" when
# it had never been booted at all. The harness mtime already encodes this (it is linked from those
# objects), but nothing spells it out, so put the truth where the run script can echo it.
printf '%s\n' "dir=$ENGINE_DIR" \
  "register_functions.cpp=$(stat -c %y "$ENGINE_DIR/register_functions.cpp" 2>/dev/null)" \
  "objects=$(printf '%s\n' $ENGINE_OBJS | wc -l)" > "$B/run/.engine_stamp"

echo "built $B/run/vulcan4_harness"
echo "build_harness: engine stamp written -- $(head -1 "$B/run/.engine_stamp")"
