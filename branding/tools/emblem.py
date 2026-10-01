"""emblem.py — three emblem directions for VULCAN.

Every mark is defined in a 100-unit square design box, then normalised and dropped into the
lockups by bounding box, so a mark's weight is a design decision and its placement is
mechanical. Flat geometry only: no gradients, no strokes-with-caps, no stock shapes.
"""
from geom import poly, n


def _bb(paths):
    xs, ys = [], []
    for pts in paths:
        for x, y in pts:
            xs.append(x)
            ys.append(y)
    return min(xs), min(ys), max(xs), max(ys)


def place(paths, size, baseline_y, align="bottom", cx=0.0):
    """Scale paths so the mark is `size` tall, bottom on baseline_y, centred on cx.
    Returns (list_of_point_lists, width)."""
    x0, y0, x1, y1 = _bb(paths)
    k = size / (y1 - y0)
    w = (x1 - x0) * k
    ox = cx - w / 2.0 - x0 * k
    oy = baseline_y - y1 * k
    return [[(x * k + ox, y * k + oy) for x, y in pts] for pts in paths], w


# ----------------------------------------------------------------------- A. WEDGE
def wedge(foot=20.0, void_top=40.0, apex_frac=0.88, w=100.0, h=100.0):
    """The V as one solid mass: straight-splayed legs, machined flat foot, and the void driven
    down to a chisel. Same construction as the wordmark's V, enlarged — the mark IS the letter."""
    xv = (w - foot) / 2.0
    vt = void_top / 2.0
    ax = w / 2.0
    ay = h * apex_frac
    pts = [(0, 0), (xv, h), (xv + foot, h), (w, 0),
           (w - vt, 0), (ax, ay), (vt, 0)]
    return [pts]


# ----------------------------------------------------------------------- B. SEAM
def seam(slip=13.0, th=30.0, foot=22.0, w=100.0, h=100.0):
    """Machine code, then native code: the two strokes of the V no longer meet at the vertex.
    One stroke has been translated — the seam is the point of translation. Returns
    (left_pts, right_pts); the slipped stroke carries the accent."""
    xv = (w - foot) / 2.0
    left = [(0, 0), (xv, h), (xv + foot, h), (th, 0)]
    base_r = [(w, 0), (xv + foot, h), (xv, h), (w - th, 0)]
    right = [(x + slip, y - slip * 1.05) for x, y in base_r]
    return left, right


# ----------------------------------------------------------------------- C. BARE
def bare(w=100.0, h=100.0, inset=8.0, mouth=22.0, apex=0.86, t=26.0):
    """The emulator layer removed: a solid plate with a V-shaped slot cut clean through it.
    One path with a hole (fill-rule evenodd) so it ships as a single flat shape."""
    plate = [(0, 0), (w, 0), (w, h), (0, h)]
    ax = w / 2.0
    ay = h * apex
    xl, xr = inset, w - inset
    hole = [(xl, 0), (ax, ay), (xr, 0), (xr - mouth, 0), (ax, ay - t), (xl + mouth, 0)]
    return plate, hole
