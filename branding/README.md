# VULCAN branding

Everything in this folder is generated. Do not hand-edit a PNG or an SVG — edit
`tools/` and re-run the build.

    cd /home/or/vulcan4/branding/tools
    python3 build_all.py     # writes svg/ png/ contact/ docs/ and manifest.json
    python3 verify.py        # machine checks; exits 1 on any failure

Requires: `rsvg-convert` (librsvg) for rasterising, `python3` + `fontTools` + `Pillow`
for the outline extraction and the checks. `inkscape`/ImageMagick used only for ad-hoc QA
crops, not by the build.

## The identity in one paragraph

A wordmark and three emblem directions. The wordmark is VULCAN set in Impact condensed a
further 0.90, sheared 12 degrees, tracked 0.115 cap — deliberately generous where the
reference we drew spirit from is tightly set. **One letter is bespoke: the V.** Its counter
walls are drawn dead straight, the counter is driven down to 11.4% of the cap and its point
is cut off flat (a chisel floor), and its base is narrowed from Impact's 62.4 units to 50.
Every emblem is that same drawn V, so the mark cannot drift away from the logotype.

## What is in here

| Path | What it is |
|---|---|
| `svg/vulcan-wordmark.svg` | the logotype, outlined paths, no live text, ink |
| `svg/vulcan-wordmark-inverse.svg` | same, white (for dark grounds) |
| `svg/vulcan-mark-1_monolith.svg` | emblem 01, the letter V alone |
| `svg/vulcan-mark-2_wingpair.svg` | emblem 02, the V's strokes drawn apart — the delta is the gap |
| `svg/vulcan-mark-3_grounded.svg` | emblem 03, the V on a copper rule (the disc) |
| `svg/vulcan-mark-*-mono.svg` | monochrome version of each emblem (accent folded into ink) |
| `svg/vulcan-lockup-h-*.svg` | horizontal lockup per direction, tagline on its own layer |
| `svg/vulcan-lockup-v-*.svg` | stacked lockup per direction |
| `svg/vulcan-lockup-horizontal-plain.svg` | recommended lockup, no tagline |
| `svg/vulcan-lockup-stacked{,-mono,-inverse}.svg` | recommended lockup, mono and inverse |
| `svg/vulcan-favicon.svg` | emblem 01 squared up for a favicon |
| `png/*-1024/512/256.png` | raster exports, RGBA, transparent |
| `png/vulcan-favicon-{64,32}.png` | favicons |
| `png/*-2048.png` | lockups at 2048 wide |
| `contact/vulcan-contact-sheet.png` | **the picture that gets judged** — three directions, each proved at 32/24/16 px in ink on paper, plus the construction proof for the bespoke V |
| `docs/vulcan-colour-card.svg` | palette chips |
| `manifest.json` | every written file with its byte count, emitted by the build |

## Palette

| Role | Value |
|---|---|
| Ink (primary) | `#0C0C0D` |
| White (primary, inverse) | `#FFFFFF` |
| Paper (warm ground) | `#F2F0EB` |
| Copper — the single accent | `#C4622D` |

Copper is used exactly once in the whole system: the rule under direction 03. There is no
gradient, no glow, no second accent, and nothing borrowed from the reference's red-plus-
violet-blue pairing.

## The three directions, one line each

1. **MONOLITH** — the logotype's V, scaled up: nothing added, nothing taken away. The emblem
   and the first letter are literally the same drawn object.
2. **WINGPAIR** — the V's two strokes drawn apart; the gap opens from 6 units at the foot to
   26 at the cap line, so the Vulkan delta is the negative space, not an added shape.
3. **GROUNDED** — the V standing on a copper rule: the disc the software runs off.

## Rebuild gotchas

- The tagline is a separate `<g id="tagline" data-text="...">` layer in every lockup so the
  words can be swapped without re-cutting anything. `verify.py` fails the build if it is missing.
- Every SVG is outlined. `verify.py` fails on any `<text>` or `font-` attribute inside `svg/`.
- `logo.pairs()` must parse path commands, not zip numbers: `SVGPathPen` emits `H`/`V`
  linetos and a naive zip puts a phantom margin on the canvas. See the rationale doc.
- `marks.py` works in a box that spans **exactly** y = 0..100 (0 = cap line, 100 = baseline).
  A mark that stops short of the box drifts when `mark_svg()` scales it.
