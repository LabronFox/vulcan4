#!/usr/bin/env bash
# VULCAN 4 NIGHTWATCH -- the captain's overnight loop, built brick by brick (2026-10-02).
#
# WHAT THE CAPTAIN ASKED FOR, verbatim: "when Sanji finishes the goal. U get a notify and wake.
# Both of them. But based on what Sanji did. And then before ur finished u are forced to give
# Sanji a new goal based on the context and fixes needed using tofu to write it exactly how we
# did it this entire time ok? And make this into a script. This way he can work all night."
#
# THE SHAPE, and why it is not a cron:
#   1. DETECT  -- watch the opencode goal plugin's OWN state file. It already records stopped,
#                 stopReason and blockedReason for every goal, so nothing has to be injected into
#                 Sanji's session to know he finished. No plugin, no patch to his tools.
#   2. WAKE    -- on a goal end, create a kanban task assigned to `default` (me) carrying the
#                 review brief. kanban's native notify+wake both DMs me AND starts my gateway with
#                 fresh board context. This is the same mechanism the captain already trusts for
#                 every other agent, which is why he asked for it.
#   3. REVIEW  -- I do that in the woken session: read his diff, run the GATE against the product,
#                 check for a faked picture, then send him a new /goal over Tofu keystrokes.
#                 The brief is written into the task body, so step 3 cannot be skipped.
#   4. REARM   -- this script keeps watching, so the next goal end wakes me again. There is always
#                 exactly one armed tripwire because each end CREATES the next one.
#
# IT IS NOT A POLLING CRON. The loop above is a plain script, but the only thing it does while
# Sanji works is sleep and stat a file. All the intelligence lives in the woken Caine session.
#
# NOTHING IN THE CODE TREE IS EDITED HERE. Caine supervises, Sanji cooks. That is the whole
# division of labour and this script must never cross it.

set -u
PROJ=/home/or/vulcan4
AUTO="$PROJ/.auto"
STATE_DIR="$PROJ/.opencode/goals/state.json.sessions"
GATE="$AUTO/queue/51-w109-verify.sh"
LOG="$AUTO/nightwatch.log"
SEEN="$AUTO/nightwatch.seen"
INTERVAL=${NIGHTWATCH_INTERVAL:-90}
GIVEUP_H=${NIGHTWATCH_GIVEUP_HOURS:-14}

mkdir -p "$AUTO"
log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$LOG"; }

# The latest state file the goal plugin writes. Sorted by mtime, not by name, because the session
# directory name is a hash and means nothing.
latest_state() {
  ls -t "$STATE_DIR"/*/state.json 2>/dev/null | head -1
}

# Goal end = this goalId has stopped for a reason we did not choose. `stopped` is the plugin's own
# field, so this is the plugin's verdict, not our guess.
goal_ended() {
  python3 - "$1" <<'PY' 2>/dev/null
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    print("NONE"); raise SystemExit
goals = d.get("goals") or []
if not goals:
    print("NONE"); raise SystemExit
g = max(goals, key=lambda x: x.get("startedAt") or 0)
gid = g.get("goalId", "")
stopped = bool(g.get("stopped"))
reason = g.get("stopReason") or ""
if not gid:
    print("NONE")
elif not stopped:
    print("RUNNING")
else:
    print(f"{gid}|{reason or 'unknown'}")
PY
}

log "nightwatch armed. interval=${INTERVAL}s giveup=${GIVEUP_H}h gate=$GATE"
start=$(date +%s)

while :; do
  sleep "$INTERVAL"
  st=$(latest_state)

  if [ -z "$st" ]; then
    log "no goal state file yet"
    continue
  fi

  ended=$(goal_ended "$st")
  case "$ended" in
    RUNNING) continue ;;
    NONE)   continue ;;
  esac

  gid="${ended%%|*}"
  reason="${ended#*|}"

  # Already handled this exact goal end? Then this is a poll, not an event.
  if [ -f "$SEEN" ] && [ "$(cat "$SEEN")" = "$gid" ]; then
    continue
  fi
  printf '%s' "$gid" > "$SEEN"

  log "GOAL ENDED: $gid reason=$reason -- waking Caine via kanban"

  # ---- The facts that go into the wake, so the woken session starts informed instead of
  # ---- blind. Every one of these is a MEASUREMENT, never a summary.
  head=$(git -C "$PROJ" log --oneline -1 2>/dev/null)
  commits=$(git -C "$PROJ" log --oneline -8 2>/dev/null)
  dirty=$(git -C "$PROJ" status --short 2>/dev/null | head -10)

  gate_out="(gate not run yet)"
  if [ -x "$GATE" ]; then
    gate_out=$("$GATE" 2>&1 | tail -8)
  fi

  # The captain asked twice for this: prove the thing still boots, and look for footage. A boot
  # that stopped crashing is progress even when the picture is still black, and it is the number
  # that tells us whether to send him a fix or a "keep going".
  boot_proof="(not run)"
  bootlog=$(ls -t /mnt/ssd/vulcan4-build/run/boot_gate_w108.log 2>/dev/null | head -1)
  if [ -n "$bootlog" ]; then
    boot_proof=$(grep -m1 'BOOT REPORT' "$bootlog" 2>/dev/null | cut -c1-400)
    [ -z "$boot_proof" ] && boot_proof="NO BOOT REPORT -- crashed. Tail: $(tail -3 "$bootlog" | tr '\n' ' ')"
  fi

  results=$(ls -t "$PROJ"/docs/W1*-RESULTS.md 2>/dev/null | head -1)

  # ---- The wake. One kanban task, assigned to me, body = the whole review procedure so step 3
  # ---- cannot be forgotten. notify+wake is the native behaviour the captain asked for.
  body=$(cat <<EOF
SANJI'S VULCAN 4 GOAL ENDED. Your turn: review, then give him a new goal.

GOAL ID: $gid
STOP REASON: $reason

WHAT HE DID (commits, newest first):
$commits

TREE STATE (empty means clean):
$dirty

HEAD: $head

GATE RESULT -- the product, not the commit:
$gate_out

BOOT PROOF (does it still boot, and what is the picture):
$boot_proof

HIS RESULTS DOC: ${results:-none written}

DO THIS, IN THIS ORDER, IN THIS ONE TURN:
1. Read his commits and his results doc. Did he move frames_presented and gs_packets, or did he
   only talk? Judge the numbers.
2. HONESTY CHECK, law 2: no faking. Did he hardcode a framebuffer address, paint a test pattern,
   or read memory the game did not write to make the window non-black? That is WORSE than black --
   the captain cannot tell which he is looking at. Call it out and make him revert it.
3. Boot it YOURSELF and LOOK. Do not trust his log alone:
     DISPLAY=:0 bash $PROJ/tools/harness/run_gt4_desktop.sh
   Then: is the window black, glitched, or showing something? Report exactly that, in one line.
   If there is any picture at all, say so loudly and save a screenshot to
   /home/or/vulcan4/docs/evidence/.
4. WRITE HIM A NEW GOAL, based on what you just saw. It must name the wall, the numbers before and
   after, the dead ends already tried, and the ONE thing you want moved. Do not send him a vague
   "keep going". If he is blocked, the new goal is the unblock.
5. SEND IT over Tofu keystrokes, exactly as we have all night:
     ssh orgolan@10.100.102.50 "echo BASE64 | base64 -d | pbcopy"
   then SEPARATE osascript calls, with delays between them:
     a) activate Terminal, key code 53 (Escape), then keystroke "v" using command down
     b) activate Terminal, key code 36 (Return)
   One combined AppleScript call drops characters and only the tail lands.
   HARD RULE: the goal objective must contain NO literal "--" except in the three real flags
   --max-turns, --max-minutes, --max-tokens. The goal plugin parses any other "--" as a flag and
   rejects the entire goal. That mistake already cost one run.
   VERIFY IT LANDED: read
     /home/or/vulcan4/.opencode/goals/state.json.sessions/*/state.json
   and confirm a goal with stopped=False. If not landed, retry once. Never claim success without
   that read.
6. Reply to the captain in ONE line: what changed, the number, what you sent him.

DO NOT edit the code tree. He cooks, you supervise.
EOF
)

  title="VULCAN 4 W$(date +%H%M): review Sanji, send next goal"
  out=$(hermes kanban create "$title" --assignee default --body "$body" --json 2>&1)
  tid=$(printf '%s' "$out" | python3 -c "import sys,json;print(json.load(sys.stdin).get('id',''))" 2>/dev/null || true)

  if [ -n "$tid" ]; then
    # CRITICAL, LEARNED THE HARD WAY: a task created from the CLI gets NO subscription. Without
    # this line the task exists, the board records it, and nobody is ever told -- the wake is
    # silent and the night looks like it is progressing while nobody is supervising it. Verified:
    # "hermes kanban notify-list" printed "(no subscriptions)" on a freshly created task.
    # notify+wake = the passive Discord message AND my gateway being started with fresh board
    # context, which is what actually injects the review brief.
    hermes kanban notify-subscribe "$tid" \
      --platform discord --chat-id 1479969115119685804 --chat-type channel \
      --notifier-profile default --delivery-mode notify+wake >>"$LOG" 2>&1 || \
      log "!! FAILED to subscribe $tid -- Caine will NOT be woken"
    log "woke Caine: kanban $tid (goal $gid)"
  else
    log "KANBAN CREATE FAILED for goal $gid: $out"
  fi

  now=$(date +%s)
  if [ $(( (now - start) / 3600 )) -ge "$GIVEUP_H" ]; then
    log "reached ${GIVEUP_H}h. standing down."
    break
  fi
done
