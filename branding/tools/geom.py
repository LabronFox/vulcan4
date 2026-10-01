"""geom.py — tiny path builders. Upright output coords, y down, baseline y = 0."""
from math import tan, radians


def n(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def poly(pts, close=True):
    d = "M" + " L".join("%s %s" % (n(x), n(y)) for x, y in pts)
    return d + (" Z" if close else "")


def quads(*shapes):
    """Union several closed polygons into ONE path (nonzero fill => overlaps union cleanly)."""
    return " ".join(poly(s) for s in shapes)


def obl(x, y, o):
    """Apply the oblique lean to a point: top of letter (y<0) shifts +x (leans right)."""
    return (x - y * tan(radians(o)), y)


def ell(cx, cy, rx, ry):
    return ("M %s %s a %s %s 0 1 0 %s 0 a %s %s 0 1 0 %s 0 Z"
            % (n(cx - rx), n(cy), n(rx), n(ry), n(2 * rx), n(rx), n(ry), n(-2 * rx)))
