#!/usr/bin/env bash
# W251. Boot the harness under a private X server and CAPTURE THE WINDOW, because the deliverable
# this dish is judged on is a picture and a headless run has no window to look at.
#
# Why not `xvfb-run`: it picks the display number and, more importantly, it gives you no handle on
# the window -- `import -window root` grabs the whole 1280x1024 root, so the capture is a small game
# frame in a sea of black and every comparison against a 640x448 reference is comparing the wrong
# thing. The stored reference is 640x448 (the raylib window size), so the capture must be the WINDOW.
# The window is found by geometry, not by title: W101 makes the title a live HUD ("... 60 FPS ..."),
# so a --name match on a fixed string would start failing the moment the number changes.
#
# The X server is OURS (:99 unless VULCAN4_DISPLAY says otherwise) -- never the user's session.
#
# usage: run_capture.sh TAG [entries] [seconds] [capture-every-seconds]
set -u
TAG="${1:?usage: run_capture.sh TAG [entries] [seconds] [capture-every]}"
ENTRIES="${2:-2000000}"
SECS="${3:-12}"
EVERY="${4:-3}"
DISP="${VULCAN4_DISPLAY:-:101}"
RUN=/mnt/ssd/vulcan4-build/run
OUT="$RUN/boot_${TAG}.log"
LOG="$RUN/${TAG}-captures.log"

cd "$RUN" || exit 1
{
  echo "# W251 CAPTURE RUN tag=$TAG entries=$ENTRIES budget=${SECS}s display=$DISP at $(date +%Y%m%d_%H%M%S)"
  echo "# harness=$(stat -c %y vulcan4_harness 2>/dev/null)"
} > "$OUT"

# Refuse to reuse a display somebody else may be on: killing their X server to take a screenshot
# would be a spectacular own goal. Pick a free one instead.
if [ -e "/tmp/.X${DISP#:}-lock" ]; then
  echo "run_capture: display $DISP is in use; set VULCAN4_DISPLAY to a free one" >&2
  exit 1
fi
Xvfb "$DISP" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!
sleep 1.5

DISPLAY="$DISP" nice -n 10 ionice -c3 timeout "$((SECS + 90))" \
  ./vulcan4_harness /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml \
  "$ENTRIES" "$SECS" >> "$OUT" 2>&1 &
HPID=$!

# The window, found by ITS OWN GEOMETRY (the raylib window is 640x448; nothing else on this private
# display is). Report loudly if it never appears -- an empty capture set that still exits "ok" would
# be the same class of lie as a green build with no binary.
find_window() {
  for w in $(DISPLAY="$DISP" xdotool search --onlyvisible --name '.' 2>/dev/null); do
    eval "$(DISPLAY="$DISP" xdotool getwindowgeometry --shell "$w" 2>/dev/null)" || continue
    if [ "${WIDTH:-0}" = "640" ] && [ "${HEIGHT:-0}" = "448" ]; then echo "$w"; return 0; fi
  done
  return 1
}

: > "$LOG"
WID=""
n=0
end=$(( $(date +%s) + SECS + 30 ))
while [ "$(date +%s)" -lt "$end" ]; do
  kill -0 "$HPID" 2>/dev/null || break
  if [ -z "$WID" ]; then WID=$(find_window || true); fi
  if [ -n "$WID" ]; then
    n=$((n + 1))
    f="$RUN/${TAG}-win-$(printf '%02d' "$n").png"
    if DISPLAY="$DISP" import -window "$WID" "$f" 2>/dev/null; then
      echo "captured $f $(stat -c %s "$f") bytes" >> "$LOG"
    else
      echo "import failed for $f" >> "$LOG"
    fi
  else
    echo "no 640x448 window on $DISP yet" >> "$LOG"
  fi
  sleep "$EVERY"
done

wait "$HPID" 2>/dev/null
kill "$XPID" 2>/dev/null
wait "$XPID" 2>/dev/null

LAST=$(ls -t "$RUN"/${TAG}-win-*.png 2>/dev/null | head -1)
if [ -n "$LAST" ]; then
  cp "$LAST" "$RUN/${TAG}-capture.png"
  echo "wrote $RUN/${TAG}-capture.png (from $LAST)"
else
  echo "NO CAPTURE was taken -- no 640x448 window ever appeared on $DISP" >&2
fi
echo "wrote $OUT ($(stat -c %s "$OUT") bytes), $n frame(s)"
