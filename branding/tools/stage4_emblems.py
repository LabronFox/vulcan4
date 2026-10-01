"""Stage 4: judge the three emblem directions — alone at 256, at 32, and in the horizontal lockup."""
import os, subprocess
from typekit import glyph_d, advance, metrics
from custom import poly, T, V, REF_CAP
from emblem import wedge, seam, bare, place
from geom import n

OUT = "/home/or/vulcan4/branding/src/stage4"
os.makedirs(OUT, exist_ok=True)
FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
COND, SHEAR, TKF = 0.90, 12.0, 0.115
BG, FG, CU = "#101012", "#F2F0EB", "#C4622D"
SIZE = 190.0
CAP = metrics(FONT, SIZE, COND)["cap"]


def word(cap):
    x, parts = 0.0, []
    tk = cap * TKF
    s = cap / CAP
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


def draw_mark(kind, data, cx, base, size):
    """Returns (svg_string, width) — the mark drawn centred at cx with its bottom on base."""
    if kind == "1c":
        pts, w = place(data, size, base, cx=cx)
        return f'<path d="{poly(pts[0], 1, 1)}" fill="{FG}"/>', w
    if kind == "2c":
        left, right = data
        p, w = place([left, right], size, base, cx=cx)
        return (f'<path d="{poly(p[0], 1, 1)}" fill="{FG}"/>'
                f'<path d="{poly(p[1], 1, 1)}" fill="{CU}"/>'), w
    plate, hole = data
    p, w = place([plate, hole], size, base, cx=cx)
    return f'<path d="{poly(p[0], 1, 1)} {poly(p[1], 1, 1)}" fill="{FG}" fill-rule="evenodd"/>', w


MARKS = [("A  WEDGE  - one solid mass, machined foot", "1c", wedge()),
         ("B  SEAM  - the strokes no longer meet (accent on the translated stroke)", "2c", seam()),
         ("C  BARE  - the emulator layer removed: V slot cut through a plate", "hole", bare())]

rowh = 470
W = 1700
H = rowh * len(MARKS) + 140
body = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
for i, (name, kind, data) in enumerate(MARKS):
    y = 140 + i * rowh
    base = y + 300
    body.append(f'<text x="50" y="{y - 40}" fill="#8A8A90" font-family="monospace" font-size="24">{name}</text>')
    m, _ = draw_mark(kind, data, 190, base, 256)
    body.append(m + f'<text x="60" y="{base + 50}" fill="#5A5A60" font-family="monospace" font-size="18">256 px</text>')
    m, _ = draw_mark(kind, data, 480, base, 32)
    body.append(m + f'<text x="460" y="{base + 50}" fill="#5A5A60" font-family="monospace" font-size="18">32 px</text>')
    ms = CAP * 1.15
    m, mw = draw_mark(kind, data, 700 + ms / 2, base, ms)
    body.append(m)
    wb, _ = word(CAP)
    body.append(f'<g transform="translate({n(700 + ms + CAP * 0.55)} {n(base)}) skewX({n(-SHEAR)})" '
                f'fill="{FG}" fill-rule="nonzero">{wb}</g>')
    body.append(f'<text x="700" y="{base + 50}" fill="#5A5A60" font-family="monospace" font-size="18">lockup 1:1</text>')
    body.append(f'<line x1="40" y1="{y - 10}" x2="{W - 40}" y2="{y - 10}" stroke="#1C1C20"/>')

open(f"{OUT}/stage4.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
    + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/stage4.png", f"{OUT}/stage4.svg"], check=True)
print("wrote", f"{OUT}/stage4.png")
