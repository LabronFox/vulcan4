# THE CAPTAIN'S TASTE TEST

*Born 2026-09-30, mid-session, straight out of the captain's own playing: "good thing we know what
to test now :3"*

**Why this file exists.** Every other gate in this project is measurable by a machine: pixels,
counts, registers, tests. Those prove the machine works. They cannot prove the game **feels like
GT4**. The captain judges the product, and the product is a feeling — so his checks live here, in his
words, and they are the last gate on every subsystem.

**How it is used:** a dish claims a subsystem is done → the captain drives it → if the sensation
below is missing, the subsystem is not done, no matter what the log says.

| # | What the captain does | The sensation that must be there | What it proves if missing |
|---|---|---|---|
| 1 | R92CP on the Mulsanne, flat out | *"at these speeds i can feel every imperfection on the road"* — ripples reach the wheel, the car goes light and wanders | road mesh or tyre-load model is smoothed/stubbed |
| 2 | 190 E Evo II out of a slow corner, on the throttle | it steps out and comes back; *"im just powersliding"* is a choice, not a surprise | differential / tyre slip faked, or hidden traction control |
| 3 | IA-15, hanging back behind the pace car | it **speeds up when you push, slows when you coast** — it is waiting for you | pace-car logic is a scripted speed, not the adaptive behaviour |
| 4 | Complete any lap (G6.2) | the cue lands **the instant the time is committed** — not when the number redraws | the hook is on the symptom, not the cause |
| 5 | Fail one lesson past the threshold (G6.3) | the skip is offered, and the game **never** awards a medal for it | the skip is counterfeiting progress |
| 6 | Menus, one hand on the pad | cursor and page changes land on the same frame as the input — no hitch, no emulator stutter | input path is polled late or buffered |
| 7 | 375 km/h, engine note | the sound follows **load**, not speed alone; it barks on lift-off | audio is a looping sample rather than the disc's own engine data |

**Rules for this list:**
- It is the captain's. He adds, edits and deletes — nobody else curates it.
- Never soften a check to make a dish pass. A failing sensation is a finding, not an inconvenience.
- Only in-game, felt things belong here. Anything a log can measure goes in the normal gates.
