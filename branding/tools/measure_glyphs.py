"""Measure the real drawn geometry of Impact's glyphs (output coords: y down, baseline 0)."""
from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen

FONT = "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"
EM = 200.0

f = TTFont(FONT, lazy=True)
upem = f["head"].unitsPerEm
gs = f.getGlyphSet()
cmap = f.getBestCmap()
s = EM / upem


def fmt(p):
    if p is None:
        return "None"
    return "(%g, %g)" % (round(p[0] * s, 1), round(-p[1] * s, 1))


for ch in ("V", "A", "U", "L", "C", "N"):
    rec = DecomposingRecordingPen(gs)
    gs[cmap[ord(ch)]].draw(rec)
    print("== %s  (%d ops)" % (ch, len(rec.value)))
    for op, args in rec.value:
        print("   %-10s %s" % (op, " ".join(fmt(p) for p in args) if args else ""))
    print()
