"""letterforms.py — the bespoke VULCAN letterform (the delta-A) and the emblem geometry.

THE BESPOKE LETTER: A, drawn as a Vulkan delta with a flattened apex.
Rationale in brief: A is the only letter in VULCAN whose top is already a point, so shaving
that point flat echoes the Vulkan triangle in the letterform itself; the emblem is then the
same construction rotated 180 degrees, so the mark and the logotype share one geometric rule.
"""
from math import tan, radians
from geom import poly, quads, n


def delta_A(cap, width, stem, leg_deg=13.0, bar=None, bar_th=None):
    """A = truncated delta. `bar` is the crossbar's centre height as a fraction of cap
    (None = no crossbar). stem = horizontal thickness of a leg measured at the baseline."""
    t = tan(radians(leg_deg))
    px = cap * t                      # horizontal run of a leg over the cap height
    hx = stem                         # horizontal thickness of one leg
    pl = 2 * px + 2 * hx - width      # resulting plateau (flat top) width
    left = [(0, 0), (px, -cap), (px + hx, -cap), (hx, 0)]
    right = [(width, 0), (width - px, -cap), (width - px - hx, -cap), (width - hx, 0)]
    shapes = [left, right]
    if bar is not None:
        y = bar * cap
        th = bar_th if bar_th else hx * 0.92
        # inner edge x at height |y| for each leg, plus a small overlap into the leg
        xl = hx + y * t - hx * 0.30
        xr = (width - hx) - y * t + hx * 0.30
        shapes.append([(xl, -y), (xr, -y), (xr, -(y + th)), (xl, -(y + th))])
    return quads(*shapes), pl


def delta_A_counter_apex(cap, width, stem, leg_deg=13.0):
    t = tan(radians(leg_deg))
    px, hx = cap * t, stem
    k = (width - 2 * hx) / (2 * px)
    return k * cap


# ---------------------------------------------------------------- emblem geometry

def emblem_wedge(w=100.0, h=104.0, th=30.0, foot=10.0):
    """V as one solid mass: splayed legs, flat top terminals, flat vertex foot.
    The void between the legs is the delta. Single closed path."""
    xv = w / 2 - foot / 2
    xa = w / 2
    # inner edge is parallel to the outer edge (constant leg thickness = th horizontal)
    y_a = (xa - th) * (h / xv)
    return poly([(0, 0), (xv, h), (xv + foot, h), (w, 0), (w - th, 0), (xa, y_a), (th, 0)])


def emblem_seam(w=100.0, h=104.0, th=30.0, slip=13.0, foot=8.0):
    """Two strokes, the right one slipped up-and-right where the vertex should close:
    the seam where machine code becomes native code. Returns (left_d, right_d)."""
    half = w / 2
    # left stroke: full left leg, terminating at the centre
    left = [(0, 0), (half, h), (half + foot, h), (th, 0)]
    l = poly(left)
    # right stroke: same leg, translated by (slip, -slip*3/4) => the vertex never closes
    dx, dy = slip, -slip * 0.85
    r = poly([(half + dx, dy), (w + dx, h + dy), (w + dx - foot, h + dy), (w - th + dx, dy)])
    return l, r


def emblem_bare(w=100.0, h=100.0, inset=0.0, void_w=34.0, void_h=46.0):
    """A full plate with a V cut clean out of it: the absent emulator layer.
    Returns (plate_d, void_polygon) so the void can be knocked out with fill-rule=evenodd."""
    plate = [(0, 0), (w, 0), (w, h), (0, h)]
    cx = w / 2
    # V void: a thick chevron opening upward, cut from the plate
    top = h * 0.06
    v = [(cx - void_w / 2, top), (cx, h - 4), (cx + void_w / 2, top),
         (cx + void_w / 2, top + 12), (cx, h - 16), (cx - void_w / 2, top + 12)]
    return poly(plate) + " " + poly(v), v
