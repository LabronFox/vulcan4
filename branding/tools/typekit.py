"""typekit.py — outline extraction for the VULCAN logotype.

WHY outline extraction: the wordmark must ship as OUTLINED PATHS (no live text objects).
A logotype that needs an installed font is not a logotype. We take real drawn curves from a
system typeface as the FOUNDATION (U, L, C, N) and hand-build the bespoke letters (A) as
explicit geometry, so the bespoke letters share the foundation's cap height and stem weight.

COORDINATE SYSTEM (output / SVG): x right, y DOWN, baseline y = 0, text starts at x = 0.
All paths are emitted UPRIGHT — the oblique lean is applied once, at SVG group level, with
skewX(-theta), so foundation glyphs and hand-built glyphs share exactly the same shear.
"""
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.misc.transform import Transform
from fontTools.pens.boundsPen import BoundsPen

_cache = {}


def load(path):
    if path not in _cache:
        _cache[path] = TTFont(path, fontNumber=0, lazy=True)
    return _cache[path]


def _s(path, size, condense):
    f = load(path)
    return size / f["head"].unitsPerEm, f


def glyph_d(path, ch, size, condense=1.0, dx=0.0, dy=0.0):
    """Upright outline for one glyph, in output coords."""
    f = load(path)
    upem = f["head"].unitsPerEm
    gs = f.getGlyphSet()
    gname = f.getBestCmap()[ord(ch)]
    s = size / upem
    t = Transform().translate(dx, dy).scale(s * condense, -s)
    pen = SVGPathPen(gs)
    gs[gname].draw(TransformPen(pen, t))
    return pen.getCommands()


def advance(path, ch, size, condense=1.0):
    f = load(path)
    upem = f["head"].unitsPerEm
    return f["hmtx"][f.getBestCmap()[ord(ch)]][0] * (size / upem) * condense


def glyph_bounds(path, ch, size, condense=1.0):
    f = load(path)
    upem = f["head"].unitsPerEm
    gs = f.getGlyphSet()
    gname = f.getBestCmap()[ord(ch)]
    bp = BoundsPen(gs)
    gs[gname].draw(bp)
    if bp.bounds is None:
        return None
    s = size / upem
    x0, y0, x1, y1 = bp.bounds
    return (x0 * s * condense, -y1 * s, x1 * s * condense, -y0 * s)


def metrics(path, size, condense=1.0):
    """cap height / stem width / x-height — the numbers the bespoke letters must match."""
    out = {}
    ib = glyph_bounds(path, "I", size, condense)
    if ib:
        out["cap"] = ib[3] - ib[1]
        out["stem"] = ib[2] - ib[0]
    xb = glyph_bounds(path, "x", size, condense)
    if xb:
        out["xheight"] = xb[3] - xb[1]
    return out
