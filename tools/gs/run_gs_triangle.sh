#!/usr/bin/env bash
# Run the G2.5 triangle oracle in both modes and leave the newest triangle*.png as the FLAT frame.
#
# Why the ordering matters: the dish's VERIFY gate globs /mnt/ssd/vulcan4-build/gs/triangle*.png and
# measures the NEWEST one, so the flat frame has to be written last. Both frames carry the same
# geometry and therefore the same analytic area, so either one satisfies the gate -- this is about
# determinism, not about which frame is more valid.
set -euo pipefail

OUT=${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}/gs
BIN=$OUT/vulcan4_gs_triangle
LOG=$OUT/triangle-oracle.log

mkdir -p "$OUT"
: > "$LOG"

echo "=== TEXTURED ===" | tee -a "$LOG"
"$BIN" "$OUT/triangle-texture.png" --texture 2>&1 | grep -a -v '^\[gs:' | tee -a "$LOG"
sleep 1
echo "=== FLAT ===" | tee -a "$LOG"
"$BIN" "$OUT/triangle.png" 2>&1 | grep -a -v '^\[gs:' | tee -a "$LOG"

touch "$OUT/triangle.png"
echo "frames: $OUT/triangle.png (newest) $OUT/triangle-texture.png" | tee -a "$LOG"