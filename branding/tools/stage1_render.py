"""Stage 1: judge the foundation + the delta-A variants. No decisions until the picture is seen."""
import os, subprocess
from build import wordmark_body, INK, PAPER, COPPER
from typekit import glyph_d, advance, metrics, glyph_bounds

OUT = "/home/or/vulcan4/branding/src/stage1"
os.makedirs(OUT, exist_ok=True)

SIZE = 190.0
ROWS = [
    ("01 impact .115 upright, bar .30", dict(shear=0, tracking=None, bar=0.30)),
    ("02 impact .115 sheared 12, bar .30", dict(shear=12, tracking=None, bar=0.30)),
    ("03 tracking .15 sheared 12, bar .30", dict(shear=12, tracking=None, bar=0.30, tk=0.15)),
    ("04 bar NONE (VULCAN / VULC-delta-N)", dict(shear=12, bar=None)),
    ("05 shear 14 cond .88 tk .13 bar .30", dict(shear=14, condense=0.88, bar=0.30, tk=0.13)),
    ("06 leg 17 (wider crown) shear 12", dict(shear=12, bar=0.30, leg_deg=17)),
    ("07 shear 9 tk .10 bar .30", dict(shear=9, bar=0.30, tk=0.10)),
    ("08 stem 1.06 heavier A, shear 12", dict(shear=12, bar=0.30, stem_scale=1.06)),
]

rowh = 300
W = 1500
H = rowh * len(ROWS) + 40
body = [f'<rect width="{W}" height="{H}" fill="#101012"/>']
notes = []
for i, (label, kw) in enumerate(ROWS):
    tk = kw.pop("tk", None)
    if tk:
        m = metrics("/usr/share/fonts/truetype/msttcorefonts/Impact.ttf", SIZE, kw.get("condense", 0.90))
        kw["tracking"] = m["cap"] * tk
    b, meta = wordmark_body(size=SIZE, dx=40, dy=200 + i * rowh, fill="#F2F0EB", **kw)
    body.append(b)
    body.append(f'<text x="40" y="{72 + i * rowh}" fill="#6E6E72" font-family="monospace" '
                f'font-size="21">{label}   w={meta["total"]:.0f} cap={meta["cap"]:.0f} '
                f'plateau={meta["a"]["plateau"]:.0f} tk={meta["tracking"]:.1f}</text>')
    body.append(f'<line x1="40" y1="{206 + i * rowh}" x2="{W - 40}" y2="{206 + i * rowh}" '
                f'stroke="#1E1E22" stroke-width="1"/>')
    notes.append((label, meta))

open(f"{OUT}/stage1.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
    + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/stage1.png", f"{OUT}/stage1.svg"], check=True)
print("wrote", f"{OUT}/stage1.png")
for label, m in notes:
    print(label, "| total=%.0f cap=%.1f plateau=%.1f tk=%.1f" % (m["total"], m["cap"], m["a"]["plateau"], m["tracking"]))
