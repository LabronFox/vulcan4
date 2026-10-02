# GOAL: W107 — MAKE THE WINDOW SHOW THE GAME'S ACTUAL PICTURE

## WHY THIS DISH IS DIFFERENT FROM EVERY OTHER ONE

For four days the captain has been told numbers. He has never seen a picture. He said it
plainly tonight: *"the entire point were doing this entire recomp is to get a game. not some
coding nonsense."* He double-clicks an icon on his desktop and looks at a window. If that
window is black, this project has delivered nothing, no matter what any log says.

**So the success gate for W107 is not a number in a log. It is PIXELS ON SCREEN.**

## WHAT THE WINDOW ACTUALLY DISPLAYS -- ALREADY BUILT, DO NOT REBUILD IT

`UploadFrame()` in ps2xRuntime/src/lib/ps2_runtime.cpp (~line 377) is the real display
path, and it is honest. Every vblank it calls `gs().latchHostPresentationFrame()` then
`copyLatchedHostPresentationFrame()` and uploads the GS's own framebuffer to the raylib
texture. Nothing is faked, nothing is synthesised. When it cannot get a frame it draws
MAGENTA, deliberately, so "no frame" can never be mistaken for a black frame.

**The picture is a three-link chain, and link 1 is broken:**

  1. THE GUEST SETS FBP -- tells the GS which address in RDRAM holds the picture.
  2. The GS draws primitives into that address.
  3. UploadFrame reads that address and blits it.

Link 2 works. Link 3 works. **Link 1 never happens.**

## THE MEASUREMENT, FROM THE CAPTAIN'S OWN RUN (boot_desk203501.log, 60s, 2.2 MB)

    $ grep -oE 'ctx0fbp=[0-9]+|ctx1fbp=[0-9]+' boot_desk203501.log | sort | uniq -c
      112 ctx1fbp=0
       94 ctx0fbp=0
       18 ctx0fbp=160

**94 of 112 packets draw into address ZERO.** 18 have 0x160 = 352, which is 22 words --
that is not a framebuffer either, it is a pointer to something else. Not one packet in the
whole run names a real framebuffer address.

Also, and this is the lead: the ONE substantial packet in the log is

    VULCAN4 FRAME source=guest n=3 bytes=114688 [gs:gif] ... nloop=0

**114,688 bytes with nloop=0.** On a real GS that packet is a register-walk that sets up the
display: framebuffer width, ZBuffer, clip coords, and roughly sixteen GS registers including
FBP. nloop=0 means our decoder read ZERO registers out of the one packet that contains them.
**That single decode failure is the most likely reason FBP is never set, and it is the first
thing to investigate.**

Also worth knowing, because it invalidates a number the captain was shown: `guestFrameCounter`
increments once per **GS GIF packet** (ps2xRuntime/src/lib/gs/gs_frontend.cpp:666, inside
`processGIFPacket`), NOT per display frame. It was displayed to him as "FPS". A 96-byte
packet is a chunk of a GIF stream, and GT4 sends many per frame. **Any FPS figure derived
from it is packets per second and must be relabelled or replaced.** The speed figure
(0.93x PS2, EE cycles per wall second against kEeClockHz) is unaffected and remains true.

## WHAT TO DO, IN ORDER

1. **The 114,688-byte packet.** Find why a GIF packet with that much data decodes to nloop=0.
   Compare its header/tag words against the GS packet format. Check whether it is a
   `nloop`-bearing path this decoder skips, whether it uses the alternate tag/DWORD mode
   (bit 17 of the tag = 1 selects the 2-DWORD format, and a decoder that only walks the
   1-DWORD form will read nloop as 0), or whether it is a DMAtag-linked packet that must be
   resolved from a DMA chain before its registers exist. Report which of the three it is.

2. **The 0x160 value.** 18 packets use fbp=0x160. Establish what that address actually holds
   in the guest's RDRAM at that moment. If it is a pointer, follow it -- the real framebuffer
   may be one indirection away and already correct.

3. **Only then** consider anything else. Do NOT invent a framebuffer address, do NOT point
   the GS at DEFAULT_FB_ADDR to make the window show something, and do NOT draw a test
   pattern. Law 2: a black screen with an honest limitation beats a plausible lie, and a
   window showing a fake picture is the worst possible outcome -- it would convince the
   captain the game renders when it does not.

## SUCCESS, EXACTLY, AND IT IS NOT A LOG LINE

The captain double-clicks the VULCAN 4 icon on his desktop and GT4's own startup image
appears in the window. Anything less is not success, however good the numbers are.

If FBP is genuinely never set by the guest and you have PROVEN that, that is a successful
dish: report it with the packet decode, name what the guest SHOULD have written and where,
and hand back the next single measurement. Do not go hunting past the wall.

## RULES

The captain judges the product, not the log. No fake numbers. "3.6% of PS2" and "28x
slower" are withdrawn; measured speed is 0.93x PS2. Never divide function ENTRIES by a
clock rate -- an entry is not a cycle. Do not touch the harness window title; it is
currently showing a number that is really packets/sec and will be relabelled separately.
Commit as Or Golan <or024662@gmail.com>, no push. -j4, nice -n 10, ionice -c3 -- a live
Minecraft server shares this box. Keep the suite at 493/493. Report STUCK / TRIED /
BLOCKED BY / NEED the first time you are genuinely stuck, with numbers attached.