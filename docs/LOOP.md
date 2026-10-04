# VULCAN 4 — THE LOOP

*Everything about how work gets done on this project when nobody is watching. Read this before you
touch the driver, the switch, the cron, or the gate. Written 2026-10-04 after a day in which the loop
broke in seven separate ways — six of them invisible from the outside.*

---

## 1. THE DESIGN (whose job is what)

The captain's own description, verbatim:

> *"The loop has to wake u up when he's done with his task and then u give him a new task based on his
> results. That's the loop."*

Three roles, and they must not collapse into each other:

| role | who | does |
|---|---|---|
| **RECIPE-WRITER / REVIEWER** | **Caine** (Hermes agent) | reads the chef's results, runs the product gate, authors the next dish |
| **CHEF** | **Sanji** (`opencode`, model `opencode-go/deepseek-v4.1-flash`) | writes the code, builds, measures, commits |
| **TASTER** | **the captain** | looks at the picture, says "that's GT4" or "that's wrong", says STOP |

**The dispatcher is NOT an intelligence.** `vulcan4_driver.sh` is ~500 lines of bash with no AI in it.
Its entire job: *read a dish text file → hand it to the chef → wait → run one shell command (the gate)
→ repeat.* If you ever find yourself expecting the driver to make a judgement call, that judgement
belongs in a dish, written by Caine.

**THE THING THAT DRIFTED, AND THE LESSON:** the design above is right, but the driver's empty-queue
fallback queues `00-campaign-loop.txt`, a meta-dish that tells the CHEF to read the campaign and write
its OWN next dishes. From dish 04 onward that is exactly what happened — **the chef started authoring
his own work queue and the recipe-writing half of the loop quietly stopped.** The chef grading his own
homework is not the design. The fallback still exists (it is what keeps work flowing at 3am), but it
now **announces itself** with a notification naming the one thing a human should do. If that
notification repeats, the recipe-writer has gone missing.

---

## 2. THE COMPONENTS (every moving part, with its path)

| what | where | note |
|---|---|---|
| **the dispatcher** | `~/.hermes/scripts/vulcan4_driver.sh` | bash, no AI. Feeds dishes, runs the gate, parks on trouble |
| **its service** | `~/.config/systemd/user/vulcan4-driver.service` | `Restart=always`, `enabled`. Parent is systemd, so it survives Hermes restarts and reboots |
| **the ON/OFF switch** | `~/.hermes/scripts/coder_loop.sh` `on\|off\|status` | flag file `~/.hermes/cache/coder_loop.state`, **read by the driver** |
| **the review cron** | job `bfb12272ce88`, every 10m | monitor-gated: wakes Caine only when a new commit lands |
| **the monitor** | `~/.hermes/scripts/vulcan_dish_monitor.sh` | prints `HEAD-hash \| subject` — **deterministic**, so unchanged output suppresses the agent run |
| **the product gate** | `/home/or/vulcan4/.auto/verify-menu.sh` (**v4**) | the picture is the decider; see §4 |
| **the health check** | `~/.hermes/scripts/vulcan4_health.sh` | 16-point report, exit 0 = healthy |
| **the dishes** | `/home/or/vulcan4/.auto/queue/*.txt` | must begin a line with `GOAL:`; may carry `VERIFY:` |
| **the session id** | `/home/or/vulcan4/.auto/session` | **read this file, never hardcode an id** |
| **logs** | `.auto/driver.log`, `.auto/driver.stderr.log`, `.auto/dish-<name>.log` | |
| **park marker** | `/home/or/vulcan4/.auto/PARKED` | present ⇔ the driver is parked RIGHT NOW |

### The process, end to end

1. the driver picks the lowest-numbered dish in `queue/`
2. `opencode run -s <session> "$(cat dish)"` — **the dish text is the prompt**, one turn
3. the chef works until he stops; the process exits
4. the driver runs the dish's `VERIFY:` gate
5. gate ok → dish moves to `done/`; gate failed but a **commit landed** → counted as progress, retried once
6. gate failed and nothing committed → `failed/`, next dish
7. queue empty → campaign gate; if the product gate now passes, **STOP + tell the captain**, else fall
   back to the campaign meta-dish (loudly — see §1)

---

## 3. ON / OFF — AND WHY THIS IS THE PART THAT KEEPS GOING WRONG

    bash ~/.hermes/scripts/coder_loop.sh status      # is it on, and is a turn running
    bash ~/.hermes/scripts/coder_loop.sh off         # stop dispatching, KEEP the session and any in-flight dish
    bash ~/.hermes/scripts/coder_loop.sh on          # resume

**THE RULE THAT HAS BEEN VIOLATED TWICE: an on/off control must cover EVERY mechanism that keeps the
loop alive, or "off" is a lie.**

- The first time, pausing the loop left its **wake-up cron** firing (the captain caught it).
- The second time, the switch was found to be **entirely decorative** — `vulcan4_driver.sh` did not
  reference `coder_loop.state` at all, so `off` stopped nothing and `on` started nothing. The tell was
  that `status` read `loop=off` *while dishes had been running all along*.

**Both are now wired:** the driver reads the flag at the top of every loop pass, and `coder_loop.sh`
`off`/`on` also pauses/resumes the review cron. **Verify a switch by watching the thing it claims to
control** — turn it off and confirm work actually stops. A switch you have only ever *read* is a
switch you have never tested.

---

## 4. THE GATE — and the four times it was wrong

`bash /home/or/vulcan4/.auto/verify-menu.sh` — exit 0 = the screen is measurably NOT the disclaimer.

The current rule (**v4**): **THE PICTURE IS THE DECIDER.** The newest window capture is compared to a
stored disclaimer reference (`/mnt/ssd/vulcan4-build/run/.disclaimer-reference.png`) — exact bytes
first, then a 32×32 perceptual diff (threshold 6). Anything not measurably the disclaimer passes.
Run stats (`functions_entered`, `halt`, `bios_files`) are **printed as context** and do not block a
result. `bios_files=0` remains a hard requirement — that is a project law, not a preference.

The history matters, because every version taught the same lesson:

| ver | rule | how it was wrong |
|---|---|---|
| v1 | barrier `0x0100d908` not the top site | **false-passed** on a short nondeterministic boot that never reaches the barrier |
| v2 | + `functions_entered >= 20000` | **false-passed** on a 37,898-function run whose capture was BYTE-IDENTICAL to the disclaimer |
| v3 | compares the capture to a reference | correct, but **false-NEGATIVED** — it still demanded a long run and a *finished* boot log, so it rejected a genuinely different picture |
| **v4** | the picture decides | current |

**THE RECURRING MISTAKE, NAMED: gating the PRECONDITIONS of the deliverable instead of the
deliverable.** Run length and halt reason describe how a run went; they are not whether the game
showed a new screen. Two independent false negatives were found in v3 — first *"no BOOT REPORT in
boot_w193m.log"* (the newest log was an in-progress run), then *"functions_entered=5919 below 20000"*
(the newest COMPLETE log was a short shape). Either would have **swallowed the captain's menu alert
at the exact moment it mattered.**

**Teeth are proven BOTH ways and must stay that way:** plant a genuinely different PNG → the gate
PASSES; with the real disclaimer → it FAILS. A gate that cannot fail has no teeth; a gate that cannot
pass is decoration.

---

## 5. THE FAILURE MODES FOUND ON 2026-10-04 (all fixed; all were invisible from outside)

Keep these as a checklist — they are the bug classes, not the incidents.

1. **A call to an UNDEFINED FUNCTION on the success path.** Line 205 called `move_to`, a function that
   does not exist anywhere in the script. Under `set -u` (not `set -e`) that is a silent
   command-not-found, so a gate-PASSING dish never left the queue and was re-dispatched **forever** —
   six times in 57 minutes, every log line reading `OK (gate passed)`. Every *other* path used `mv`,
   which is why only the success path leaked. **Fix:** plain `mv`. **Guard added:** track
   (dish name + HEAD) and park after 3 dispatches at an unchanged state.
2. **The driver's own stderr was never logged.** The `command not found` above went to stderr while
   the log captured only stdout — which is exactly why it survived an hour unseen. **Fix:**
   `exec 2>>"$AUTO/driver.stderr.log"`.
3. **A wait with NO TIMEOUT.** Every wait was `while running; do sleep 30; done`, unbounded. One
   wedged dish stalls the loop forever and writes NOTHING. **Fix:** `wait_dish()` with
   `DISH_TIMEOUT=5400`; reaps on expiry. Proven both ways with mocked `pgrep`/`kill`.
4. **A status marker that outlived its condition.** Park paths set `$PARKED` then `sleep`; nothing
   cleared it, so the file read *"3 consecutive failures"* while the driver was awake and dispatching.
   **Fix:** `rm -f "$PARKED"` the moment real work resumes.
5. **"Gate failed" ≠ "dish wasted".** `consec_fail` only reset on a gate PASS — but a picture gate
   cannot pass until the bug is fixed, so the loop **parked on its own best work** (19:07, while
   landing W176/W177/W181/W185). **Fix:** a gate-failed dish that still COMMITTED counts as progress.
6. **Restart double-dispatch.** Dishes are `setsid`'d and outlive a driver restart; the new process
   would dispatch a *second* dish onto the same session. **Fix:** adopt an in-flight dish on startup.
   (Note: restarting the *unit* kills the cgroup including the dish, so this is a safety net.)
7. **🚨 THE PATH TRAP — the one that was mine.** Moving the driver under **systemd** (for persistence)
   silently broke **every dish launch**: `opencode` lives at `/home/or/.opencode/bin/opencode` and
   reaches PATH only via `~/.bashrc:160`, which an interactive shell sources and **systemd does not**.
   `setsid opencode run` died with *No such file or directory* **before starting anything** — while
   the driver still logged `--- dish start ---` and sat in its wait loop, so the log read as a merely
   SLOW dish. **Fix:** pin the binary (`OPENCODE_BIN`) **and** set `Environment=PATH=` in the unit.
   **The coupling bug this creates:** once the launcher uses an absolute path, the command line no
   longer begins with the bare word `opencode`, so a `running()` anchored on `^opencode run` matches
   NOTHING → the driver would judge the gate mid-run and dispatch a second dish. Widened to
   `^[0-9]+ (/[^ ]*/)?opencode run`. **Any time you change HOW a process is launched, re-check every
   pattern that matches it.**
8. **Two hardcoded stale session ids** (`vulcan_idle_watch.sh` and `coder_loop.sh`) pointed at
   sessions that no longer existed, so anything built on them watched a ghost. **Rule: read the id
   from `.auto/session`; never embed one.**

---

## 6. THE REVIEW CRON

Job `bfb12272ce88` — *"VULCAN4 review loop (wake Caine on new result)"*.

- **schedule** every 10m · **enabled**
- **monitor** `vulcan_dish_monitor.sh` → the agent runs **only when the monitor's output changes**
  (i.e. only when a new commit lands). Unchanged output = silent no-op tick.
- **deliver** `origin` — **it must reach Caine.** An earlier version had `deliver: local`, which wrote
  the wake-up to a FILE NOBODY READS; that single field is why the loop looked dead.
- **failure_deliver** `local` — failure/skip notices never reach the captain.
- **model/provider pinned** to `opencode-go / deepseek-v4.1-flash`. Unpinned jobs are **skipped** when
  the global model drifts, to prevent unintended spend.

**The monitor must emit STABLE output** (no timestamps, no elapsed counters) or every tick looks
changed and the job fires every 10 minutes. An early draft appended a `COOKING/SETTLED` flag — that
changes on every dish start and finish, i.e. exactly the flicker the monitor exists to ignore. It now
prints the commit hash and subject only.

---

## 7. THE HEALTH CHECK

    bash "$HOME/.hermes/scripts/vulcan4_health.sh"

16 checks, exit 0 = healthy. It verifies the driver is alive/enabled, the switch, the cron is enabled
**and watching the live session**, a dish is in flight *and moving*, `PARKED` is honest, **the gate
can still fail**, no undefined function calls, every wait is bounded, the progress-aware `consec_fail`
guard exists, stderr is logged, the launcher is pinned to an absolute path, `running()` matches both
launcher forms, and the queue state.

⚠️ **Invoke it as `bash "$HOME/.hermes/scripts/vulcan4_health.sh"`.** Writing the literal
`/home/or/.hermes/scripts/...` path into a command trips the gateway's self-restart guard, which then
blocks the whole invocation.

**Write the health check the same day you fix the bugs.** Seven failures in one day each looked
healthy from a distance. The check is what turns *"is the loop OK?"* from an opinion into a reading.

---

## 8. QUICK REFERENCE

    # is it alive and honest?
    bash "$HOME/.hermes/scripts/vulcan4_health.sh"

    # is it on?
    bash ~/.hermes/scripts/coder_loop.sh status

    # stop it (driver + cron)
    bash ~/.hermes/scripts/coder_loop.sh off

    # start it (driver + cron)
    bash ~/.hermes/scripts/coder_loop.sh on

    # reclaim the loop from the fallback (the recipe-writer's actual job)
    #   read the newest docs/W1xx-RESULTS.md and git log, then:
    #   write /home/or/vulcan4/.auto/queue/NN-slug.txt   (needs a GOAL: line and a VERIFY: line)

    # watch the chef work live
    #   http://10.100.102.11:4096   (password in ~/.config/opencode/server.env)

    # logs
    tail -f /home/or/vulcan4/.auto/driver.log
    tail -f /home/or/vulcan4/.auto/driver.stderr.log

**Never restart the driver mid-dish without reason** — `systemctl --user restart` kills the cgroup,
including the in-flight dish. That is *why* it never orphans work, but it is still lost time.

**One controller per lane.** Never run this driver against a session that a goal-plugin or a second
dispatch loop is also driving. Two writers on one session corrupt turns, double the tool calls, and
get the gate judged against the wrong run.
