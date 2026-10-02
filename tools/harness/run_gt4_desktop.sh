#!/usr/bin/env bash
# THE CAPTAIN'S RUNNER. Two windows on the real desktop: the game, and its own logs.
#
# Why this exists: for 60+ dishes the only way anyone saw this project was a text log.
# The captain judges the PRODUCT -- a picture -- so the product has to open a window on
# his desktop, beside its own logs, with no xvfb and no remote setup.
#
# TWO WINDOWS, ONE PROCESS. The harness opens its own raylib window (runtime.initialize
# does it, that is the "VULCAN 4 - first boot" window), so running a second harness for
# the "game window" would be a SECOND BOOT: two guests fighting over the same files, and
# every number on screen belonging to neither. So: one process, its raylib window lands
# on DISPLAY, its stdout lands in the terminal below it.
#
# Same binary and same arguments as tools/harness/run_boot_named.sh, so what you watch is
# what the measurements are taken from. Nothing is faked for the window: if the GS draws
# nothing, the window is black -- and black is the honest answer.
set -u

: "${DISPLAY:=:0}"
export DISPLAY

BUILD_DIR="${VULCAN4_BUILD:-/mnt/ssd/vulcan4-build}"
RUN_DIR="$BUILD_DIR/run"
GAME_DIR="${GT4_DIR:-/mnt/ssd/gt4/work}"
CONFIG="${GT4_CONFIG:-$GAME_DIR/gt4.toml}"
HARNESS="$RUN_DIR/vulcan4_harness"
ENTRIES="${1:-6000000}"
SECONDS_BUDGET="${2:-600}"
TAG="desk$(date +%H%M%S)"
LOG="$RUN_DIR/boot_${TAG}.log"

if pgrep -f 'vulcan4_harness /mnt/ssd' >/dev/null 2>&1; then
    echo "A boot is ALREADY RUNNING: pid $(pgrep -f 'vulcan4_harness /mnt/ssd' | tr '\n' ' ')"
    echo "Two boots fight over the same files and every number belongs to neither."
    exit 1
fi

for f in "$HARNESS" "$GAME_DIR/SCUS_973.28" "$CONFIG"; do
    [ -e "$f" ] || { echo "MISSING: $f"; exit 1; }
done

command -v xfce4-terminal >/dev/null || { echo "MISSING: xfce4-terminal"; exit 1; }
[ -S "/tmp/.X11-unix/X${DISPLAY#:}" ] || [ "$DISPLAY" = ":0" -a -S /tmp/.X11-unix/X0 ] \
    || echo "WARNING: no X socket for $DISPLAY -- the game window will not appear."

mkdir -p "$RUN_DIR"
{
    echo "# DESKTOP RUN tag=$TAG entries=$ENTRIES budget=${SECONDS_BUDGET}s at $(date '+%F %T')"
    echo "# harness=$(stat -c %y "$HARNESS" 2>/dev/null)"
    echo "# runtime=$(stat -c %y "$BUILD_DIR/ps2xRuntime/libps2_runtime.a" 2>/dev/null)"
    echo "# generated=$(stat -c %y "$BUILD_DIR/recomp/ps2_recompiled_functions.cpp" 2>/dev/null)"
    echo "# display=$DISPLAY (real desktop, NOT xvfb)"
} > "$LOG"

# WINDOW 1 of 2 -- the logs. The harness's raylib window pops up over this one.
# --hold keeps the log readable after the boot ends, which is when you want to read it.
xfce4-terminal --title="VULCAN 4 - logs ($TAG)" --geometry=104x32+0+0 --hold \
    --command="bash -c 'cd \"$RUN_DIR\"; echo \"GT4 recomp booting on $DISPLAY -- log: $LOG\"; echo; nice -n 10 ionice -c3 timeout $((SECONDS_BUDGET + 60)) ./vulcan4_harness \"$GAME_DIR/SCUS_973.28\" \"$CONFIG\" $ENTRIES $SECONDS_BUDGET 2>&1 | tee -a \"$LOG\"; echo; echo \"=== boot ended. full log: $LOG ===\"; read -r -p \"press enter to close\" _'"

echo
echo "Launched. You should see:"
echo "  1. 'VULCAN 4 - first boot'  <- THE GAME. Black means the GS has drawn nothing."
echo "  2. 'VULCAN 4 - logs'         <- the boot, live"
echo "  log file: $LOG"
echo "Stream it to your phone with Moonlight -> Cortex if you are not at the desk."