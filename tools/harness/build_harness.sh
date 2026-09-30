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

# The generated unit is the biggest translation unit in the project; -O0 keeps the
# build inside a sane time and costs nothing at 20M guest entries a second.
if [ ! -f ps2_recompiled_functions.o ] || [ "$G/ps2_recompiled_functions.cpp" -nt ps2_recompiled_functions.o ]; then
    nice -n 10 g++ -std=c++20 -O0 -msse4.1 $INC $DEFS -c "$G/ps2_recompiled_functions.cpp" -o ps2_recompiled_functions.o
fi

FFMPEG=$(pkg-config --libs libavcodec libavformat libavutil libswresample libswscale)
nice -n 10 g++ -o vulcan4_harness harness.o register_functions.o ps2_recompiled_functions.o \
  "$B/ps2xRuntime/libps2_runtime.a" "$B/ps2xIOP/libps2_iop.a" \
  "$B/_deps/raylib-build/raylib/libraylib.a" $FFMPEG -lpthread -ldl -lm -lrt -lX11

echo "built $B/run/vulcan4_harness"
