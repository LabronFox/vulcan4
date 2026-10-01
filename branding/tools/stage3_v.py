"""Stage 3: tune the V. How deep can the void go and how narrow the base before the V
stops being a V — judged at presentation size AND at a 32px cap, where the ink must not blob."""
import os, subprocess
from typekit import glyph_d, advance, metrics
from custom import poly, T, V, REF_CAP
from geom import n

OUT = "/home/or/vulcan4/branding/src/stage3"
os.makedirs(OUT, exist_ok=True)
FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
COND, SHEAR, TKF = 0.90, 12.0, 0.115
BG, FG = "#101012", "#F2F0EB"


def v_custom(cap, void_apex=-22.0, base_l=22.6, base_r=85.0, tip=None):
    kx, ky = T(cap, COND)
    if tip:                                     # flat-tipped void: apex cut like every other
        pts = [(V["x_r"], -REF_CAP), (base_r, 0), (base_l, 0), (V["x_l"], -REF_CAP),
               (V["inn_l"], -REF_CAP), (V["cx"] - tip / 2, void_apex),
               (V["cx"] + tip / 2, void_apex), (V["inn_r"], -REF_CAP)]
    else:
        pts = [(V["x_r"], -REF_CAP), (base_r, 0), (base_l, 0), (V["x_l"], -REF_CAP),
               (V["inn_l"], -REF_CAP), (V["cx"], void_apex), (V["inn_r"], -REF_CAP)]
    return poly(pts, kx, ky)


def word_line(cap, size, vd):
    x, parts = 0.0, []
    tk = cap * TKF
    for ch in "VULCAN":
        if ch == "V":
            parts.append(f'<path transform="translate({n(x)} 0)" d="{v_custom(cap, **vd)}"/>')
        else:
            parts.append(f'<path d="{glyph_d(FONT, ch, size, COND, dx=x)}"/>')
        x += advance(FONT, ch, size, COND) + tk
    return "".join(parts), x - tk


CASES = [
    ("A control: Impact's own V", {}),
    ("B void straight, apex -35, base 22.6/85", dict(void_apex=-35)),
    ("C void straight, apex -18, base 28/78", dict(void_apex=-18, base_l=28, base_r=78)),
    ("D void straight, apex -8, base 34/72", dict(void_apex=-8, base_l=34, base_r=72)),
    ("E void flat tip 7, apex -12, base 32/74", dict(void_apex=-12, base_l=32, base_r=74, tip=7)),
    ("F void flat tip 14, apex -20, base 30/76", dict(void_apex=-20, base_l=30, base_r=76, tip=14)),
    ("G solid wedge foot 34", dict(void_apex=100000, base_l=33, base_r=67)),
]

SIZE = 190.0
CAP = metrics(FONT, SIZE, COND)["cap"]
rowh = 300
W = 1560
H = rowh * len(CASES) + 240
body = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
for i, (label, vd) in enumerate(CASES):
    p, wdt = word_line(CAP, SIZE, vd)
    y = 210 + i * rowh
    body.append(f'<g transform="translate(40 {y}) skewX({n(-SHEAR)})" fill="{FG}" fill-rule="nonzero">{p}</g>')
    body.append(f'<text x="40" y="{y - 190}" fill="#77777c" font-family="monospace" font-size="21">{label}</text>')
    body.append(f'<line x1="40" y1="{y + 6}" x2="{W - 40}" y2="{y + 6}" stroke="#1C1C20"/>')
    # small-size proof: 32 px letter height, same V, rendered 1:1
    ys = 250 + i * rowh
    sc = 32.0 / CAP
    body.append(f'<g transform="translate(1200 {y - 40}) scale({n(sc)}) skewX({n(-SHEAR)})" '
                f'fill="{FG}" fill-rule="nonzero">{p}</g>')
    body.append(f'<text x="1150" y="{y - 190}" fill="#4a4a50" font-family="monospace" font-size="19">32px cap</text>')

open(f"{OUT}/stage3.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
    + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/stage3.png", f"{OUT}/stage3.svg"], check=True)
print("wrote", f"{OUT}/stage3.png")
