#!/usr/bin/env bash
# W67. A RUN SCRIPT, so the log that gets measured is named by the thing that produced it.
# boot_span.log appeared in no run script at all, which is how a log 1.5 h older than the runtime
# archive it described became evidence. If the log name is derived here, it cannot go stale silently.
set -u
TAG="${1:?usage: run_boot_named.sh TAG [entries] [seconds]}"
ENTRIES="${2:-2000000}"
SECONDS_BUDGET="${3:-12}"
RUN=/mnt/ssd/vulcan4-build/run
OUT="$RUN/boot_${TAG}.log"
STAMP=$(date +%Y%m%d_%H%M%S)

# W276 R26. THE BINARY IS NAMED RELATIVE, SO THE CWD MATTERS -- AND THIS SCRIPT NEVER SET IT.
#
# run_capture.sh does `cd "$RUN"` before running the harness; this script did not. So an invocation
# from the repo root (`bash tools/harness/run_boot_named.sh TAG 2000000 45`) produced a header-only log
# whose last line was `/usr/bin/xvfb-run: 184: ./vulcan4_harness: not found` -- MEASURED 2026-10-08
# 19:58, /mnt/ssd/vulcan4-build/run/boot_w276r26on.log (455 bytes). A log file that exists and holds no
# run is the exact shape a reader mistakes for a measurement, which is the failure this whole script
# was written to prevent. cd, and refuse loudly if the binary is not where the run needs it.
cd "$RUN" || exit 1
if [ ! -x ./vulcan4_harness ]; then
  echo "run_boot_named.sh: no executable ./vulcan4_harness in $RUN -- build the harness first" >&2
  exit 1
fi
{
  echo "# W67 NAMED RUN tag=$TAG entries=$ENTRIES budget=${SECONDS_BUDGET}s at $STAMP"
  echo "# harness=$(stat -c %y /mnt/ssd/vulcan4-build/run/vulcan4_harness 2>/dev/null)"
  echo "# runtime=$(stat -c %y /mnt/ssd/vulcan4-build/ps2xRuntime/libps2_runtime.a 2>/dev/null)"
  echo "# generated=$(stat -c %y /mnt/ssd/vulcan4-build/recomp/ps2_recompiled_functions.cpp 2>/dev/null)"
  # W276 R26. `# generated=` above is the LOADER emit. The ENGINE this binary actually holds is what
  # VULCAN4_ENGINE_DIR named at LINK time, and build_harness.sh now stamps it beside the binary.
  # Without this line a reader takes `generated=` for the run's whole provenance and reads a stale
  # engine's log as evidence about a new one -- which happened on 2026-10-08 (boot_w276r26c.log).
  if [ -f /mnt/ssd/vulcan4-build/run/.engine_stamp ]; then
    echo "# engine=$(tr '\n' ' ' < /mnt/ssd/vulcan4-build/run/.engine_stamp)"
  else
    echo "# engine=(unknown: no .engine_stamp; rebuild the harness to record it)"
  fi
  echo "# argv: $0 $TAG $ENTRIES $SECONDS_BUDGET"
} > "$OUT"
nice -n 10 ionice -c3 timeout "$((SECONDS_BUDGET + 60))" xvfb-run -a \
  ./vulcan4_harness /mnt/ssd/gt4/work/SCUS_973.28 /mnt/ssd/gt4/work/gt4.toml \
  "$ENTRIES" "$SECONDS_BUDGET" >> "$OUT" 2>&1
echo "wrote $OUT ($(stat -c %s "$OUT") bytes)"
