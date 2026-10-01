"""Render VULCAN in every candidate base font so we can pick the foundation honestly."""
import os
import subprocess
from typekit import word, metrics, glyph_bounds

OUT = "/home/or/vulcan4/branding/src/fonttest"
os.makedirs(OUT, exist_ok=True)

CANDS = [
    ("impact",        "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf", 1.00, -8),
    ("arialblack",    "/usr/share/fonts/truetype/msttcorefonts/Arial_Black.ttf", 1.00, -8),
    ("librnarrow_bi", "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-BoldItalic.ttf", 1.00, 0),
    ("nimnarrow_bo",  "/usr/share/fonts/opentype/urw-base35/NimbusSansNarrow-BoldOblique.otf", 1.00, 0),
    ("dejavu_cbo",    "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-BoldOblique.ttf", 1.00, 0),
    ("lato_heavy_it", "/usr/share/fonts/truetype/lato/Lato-HeavyItalic.ttf", 0.88, -6),
    ("librnarrow_b",  "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf", 1.00, -10),
    ("dejavu_cb",     "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf", 1.00, -10),
]

SIZE = 200
TRACK = 6

rows = []
for name, path, cond, shear in CANDS:
    if not os.path.exists(path):
        rows.append((name, "MISSING", 0, 0, 0))
        continue
    d, w, _ = word(path, "VULCAN", SIZE, tracking=TRACK, shear_deg=shear, condense=cond, dx=20, dy=160)
    m = metrics(path, SIZE, cond)
    rows.append((name, d, w, m.get("cap", 0), m.get("stem", 0)))

row_h = 240
W = 1400
H = row_h * len(rows) + 40
body = [f'<rect width="{W}" height="{H}" fill="#111111"/>']
for i, (name, d, w, cap, stem) in enumerate(rows):
    y = i * row_h
    body.append(f'<line x1="0" y1="{y + 200}" x2="{W}" y2="{y + 200}" stroke="#2a2a2a" stroke-width="1"/>')
    if d == "MISSING":
        body.append(f'<text x="20" y="{y + 60}" fill="#ff4444" font-family="monospace" font-size="24">{name} MISSING</text>')
        continue
    body.append(f'<g transform="translate(0,{y})"><path d="{d}" fill="#f2f2f2"/></g>')
    body.append(f'<text x="20" y="{y + 226}" fill="#7a7a7a" font-family="monospace" font-size="20">'
                f'{name}  cond={[c for n,c,s,_ in CANDS if n==name][0]}  extra_shear={[s for n,c,s,_ in CANDS if n==name][0]}  '
                f'cap={cap:.1f} stem={stem:.1f} w={w:.0f}</text>')

open(f"{OUT}/fonttest.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(body) + "</svg>")

subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/fonttest.png", f"{OUT}/fonttest.svg"], check=True)
print("wrote", f"{OUT}/fonttest.png")
for r in rows:
    print(r[0], "cap=%.1f stem=%.1f width=%.0f" % (r[3], r[4], r[2]) if r[1] != "MISSING" else "MISSING")
