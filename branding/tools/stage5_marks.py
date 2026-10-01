"""Stage 5: refined executions. Each mark is judged at 256, at 32, and in the lockup."""
import os, subprocess
from typekit import glyph_d, advance, metrics
from custom import poly, V, REF_CAP
from emblem import place
from geom import n

OUT = "/home/or/vulcan4/branding/src/stage5"
os.makedirs(OUT, exist_ok=True)
FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
COND, SHEAR, TKF = 0.90, 12.0, 0.115
BG, FG, CU = "#101012", "#F2F0EB", "#C4622D"
SIZE = 190.0
CAP = metrics(FONT, SIZE, COND)["cap"]


def word(cap):
    x, parts, s = 0.0, [], cap / CAP
    tk = cap * TKF
    for ch in "VULCAN":
        if ch == "V":
            pts = [(V["x_r"], -REF_CAP), (78.0, 0), (28.0, 0), (V["x_l"], -REF_CAP),
                   (V["inn_l"], -REF_CAP), (V["cx"], -18.0), (V["inn_r"], -REF_CAP)]
            d = poly([(px * s, py * s) for px, py in pts], COND, 1.0)
            parts.append(f'<path transform="translate({n(x)} 0)" d="{d}"/>')
        else:
            parts.append(f'<path d="{glyph_d(FONT, ch, SIZE * s, COND, dx=x)}"/>')
        x += advance(FONT, ch, SIZE * s, COND) + tk
    return "".join(parts), x - tk


# ----------------------------------------------------------------- mark geometry (100-box)
def wedge_pts(foot=22.0, void_top=42.0, apex=0.88, w=100.0, h=100.0):
    xv = (w - foot) / 2.0
    vt = void_top / 2.0
    return [(0, 0), (xv, h), (xv + foot, h), (w, 0), (w - vt, 0), (w / 2, h * apex), (vt, 0)]


def slip_pts(cut=0.45, dx=5.0, dy=6.0, foot=22.0, apex=0.88, w=100.0, h=100.0):
    """The V sliced horizontally; the upper section translated up-and-right. The seam is the
    point of translation. Returns (lower, left_top, right_top) as separate polygons."""
    xv = (w - foot) / 2.0
    ax, vt = w / 2.0, 21.0
    y0 = h * cut
    k = min(1.0, y0 / (h * apex))
    xl0 = xv * y0 / h
    xr0 = w - (w - xv - foot) * y0 / h
    il0 = vt + (ax - vt) * k
    ir0 = (w - vt) - (ax - vt) * k
    left_top = [(0, 0), (xl0, y0), (il0, y0), (vt, 0)]
    right_top = [(w, 0), (xr0, y0), (ir0, y0), (w - vt, 0)]
    lower = [(xl0, y0), (il0, y0), (ax, h * apex), (ir0, y0), (xr0, y0),
             (xv + foot, h), (xv, h)]
    left_top = [(x + dx, y - dy) for x, y in left_top]
    right_top = [(x + dx, y - dy) for x, y in right_top]
    return lower, left_top, right_top


def slot_closed(inset=9.0, th=22.0, apex=0.80, top=0.10, w=100.0, h=100.0):
    plate = [(0, 0), (w, 0), (w, h), (0, h)]
    ax, ay = w / 2.0, h * apex
    xl, xr = inset, w - inset
    ty = h * top
    hole = [(xl, ty), (ax, ay), (xr, ty), (xr - th, ty + th * 1.15), (ax, ay - th * 1.35),
            (xl + th, ty + th * 1.15)]
    return plate, hole


def delta_v(foot=26.0, bite=0.52, w=100.0, h=100.0):
    """A solid delta with a V bitten out of its base: the triangle and the letter in one mass."""
    ax = w / 2.0
    y = h * (1 - bite)
    return [(0, h), (ax, 0), (w, h), (ax + 18, h), (ax, y), (ax - 18, h)]


def copper_delta(void_top=42.0, apex=0.88, inset=3.5, w=100.0, h=100.0):
    ax = w / 2.0
    return [(void_top / 2 + inset, inset), (ax, h * apex - inset * 1.2), (w - void_top / 2 - inset, inset)]


def plinth(foot=22.0, th=6.0, w=100.0, cx=None):
    cx = cx if cx else w / 2.0
    half = w * 0.30
    return [(cx - half, 0), (cx + half, 0), (cx + half, th), (cx - half, th)]


MARKS = [
    ("1  WEDGE + copper delta in the void", "multi",
     [("fg", [wedge_pts()]), ("cu", [copper_delta()])]),
    ("2  WEDGE on a copper plinth", "multi",
     [("fg", [wedge_pts()]), ("cu", [plinth()])]),
    ("3  SLIP: upper section translated up-right, in copper", "multi",
     [("fg", [slip_pts()[0]]), ("cu", [slip_pts()[1], slip_pts()[2]])]),
    ("4  INVERSION: plate with a V bitten out of its top edge", "hole",
     ([(0, 0), (100, 0), (100, 100), (0, 100)], [(11, 0), (50, 74), (89, 0)])),
    ("5  SLOT CLOSED: a V-shaped hole inside the plate", "hole", slot_closed()),
    ("6  DELTA-V: a triangle with a V bitten out of its base", "multi", [("fg", [delta_v()])]),
]

rowh = 430
W = 1760
H = rowh * len(MARKS) + 140
body = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']


def render(mark, cx, base, size):
    kind, data = mark
    out = []
    if kind == "multi":
        allpts = [p for _, pl in data for p in pl]
        placed, w = place(allpts, size, base, cx=cx)
        i = 0
        for col, pl in data:
            for _ in pl:
                fill = FG if col == "fg" else CU
                out.append(f'<path d="{poly(placed[i], 1, 1)}" fill="{fill}"/>')
                i += 1
        return "".join(out), w
    plate, hole = data
    placed, w = place([plate, hole] if hole else [plate], size, base, cx=cx)
    d = poly(placed[0], 1, 1) + (" " + poly(placed[1], 1, 1) if hole else "")
    return f'<path d="{d}" fill="{FG}" fill-rule="evenodd"/>', w


for i, (name, kind, data) in enumerate(MARKS):
    y = 140 + i * rowh
    base = y + 280
    body.append(f'<text x="50" y="{y - 40}" fill="#8A8A90" font-family="monospace" font-size="23">{name}</text>')
    m, _ = render((kind, data), 180, base, 230)
    body.append(m + f'<text x="50" y="{base + 46}" fill="#5A5A60" font-family="monospace" font-size="18">230 px</text>')
    m, _ = render((kind, data), 450, base, 32)
    body.append(m + f'<text x="420" y="{base + 46}" fill="#5A5A60" font-family="monospace" font-size="18">32 px</text>')
    ms = CAP * 1.15
    m, _ = render((kind, data), 640 + ms / 2, base, ms)
    body.append(m)
    wb, _ = word(CAP)
    body.append(f'<g transform="translate({n(640 + ms + CAP * 0.55)} {n(base)}) skewX({n(-SHEAR)})" '
                f'fill="{FG}" fill-rule="nonzero">{wb}</g>')
    body.append(f'<text x="640" y="{base + 46}" fill="#5A5A60" font-family="monospace" font-size="18">lockup 1:1</text>')
    body.append(f'<line x1="40" y1="{y - 12}" x2="{W - 40}" y2="{y - 12}" stroke="#1C1C20"/>')

open(f"{OUT}/stage5.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
    + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/stage5.png", f"{OUT}/stage5.svg"], check=True)
print("wrote", f"{OUT}/stage5.png")
