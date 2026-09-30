#!/usr/bin/env bash
# Build the G2.5 triangle oracle. Output goes to the SSD, never into the repo.
# -j2-equivalent: this is a single translation unit, so the compiler is one process, and it is
# wrapped in nice/ionice because a live Minecraft server shares this machine.
set -euo pipefail

REPO=/home/or/vulcan4
SRC=$REPO/tools/PS2Recomp
OUT=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}/gs
BIN=$OUT/vulcan4_gs_triangle

mkdir -p "$OUT"

# AGRESSIVE_LOGS=1 makes the runtime print its own [gs:kick] / [gs:prim] trace, which is how the
# submission is checked to have reached the rasteriser. The library it links was built with the same
# option, but the macro is read from this translation unit too, so set it explicitly.
nice -n 10 ionice -c3 g++ -std=c++20 -O1 -msse4.1 -DAGRESSIVE_LOGS=1 \
    -I"$SRC/ps2xRuntime/include" \
    -I"$SRC/ps2xRuntime/src/lib" \
    -o "$BIN" \
    "$REPO/tools/gs/vulcan4_gs_triangle.cpp" \
    "${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}/ps2xRuntime/libps2_runtime.a" \
    "${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}/ps2xIOP/libps2_iop.a" \
    -lz -lpthread -ldl -lm

echo "built $BIN"