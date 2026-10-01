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

R=${VULCAN4_REPO:-/home/or/vulcan4}/tools/PS2Recomp
B=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}
G=$B/recomp
INC="-I$G -I$R/ps2xRuntime/include -I$R/ps2xRecomp/include -I$R/ps2xRuntime/src/lib/Kernel -I$R/ps2xIOP/include"
DEFS="-DPS2_RUNTIME_LOGS=1 -DAGRESSIVE_LOGS=1 -DPS2_FUNCTION_LOG_TRACKER=1 -DPS2X_ENABLE_IOP_RPC_TRACE=1 -DPS2X_HAS_FFMPEG=1"

mkdir -p "$B/run"
cd "$B/run"

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
#   -O2 : functions_entered=1747058  distinct_pcs=152  vsync_tick=31     (+16.3 %)
#
# So -O2 is a strict improvement -- more work done and slightly further into the guest -- and it is
# what this script now uses. But note what 16% means: the translated arithmetic is NOT the
# bottleneck, or -O2 would have won by much more. The per-entry cost is dominated by the dispatch
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
if [ ! -f ps2_recompiled_functions.o ] || [ "$G/ps2_recompiled_functions.cpp" -nt ps2_recompiled_functions.o ] || [ "$R/ps2xRuntime/include/ps2_runtime_macros.h" -nt ps2_recompiled_functions.o ] || [ "$R/ps2xRuntime/include/ps2_runtime.h" -nt ps2_recompiled_functions.o ]; then
    nice -n 10 g++ -std=c++20 -O2 -msse4.1 $INC $DEFS -c "$G/ps2_recompiled_functions.cpp" -o ps2_recompiled_functions.o
fi

FFMPEG=$(pkg-config --libs libavcodec libavformat libavutil libswresample libswscale)
nice -n 10 g++ -o vulcan4_harness harness.o register_functions.o ps2_recompiled_functions.o \
  "$B/ps2xRuntime/libps2_runtime.a" "$B/ps2xIOP/libps2_iop.a" \
  "$B/_deps/raylib-build/raylib/libraylib.a" $FFMPEG -lpthread -ldl -lm -lrt -lX11

echo "built $B/run/vulcan4_harness"
