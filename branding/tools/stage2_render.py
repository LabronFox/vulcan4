"""Stage 2: judge the bespoke letterform candidates as whole wordmarks. Pictures decide."""
import os, subprocess
from typekit import glyph_d, advance, metrics, glyph_bounds
from custom import VARIANTS
from geom import n

OUT = "/home/or/vulcan4/branding/src/stage2"
os.makedirs(OUT, exist_ok=True)
FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"

SIZE, COND, SHEAR, TKF = 190.0, 0.90, 12.0, 0.115
CAP = metrics(FONT, SIZE, COND)["cap"]
TK = CAP * TKF
BG = "#101012"
FG = "#F2F0EB"

VAR = [
    ("00 CONTROL - pure Impact, unmodified", {}),
    ("01 V-SHARP - straight void walls, deeper apex", {"V": ("v_sharp", {})}),
    ("02 V-SOLID - the V as a single wedge", {"V": ("v_solid", {})}),
    ("03 V-KEY - triangular keyway in the V's chin", {"V": ("v_key", {})}),
    ("04 A-SOLID - the A as a solid delta", {"A": ("a_solid", {})}),
    ("05 A-DELTA - A redrawn with splayed sides + flat apex", {"A": ("a_delta", {})}),
    ("06 L-CHISEL - the L's foot cut at the shear angle", {"L": ("l_chisel", {})}),
    ("07 V-SHARP + A-SOLID (two letters moved)", {"V": ("v_sharp", {}), "A": ("a_solid", {})}),
]


def word(subs):
    parts, knocks, x = [], [], 0.0
    for ch in "VULCAN":
        if ch in subs:
            name, kw = subs[ch]
            d, kn = VARIANTS[name](CAP, COND, **kw)
            parts.append(f'<path transform="translate({n(x)} 0)" d="{d}"/>')
            for kp in kn:
                kd = "M" + " L".join("%.3f %.3f" % (px, py) for px, py in kp) + " Z"
                knocks.append(f'<path transform="translate({n(x)} 0)" d="{kd}" fill="{BG}"/>')
        else:
            parts.append(f'<path d="{glyph_d(FONT, ch, SIZE, COND, dx=x)}"/>')
        x += advance(FONT, ch, SIZE, COND) + TK
    return "".join(parts), "".join(knocks), x - TK


rowh = 300
W = 1560
H = rowh * len(VAR) + 60
body = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
for i, (label, subs) in enumerate(VAR):
    p, kn, wdt = word(subs)
    y = 210 + i * rowh
    body.append(f'<g transform="translate(40 {y}) skewX({n(-SHEAR)})" fill="{FG}" fill-rule="nonzero">{p}{kn}</g>')
    body.append(f'<text x="40" y="{y - 190}" fill="#77777c" font-family="monospace" font-size="21">{label}</text>')
    body.append(f'<text x="{W - 300}" y="{y - 190}" fill="#4a4a50" font-family="monospace" font-size="21">w={wdt:.0f}</text>')
    body.append(f'<line x1="40" y1="{y + 6}" x2="{W - 40}" y2="{y + 6}" stroke="#1C1C20"/>')

open(f"{OUT}/stage2.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
    + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/stage2.png", f"{OUT}/stage2.svg"], check=True)
print("wrote", f"{OUT}/stage2.png", "cap=%.1f tk=%.1f" % (CAP, TK))
