"""logo.py — the VULCAN identity, in one place. Every deliverable is generated from here.

System decisions (all measured, see the rationale doc):
  foundation   Impact, condensed a further 0.90, sheared 12 deg, tracked 0.115 cap (generous —
               deliberately the opposite of the reference's tight setting)
  bespoke V    Impact's V redrawn with straight void walls, the void driven down to 11.4% of the
               cap and its point cut off FLAT (a chisel floor), base narrowed from 62.4 to 50
               units. One letter, two contour decisions, both visible only at wordmark scale.
  mark         three directions, all cut from the same V — see marks.py. In every one of them
               the emblem and the first letter are the same drawn object, so the mark cannot
               drift away from the wordmark the way a bolted-on symbol does.
  accent       copper #C4622D, used at most once per lockup (direction 3 only, on the rule).
"""
from typekit import glyph_d, advance, glyph_bounds, metrics
from custom import V, REF_CAP
from marks import DIRECTIONS as MARK_DIRECTIONS
from geom import n

FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
TAG_FONT = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"

INK = "#0C0C0D"
PAPER = "#F2F0EB"
COPPER = "#C4622D"

CONDENSE = 0.90
SHEAR = 12.0
TRACK_F = 0.115
REF_SIZE = 200.0
VOID_APEX = 18.0      # the counter's flat floor sits this far ABOVE the baseline (11.4% of cap)
VOID_FLAT = 4.5       # half the width of that floor: the point is cut off, not pointed
BASE_L, BASE_R = 28.0, 78.0

TAGLINE = "THE SAME DISC, NATIVELY COMPILED"


# --------------------------------------------------------------------------- geometry helpers
def ref_scale(cap):
    return cap / REF_CAP


def bespoke_v(cap, dx=0.0, dy=0.0):
    """The letter V, in glyph space, scaled to `cap`, with the oblique already compensated for
    at group level (paths stay upright; skewX is applied once by the layout).

    The one departure from Impact's V: the counter is driven straight down to a CHISEL — the
    point is cut off flat instead of converging to a point. Eight points, one flat floor. It
    is a wordmark-scale detail on purpose: at 32 px it is gone, exactly like the reference's
    straight R leg. A bespoke letterform is not supposed to survive a favicon; the icon is a
    different object."""
    s = ref_scale(cap)
    pts = [(V["x_r"], -REF_CAP), (BASE_R, 0), (BASE_L, 0), (V["x_l"], -REF_CAP),
           (V["inn_l"], -REF_CAP), (V["cx"] - VOID_FLAT, -VOID_APEX),
           (V["cx"] + VOID_FLAT, -VOID_APEX), (V["inn_r"], -REF_CAP)]
    d = "M" + " L".join("%.3f %.3f" % (x * s * CONDENSE + dx, y * s + dy) for x, y in pts) + " Z"
    return d


def wordmark_paths(cap):
    """Returns ([(path_d, ...)], total_advance) — every glyph a path, no live text."""
    size = REF_SIZE * ref_scale(cap)          # Impact cap = 0.7905 em
    tk = cap * TRACK_F
    parts, x = [], 0.0
    for ch in "VULCAN":
        if ch == "V":
            parts.append(bespoke_v(cap, dx=x))
        else:
            parts.append(glyph_d(FONT, ch, size, CONDENSE, dx=x))
        x += advance(FONT, ch, size, CONDENSE) + tk
    return parts, x - tk


def wordmark_svg(fill=INK, shear=True, cap=158.1, pad=0.06):
    parts, total = wordmark_paths(cap)
    pts = [p for d in parts for p in pairs(d)]
    sp = shear_pts(pts, shear)
    x0, y0, x1, y1 = bounds(sp)
    p = cap * pad
    ox, oy = p - x0, -y0 + p
    g = ("<g" + (f' transform="skewX({n(-SHEAR)})"' if shear else "") +
         f' fill="{fill}" fill-rule="nonzero">' +
         "".join(f'<path d="{d}"/>' for d in parts) + "</g>")
    body = f'<g transform="translate({n(ox)} {n(oy)})">{g}</g>'
    return body, (x1 - x0) + 2 * p, (y1 - y0) + 2 * p


def pairs(d):
    """Anchor points of a path, parsing its command letters.

    A flat zip of every number in the string is WRONG the moment the path contains an H or a V
    — a one-coordinate lineto, which is exactly what SVGPathPen emits for font outlines. The
    stray single number shifts every later pair by one, the bounds come out ~150 units wide,
    and the exported canvas carries a phantom margin. Measured cost of the naive version: a
    dead 334 px column down the left of vulcan-wordmark-1024.png. Parse the commands.
    """
    import re
    toks = re.findall(r"[A-Za-z]|-?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    nargs = {"M": 2, "L": 2, "H": 1, "V": 1, "Q": 4, "C": 6, "S": 4, "T": 2, "A": 7}
    pts, cmd, i = [], None, 0
    x = y = 0.0
    while i < len(toks):
        t = toks[i]
        if t.isalpha():
            cmd, i = t.upper(), i + 1
            continue
        k = nargs.get(cmd or "L", 2)
        vals = [float(v) for v in toks[i:i + k]]
        i += k
        if cmd == "H":
            x = vals[0]
        elif cmd == "V":
            y = vals[0]
        else:
            x, y = vals[-2], vals[-1]
        pts.append((x, y))
    return pts


def shear_pts(pts, on):
    from math import tan, radians
    if not on:
        return pts
    t = tan(radians(SHEAR))
    return [(x - y * t, y) for x, y in pts]


def bounds(pts):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


# --------------------------------------------------------------------------- mark rendering
def mark_colours(direction):
    """The mark's parts as (colour key, [polygons]). 'fg' is ink (or the fill in play) and
    'cu' is the single accent. One source of truth: marks.DIRECTIONS."""
    return MARK_DIRECTIONS[direction]()


def mark_paths(direction, mono=False):
    """Mark in a 100-tall box, bottom on y=0, left edge at x=0. Every mark's V is the letter V."""
    out = []
    for col, polys in mark_colours(direction):
        use = "fg" if (mono and col == "cu") else col
        for pl in polys:
            d = "M" + " L".join("%.3f %.3f" % (x, -y + 100.0) for x, y in pl) + " Z"
            out.append((use, d))
    return out


def mark_box(direction):
    polys = [p for _, pls in mark_colours(direction) for p in pls]
    xs = [x for pl in polys for x, _ in pl]
    ys = [y for pl in polys for _, y in pl]
    return min(xs), min(ys), max(xs), max(ys)


def mark_svg(direction, size=100.0, fill=INK, accent=COPPER, mono=False, pad=0.06, square=False):
    """Standalone emblem SVG body + canvas size. `size` = the mark's cap (height).
    square=True gives a square canvas with the mark fitted by height and centred — the form a
    favicon needs (a 68x100 mark in a 32x47 canvas is not a favicon)."""
    x0, y0, x1, y1 = mark_box(direction)
    k = size / (y1 - y0)
    w, h = (x1 - x0) * k, (y1 - y0) * k
    p = size * pad
    body = []
    for use, d in mark_paths(direction, mono):
        col = fill if use == "fg" else accent
        body.append(f'<g transform="translate({n(p - x0 * k)} {n(p + h)}) scale({n(k)} {n(-k)})">'
                    f'<path d="{d}" fill="{col}"/></g>')
    if square:
        side = h + 2 * p
        return (f'<g transform="translate({n((side - w) / 2.0)} 0)">' + "".join(body) + "</g>",
                side, side)
    return "".join(body), w + 2 * p, h + 2 * p


# --------------------------------------------------------------------------- tagline
def tagline_svg(text, size, tracking_f, x, y, fill, anchor="start"):
    parts, cx = [], 0.0
    tk = size * tracking_f
    for ch in text:
        if ch == " ":
            cx += advance(TAG_FONT, " ", size) + tk
            continue
        parts.append(f'<path transform="translate({n(cx)} 0)" d="{glyph_d(TAG_FONT, ch, size)}"/>')
        cx += advance(TAG_FONT, ch, size) + tk
    w = cx - tk
    ox = x if anchor == "start" else (x - w / 2.0 if anchor == "middle" else x - w)
    return (f'<g id="tagline" data-text="{text}" transform="translate({n(ox)} {n(y)})" '
            f'fill="{fill}" fill-rule="nonzero">' + "".join(parts) + "</g>"), w


def tagline_width(text, size, tracking_f):
    tk = size * tracking_f
    return sum(advance(TAG_FONT, c, size) for c in text) + tk * (len(text) - 1)


# --------------------------------------------------------------------------- lockups
def lockup_horizontal(direction, cap=158.1, mark_f=1.15, gap_f=0.52, tagline=True, fill=INK,
                      accent=COPPER, mono=False):
    """Mark left, wordmark right, baseline aligned; the tagline hangs under the wordmark."""
    ms = cap * mark_f
    parts, total = wordmark_paths(cap)
    pts = [p for d in parts for p in pairs(d)]
    sp = shear_pts(pts, True)
    x0, y0, x1, y1 = bounds(sp)
    mx0, my0, mx1, my1 = mark_box(direction)
    mk = ms / (my1 - my0)
    mw = (mx1 - mx0) * mk
    gap = cap * gap_f
    wm_ox = mw + gap - x0
    wm_oy = ms
    width = mw + gap + (x1 - x0)
    body = []
    for use, d in mark_paths(direction, mono):
        col = fill if use == "fg" else accent
        body.append(f'<g transform="translate({n(-mx0 * mk)} {n(ms)}) scale({n(mk)} {n(-mk)})">'
                    f'<path d="{d}" fill="{col}"/></g>')
    body.append(f'<g transform="translate({n(wm_ox)} {n(wm_oy)}) skewX({n(-SHEAR)})" '
                f'fill="{fill}" fill-rule="nonzero">' +
                "".join(f'<path d="{d}"/>' for d in parts) + "</g>")
    height = ms
    if tagline:
        ts = cap * 0.148
        body.append(tagline_svg(TAGLINE, ts, 0.20, mw + gap, ms + cap * 0.40, fill)[0])
        height = ms + cap * 0.40 + ts
    p = cap * 0.10
    return (f'<g transform="translate({n(p)} {n(p)})">' + "".join(body) + "</g>",
            width + 2 * p, height + 2 * p, mw + gap, ms)


def lockup_stacked(direction, cap=158.1, mark_f=1.55, gap_f=0.55, tagline=True, fill=INK,
                   accent=COPPER, mono=False):
    parts, total = wordmark_paths(cap)
    pts = [p for d in parts for p in pairs(d)]
    sp = shear_pts(pts, True)
    x0, y0, x1, y1 = bounds(sp)
    ww = x1 - x0
    ms = cap * mark_f
    mx0, my0, mx1, my1 = mark_box(direction)
    mk = ms / (my1 - my0)
    mw = (mx1 - mx0) * mk
    width = max(ww, mw)
    cx = width / 2.0
    body = [f'<g transform="translate({n(cx - mw / 2.0 - mx0 * mk)} {n(ms)}) scale({n(mk)} {n(-mk)})">' +
            "".join(f'<path d="{d}" fill="{fill if u == "fg" or mono else accent}"/>'
                    for u, d in mark_paths(direction, mono)) + "</g>"]
    ty = ms + cap * gap_f + cap                      # baseline of the wordmark
    body.append(f'<g transform="translate({n(cx - ww / 2.0 - x0)} {n(ty)}) '
                f'skewX({n(-SHEAR)})" fill="{fill}" fill-rule="nonzero">' +
                "".join(f'<path d="{d}"/>' for d in parts) + "</g>")
    height = ty
    if tagline:
        ts = cap * 0.148
        body.append(tagline_svg(TAGLINE, ts, 0.20, cx, ty + cap * 0.46, fill, "middle")[0])
        height = ty + cap * 0.46 + ts
    p = cap * 0.10
    return (f'<g transform="translate({n(p)} {n(p)})">' + "".join(body) + "</g>",
            width + 2 * p, height + 2 * p)


def svg(width, height, body, extra=""):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{n(width)}" height="{n(height)}" '
            f'viewBox="0 0 {n(width)} {n(height)}"{extra}>\n{body}\n</svg>\n')
