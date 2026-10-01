"""custom.py — bespoke letterform candidates, built in the FOUNDATION'S own glyph space.

Coordinates are Impact's, measured from the drawn outlines at em 200 (cap = 158.1) with
measure_glyphs.py, so a custom letter drops in exactly where the font glyph was: same side
bearings, same rhythm, no faked kerning. x is multiplied by `condense` and both axes by
cap/158.1, matching precisely what the font path pen does.

Each builder returns (fill_d, [knockout_pts, ...]) — knockouts are drawn in the background
colour so a candidate can be judged as a picture before it is committed to a real boolean.
"""
REF_CAP = 158.1
REF = 200.0

V = dict(x_r=105.9, x_l=-1.2, base_l=22.6, base_r=85.0, inn_l=42.2, inn_r=62.5, cx=52.35)
A = dict(apex_l=19.7, apex_r=79.2, base_r=102.7, base_l=-1.2, bar=-28.4, bar_l=43.8, bar_r=58.6)
U = dict(x_r=101.7, x_l=7.6, bot=3.3, inn_l=48.7, inn_r=60.5, floor=-23.4)
L = dict(x_l=8.2, x_r=74.3, foot=-31.6, stem_r=49.3)
N = dict(x_r=100.0, x_l=8.2, stem_l=42.6, stem_r=65.6)


def T(cap, condense=1.0):
    ky = cap / REF_CAP
    kx = ky * condense
    return kx, ky


def poly(pts, kx, ky):
    d = "M" + " L".join("%.3f %.3f" % (x * kx, y * ky) for x, y in pts) + " Z"
    return d


# --------------------------------------------------------------------------- V candidates

def v_sharp(cap, condense=1.0, void_apex=-22.0):
    """Straight void walls, apex driven deeper. The GT tell translated: their R leg is straight
    where a grotesque curls; our V's negative space is straight and cut to a chisel."""
    kx, ky = T(cap, condense)
    pts = [(V["x_r"], -REF_CAP), (V["base_r"], 0), (V["base_l"], 0), (V["x_l"], -REF_CAP),
           (V["inn_l"], -REF_CAP), (V["cx"], void_apex), (V["inn_r"], -REF_CAP)]
    return poly(pts, kx, ky), []


def v_solid(cap, condense=1.0, foot=16.0):
    """The V as a single solid wedge — no slot at all. The letter becomes the delta."""
    kx, ky = T(cap, condense)
    pts = [(V["x_l"], -REF_CAP), (V["x_r"], -REF_CAP),
           (V["cx"] + foot / 2, 0), (V["cx"] - foot / 2, 0)]
    return poly(pts, kx, ky), []


def v_key(cap, condense=1.0, w=19.0, h=44.0):
    """Impact's V with a triangular keyway cut up into its base — the letter keyed like a
    machined part, the keyway being the Vulkan delta sitting in the letter's chin."""
    kx, ky = T(cap, condense)
    o = V
    d = ("M %.3f %.3f L %.3f 0 L %.3f 0 L %.3f %.3f L %.3f %.3f "
         "Q %.3f %.3f %.3f %.3f Q %.3f %.3f %.3f %.3f L %.3f %.3f Z") % (
        o["x_r"] * kx, -REF_CAP * ky, o["base_r"] * kx, o["base_l"] * kx,
        o["x_l"] * kx, -REF_CAP * ky, o["inn_l"] * kx, -REF_CAP * ky,
        49.7 * kx, -92.8 * ky, 52.9 * kx, -47.7 * ky,
        56.2 * kx, -93.3 * ky, 59.7 * kx, -128.7 * ky,
        62.5 * kx, -REF_CAP * ky)
    key = [(o["cx"] - w, 14), (o["cx"], -h), (o["cx"] + w, 14)]
    return d, [key]


# --------------------------------------------------------------------------- A candidates

def a_solid(cap, condense=1.0):
    """The A as a solid delta: counter and crossbar gone — nothing to look through."""
    kx, ky = T(cap, condense)
    pts = [(A["apex_l"], -REF_CAP), (A["apex_r"], -REF_CAP), (A["base_r"], 0), (A["base_l"], 0)]
    return poly(pts, kx, ky), []


def a_delta(cap, condense=1.0, apex=14.0, bar=-30.0, bar_h=34.0):
    """The A redrawn as a Vulkan delta: sides splay to a flattened apex, crossbar kept so the
    letter still reads as A, and the counter is a straight-sided triangle (no curves)."""
    kx, ky = T(cap, condense)
    capk = REF_CAP * ky
    fl, fr = A["base_l"] * kx, A["base_r"] * kx
    cx = (fl + fr) / 2.0
    ap_l, ap_r = cx - apex / 2 * kx * (REF_CAP / REF_CAP), cx + apex / 2 * kx
    # stroke thickness measured horizontally at the baseline, kept equal to Impact's stem x 0.94
    hx = 38.6 * kx
    tl = fl + hx                     # left leg's inner foot
    tr = fr - hx
    outer = [(fl, 0), (ap_l, -capk), (ap_r, -capk), (fr, 0), (tr, 0), (cx, -bar * ky), (tl, 0)]
    by = bar * ky
    t = by / -capk                                   # height fraction of the crossbar
    il = tl + (ap_l - tl) * t
    ir = tr + (ap_r - tr) * t
    counter = [(il, by), (ir, by), (cx, -capk * 0.985)]
    barq = [(il - hx * 0.35, by), (ir + hx * 0.35, by), (ir + hx * 0.35, by - bar_h * ky),
            (il - hx * 0.35, by - bar_h * ky)]
    d = poly(outer, 1.0, 1.0) + " " + poly(barq, 1.0, 1.0)
    return d, [counter]


# --------------------------------------------------------------------------- L candidate

def l_chisel(cap, condense=1.0, rise=15.0):
    """The L's foot cut at the shear angle, so the letter's base reads as a machined edge."""
    kx, ky = T(cap, condense)
    o = L
    pts = [(o["x_l"], -REF_CAP), (o["stem_r"], -REF_CAP), (o["stem_r"], o["foot"]),
           (o["x_r"], o["foot"] - rise), (o["x_r"], 0), (o["x_l"], 0)]
    return poly(pts, kx, ky), []


VARIANTS = {
    "v_sharp": v_sharp, "v_solid": v_solid, "v_key": v_key,
    "a_solid": a_solid, "a_delta": a_delta, "l_chisel": l_chisel,
}
