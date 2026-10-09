#!/usr/bin/env bash
# W251. Compile the runtime-loaded ENGINE image's generated translation units.
#
# Why a script: the engine emit is split by the recompiler into bounded part files
# (W239, PS2RECOMP_TU_BYTES) and each one is compiled ALONE, at -O0, nice'd and
# ionice'd, because a live Minecraft server shares this box and because a single
# 400 MB translation unit at -O2 is what OOM-killed the box in W238. Typing the
# include order by hand is how you silently compile the ENGINE parts against the
# LOADER's ps2_recompiled_functions.h -- same file name, 723 functions vs 19,404 --
# and then measure a binary that is not the one you emitted. So the include order
# lives here, once, with the engine dir first.
#
# VULCAN4_ENGINE_DIR  which emit to compile (default: $B/recomp_engine_small, W240's)
# VULCAN4_ENGINE_OPT  optimisation level (default -O0; correctness first, and it is
#                     the level whose memory use was measured)
# VULCAN4_ENGINE_JOBS parallel compiles (default 4 -- AGENTS.md law 11)
set -euo pipefail

R=${VULCAN4_REPO:-/home/or/vulcan4}/tools/PS2Recomp
B=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}
E=${VULCAN4_ENGINE_DIR:-$B/recomp_engine_small}
G=$B/recomp
OPT=${VULCAN4_ENGINE_OPT:--O0}
JOBS=${VULCAN4_ENGINE_JOBS:-4}
export TMPDIR=${TMPDIR:-/mnt/ssd/tmp}

# $E FIRST. Its ps2_recompiled_functions.h declares the engine's own functions; the
# loader's identically-named header declares a disjoint set. Wrong order = link errors
# you would waste an afternoon on, or worse, a silent mismatch.
INC="-I$E -I$G -I$R/ps2xRuntime/include -I$R/ps2xRecomp/include -I$R/ps2xRuntime/src/lib/Kernel -I$R/ps2xIOP/include -I$B/_deps/raylib-src/src"
DEFS="-DPS2_FUNCTION_LOG_TRACKER=1 -DPS2X_ENABLE_IOP_RPC_TRACE=1 -DPS2X_HAS_FFMPEG=1"

[ -d "$E" ] || { echo "no engine emit at $E" >&2; exit 1; }

cd "$E"
# W277. A runtime header change (ps2_runtime_macros.h, the WRITE32/store-watch macros) changes the
# MEANING of every generated TU without changing its .cpp or the engine's own .h. Same trap as
# build_harness.sh W98: take the newest header under ps2xRuntime/include and rebuild when it is newer.
NEWEST_RUNTIME_HEADER=$(find "$R/ps2xRuntime/include" -name '*.h' -newer "$E/ps2_recompiled_functions.h" -print -quit 2>/dev/null || true)
total=0
for src in ps2_recompiled_functions_[0-9][0-9].cpp register_functions.cpp; do
    [ -f "$src" ] || continue
    obj="${src%.cpp}.o"
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ] && [ "$obj" -nt "$E/ps2_recompiled_functions.h" ] && [ -z "$NEWEST_RUNTIME_HEADER" ]; then
        continue
    fi
    total=$((total + 1))
done
echo "build_engine: $E, $OPT, -j$JOBS, $total unit(s) to (re)build"

build_one() {
    src=$1
    obj="${src%.cpp}.o"
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ] && [ "$obj" -nt "$E/ps2_recompiled_functions.h" ] && [ -z "$NEWEST_RUNTIME_HEADER" ]; then
        return 0
    fi
    nice -n 10 ionice -c3 g++ -std=c++20 $OPT -msse4.1 $INC $DEFS -c "$src" -o "$obj" 2>"$E/${src%.cpp}.cc.log"
    echo "  ok $obj $(stat -c%s "$obj")"
}
export -f build_one
export NEWEST_RUNTIME_HEADER
export E INC DEFS OPT

printf '%s\n' ps2_recompiled_functions_[0-9][0-9].cpp register_functions.cpp \
    | while read -r s; do [ -f "$E/$s" ] && echo "$s"; done \
    | nice -n 10 ionice -c3 xargs -P "$JOBS" -I{} bash -c 'build_one "$@"' _ {}

echo "build_engine: done"
