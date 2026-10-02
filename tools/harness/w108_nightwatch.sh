#!/bin/bash
# W108 NIGHTWATCH -- Caine's supervision loop. Runs while the captain sleeps.
#
# The captain's instruction, verbatim: "make sanji work on this overnight. and also i want u to turn
# on the vulcan driver. everytime sanji finishes it wakes u up to check what he did and review his
# code and work. if hes stuck give him a new goal. ALLL Night Ok?"
#
# So: ONE controller per lane. The dish driver feeds Sanji; this only WATCHES and PAGES me. It
# never dispatches into his TUI and never edits the tree, because two things typing into the same
# session is how you get a corrupted goal. When he finishes or parks, this fires a Hermes turn so
# I review his diff and give him the next goal.
#
# Deliberately dumb: it greps for two terminal states and nothing else. A smart supervisor that
# second-guesses the chef is worse than no supervisor.
set -u
PROJ=/home/or/vulcan4
AUTO="$PROJ/.auto"
LOG="$AUTO/nightwatch.log"
STATE="$AUTO/nightwatch.state"
INTERVAL=${NIGHTWATCH_INTERVAL:-120}
GONE=${NIGHTWATCH_GIVEUP_HOURS:-12}

log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOG"; }

mkdir -p "$AUTO"
log "nightwatch up. interval=${INTERVAL}s giveup=${GONE}h"

last_fingerprint=""
start_epoch=$(date +%s)

while true; do
  sleep "$INTERVAL"

  # 1. Is a driver dish actually in flight? If not, there is nothing to supervise.
  running=$(pgrep -fc 'vulcan4_driver.sh' 2>/dev/null || echo 0)

  # 2. Two ways a run can END: the driver parks (3 failures or empty queue), or the driver
  #    itself stops. Both mean Sanji is done or stuck, and both mean I should look.
  parked="no"
  [ -f "$AUTO/PARKED" ] && parked="yes"

  driver_alive="no"
  [ "$running" -gt 1 ] && driver_alive="yes"   # 1 == this script's own pgrep line

  # 3. Fingerprint the state so we page ONCE per finished run, not once per poll.
  fingerprint="${parked}|${driver_alive}|$(cat "$AUTO/session" 2>/dev/null)"
  fingerprint="${fingerprint}|$(ls -1 "$AUTO/queue"/*.txt 2>/dev/null | wc -l)"

  if [ "$fingerprint" != "$last_fingerprint" ] && [ -n "$last_fingerprint" ]; then
    log "STATE CHANGED: parked=$parked driver_alive=$driver_alive queued=$(ls -1 "$AUTO/queue"/*.txt 2>/dev/null | wc -l)"
    printf '%s' "$fingerprint" > "$STATE"

    # Wake Caine. He reads Sanji's diff, reviews it, and issues the next goal.
    if command -v hermes >/dev/null 2>&1; then
      hermes chat --send -q "NIGHTWATCH: Sanji's W108 overnight run has ENDED or STUCK.
        parked=$parked  driver_alive=$driver_alive  queued_dishes=$(ls -1 "$AUTO/queue"/*.txt 2>/dev/null | wc -l)
        HEAD=$(cd $PROJ && git log --oneline -1)
        Review his commits and his work, check the gate result, and if he is stuck or wrong,
        write a NEW goal for him. Do NOT touch the tree yourself tonight -- he is cooking, you are
        supervising. Wake the captain only if there is a real picture or a real dead end." \
        -m discord >/dev/null 2>&1 || log "hermes wake failed"
      log "paged Caine."
    else
      log "hermes not on PATH -- cannot wake Caine."
    fi
  fi

  [ -z "$last_fingerprint" ] && last_fingerprint="$fingerprint"

  # 4. Give up after a full night, loudly, in the log. Better than a watcher that runs forever
  #    after the captain stopped caring.
  now=$(date +%s)
  if [ $(( (now - start_epoch) / 3600 )) -ge "$GONE" ]; then
    log "nightwatch reached ${GONE}h. standing down."
    break
  fi
done
