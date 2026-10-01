"""Stage 1: pick the foundation + the delta-A variant. Renders candidates and a 32px strip."""
import subprocess, os
from typekit import glyph_d, advance, metrics, glyph_bounds
from letterforms import delta_A, delta_A_counter_apex
from geom import poly

OUT = "/home/or/vulcan4/branding/src/stage1"
os.makedirs(OUT, exist_ok=True)

IMPACT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
LIBNAR = "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf"
NIMNAR = "/usr/share/fonts/opentype/urw-base35/NimbusSansNarrow-Bold.otf"

SIZE = 200

for name, fp in [("impact", IMPACT), ("libnar", LIBNAR), ("nimnar", NIMNAR)]:
    m = metrics(fp, SIZE)
    ab = glyph_bounds(fp, "A", SIZE)
    vb = glyph_bounds(fp, "V", SIZE)
    print(name, "cap=%.1f stem=%.1f  A w=%.1f adv=%.1f | V w=%.1f adv=%.1f"
          % (m["cap"], m["stem"], ab[2] - ab[0], advance(fp, "A", SIZE), vb[2] - vb[0], advance(fp, "V", SIZE)))
    for barf in (None, 0.30, 0.26):
        pl = delta_A(m["cap"], ab[2] - ab[0], m["stem"], bar=barf)[1]
        print("   bar=%s plateau=%.1f counter_apex=%.1f" % (barf, pl, delta_A_counter_apex(m["cap"], ab[2] - ab[0], m["stem"])))
