#!/usr/bin/env bash
# Build the G2.0 GS skeleton. Output goes to the SSD, never into the repo.
set -euo pipefail

REPO=/home/or/vulcan4
SRC=$REPO/tools/PS2Recomp
OUT=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}/gs
BIN=$OUT/vulcan4_gs_probe

mkdir -p "$OUT"

g++ -std=c++20 -O1 -msse4.1 \
    -I"$SRC/ps2xRuntime/include" \
    -I"$SRC/ps2xRuntime/src/lib" \
    -o "$BIN" \
    "$REPO/tools/gs/vulcan4_gs_probe.cpp" \
    /mnt/ssd/vulcan4-build/ps2xRuntime/libps2_runtime.a \
    /mnt/ssd/vulcan4-build/ps2xIOP/libps2_iop.a \
    -lz -lpthread -ldl -lm

echo "built $BIN"
