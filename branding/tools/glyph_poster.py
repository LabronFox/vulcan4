"""Glyph poster: render each of V U L C A N huge, alone, so the drawn geometry is judged directly."""
import subprocess, os
from typekit import glyph_d, glyph_bounds, metrics

OUT = "/home/or/vulcan4/branding/src/glyphs"
os.makedirs(OUT, exist_ok=True)
FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
SIZE = 300.0

cells = []
for ch in "VULCAN":
    d = glyph_d(FONT, ch, SIZE)
    b = glyph_bounds(FONT, ch, SIZE)
    cells.append((ch, d, b))
    print(ch, "ink w=%.1f  xmin=%.1f xmax=%.1f  top=%.1f" % (b[2] - b[0], b[0], b[2], b[1]))

W, H = 1900, 560
body = [f'<rect width="{W}" height="{H}" fill="#101012"/>']
x = 60
for ch, d, b in cells:
    body.append(f'<g transform="translate({x} 420)"><path d="{d}" fill="#F2F0EB"/></g>')
    body.append(f'<text x="{x}" y="500" fill="#5E5E62" font-family="monospace" font-size="22">{ch}  ink={b[2]-b[0]:.0f}</text>')
    x += 300
# baselines + cap line reference
body.append(f'<line x1="40" y1="420" x2="{W-40}" y2="420" stroke="#33333a"/>')
body.append(f'<line x1="40" y1="{420 - 0.79*SIZE:.0f}" x2="{W-40}" y2="{420 - 0.79*SIZE:.0f}" stroke="#24242a" stroke-dasharray="4 6"/>')
open(f"{OUT}/glyphs.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(body) + "</svg>")
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{OUT}/glyphs.png", f"{OUT}/glyphs.svg"], check=True)
print("wrote", f"{OUT}/glyphs.png")
