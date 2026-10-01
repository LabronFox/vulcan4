"""build.py — the VULCAN identity builder.

Foundation typeface: Impact (heavy condensed grotesque). Chosen on measured grounds, not taste:
stem/cap = 0.260 — the only system face in the Heavy Condensed weight class (Helvetica Neue
Heavy Condensed, the closest published match to the GT wordmark, sits at ~0.26). Every letter
except A is Impact's drawn outline; A is ours.

Applied to every wordmark instance:
  * condense — extra horizontal squeeze (0.90) on top of Impact's own narrowness
  * shear    — the oblique lean, applied ONCE at group level via skewX, so the foundation
               glyphs and the hand-built A share exactly the same slant
  * tracking — generous, deliberately the opposite of the reference's tight setting
"""
from typekit import glyph_d, advance, glyph_bounds, metrics
from letterforms import delta_A
from geom import n

FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
FONT_LIGHT = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
FONT_MED = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"

INK = "#0C0C0D"
PAPER = "#F2F0EB"
COPPER = "#C4622D"


def wordmark(size=200.0, tracking=None, condense=0.90, leg_deg=13.0, bar=0.30,
             stem_scale=1.0, text="VULCAN"):
    m = metrics(FONT, size, condense)
    cap, stem = m["cap"], m["stem"] * stem_scale
    if tracking is None:
        tracking = cap * 0.115
    ab = glyph_bounds(FONT, "A", size, condense)
    a_ink, a_left = ab[2] - ab[0], ab[0]
    a_adv = advance(FONT, "A", size, condense)

    items, x, a_meta = [], 0.0, None
    for ch in text:
        if ch == "A":
            d, plateau = delta_A(cap, a_ink, stem, leg_deg=leg_deg, bar=bar,
                                 bar_th=stem * 0.86 if bar else None)
            items.append((ch, d, x + a_left))
            a_meta = dict(plateau=plateau, ink=a_ink, adv=a_adv, leg_deg=leg_deg, bar=bar)
            w = a_adv
        else:
            items.append((ch, glyph_d(FONT, ch, size, condense, dx=x), 0.0))
            w = advance(FONT, ch, size, condense)
        x += w + tracking
    total = x - tracking
    return dict(items=items, total=total, cap=cap, stem=stem, a=a_meta,
                tracking=tracking, condense=condense, size=size)


def wordmark_body(size=200.0, tracking=None, condense=0.90, leg_deg=13.0, bar=0.30,
                  shear=12.0, stem_scale=1.0, text="VULCAN", fill=INK, dx=0.0, dy=0.0,
                  extra=""):
    """SVG body for the wordmark. Returns (body, meta)."""
    wm = wordmark(size, tracking, condense, leg_deg, bar, stem_scale, text)
    parts = []
    for ch, d, ox in wm["items"]:
        if ox:
            parts.append(f'<path transform="translate({n(ox)} 0)" d="{d}"/>')
        else:
            parts.append(f'<path d="{d}"/>')
    body = (f'<g id="wordmark"{extra} transform="translate({n(dx)} {n(dy)}) skewX({n(-shear)})" '
            f'fill="{fill}" fill-rule="nonzero">' + "".join(parts) + "</g>")
    return body, wm


def tagline_body(text, size, tracking, x, y, fill, font=None, anchor="start"):
    fp = font or FONT_LIGHT
    parts, cx = [], 0.0
    for ch in text:
        if ch == " ":
            cx += advance(fp, "space", size) + tracking
            continue
        parts.append(f'<path transform="translate({n(cx)} 0)" d="{glyph_d(fp, ch, size, 1.0)}"/>')
        cx += advance(fp, ch, size) + tracking
    w = cx - tracking
    ox = x if anchor == "start" else (x - w / 2.0 if anchor == "middle" else x - w)
    return (f'<g id="tagline" data-text="{text}" transform="translate({n(ox)} {n(y)})" '
            f'fill="{fill}">{"".join(parts)}</g>', w)


def text_width(text, size, tracking, font=None):
    fp = font or FONT_LIGHT
    return sum(advance(fp, c, size) for c in text) + tracking * (len(text) - 1)
