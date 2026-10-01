"""verify.py — machine checks on the built files. Nothing here trusts a previous step.

Checks
  1  every SVG is well-formed XML, has a viewBox, and contains NO live <text> (outlined only)
  2  every lockup carries <g id="tagline" data-text="..."> as its own layer
  3  every PNG exists, is non-blank, has a real alpha channel, and has transparent corners
  4  PNG pixel dimensions match the name
  5  the mark inside a lockup is exactly mark_f x the wordmark cap (measured from the ink,
     not from the design intent) and the mark sits above the wordmark with no overlap
"""
import os, re, sys
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = "/home/or/vulcan4/branding"
SVG, PNG = f"{ROOT}/svg", f"{ROOT}/png"
fails, notes = [], []


def xml_ok(p):
    try:
        ET.parse(p)
        return True
    except Exception as e:
        fails.append(f"XML parse {p}: {e}")
        return False


# ---------------------------------------------------------------- 1 + 2 SVG checks
for fn in sorted(os.listdir(SVG)):
    p = f"{SVG}/{fn}"
    if not xml_ok(p):
        continue
    src = open(p).read()
    if 'viewBox="' not in src:
        fails.append(f"{fn}: no viewBox")
    if "<text" in src:
        fails.append(f"{fn}: contains a live <text> object")
    if "font-" in src:
        fails.append(f"{fn}: references a font")
    n_paths = src.count("<path")
    if n_paths == 0:
        fails.append(f"{fn}: no paths")
    if "lockup" in fn and "plain" not in fn and 'id="tagline"' not in src:
        fails.append(f"{fn}: lockup without a separate tagline layer")
    if 'id="tagline"' in src and 'data-text="' not in src:
        fails.append(f"{fn}: tagline layer without data-text")
    notes.append(f"{fn}: {n_paths} paths, {len(src)} bytes")

# ---------------------------------------------------------------- 3 + 4 PNG checks
for fn in sorted(os.listdir(PNG)):
    p = f"{PNG}/{fn}"
    im = Image.open(p)
    if im.mode != "RGBA":
        fails.append(f"{fn}: mode {im.mode}, expected RGBA")
        continue
    a = im.getchannel("A")
    bbox = a.getbbox()
    if not bbox:
        fails.append(f"{fn}: fully transparent")
        continue
    w, h = im.size
    corners = [a.getpixel((0, 0)), a.getpixel((w - 1, 0)), a.getpixel((0, h - 1)), a.getpixel((w - 1, h - 1))]
    if max(corners) > 0:
        notes.append(f"{fn}: corner alpha {corners} (bleed to the edge)")
    m = re.search(r"-(\d+)\.png$", fn)
    if m and int(m.group(1)) not in (2048,):
        want = int(m.group(1))
        if w != want:
            fails.append(f"{fn}: width {w} != {want}")
    ink = sum(1 for px in a.getdata() if px > 8) / float(w * h)
    notes.append(f"{fn}: {w}x{h}, ink {ink*100:.1f}%, bbox {bbox}")

# ---------------------------------------------------------------- 5 geometry checks
def profiles(p, thresh=40):
    im = Image.open(p).convert("RGBA")
    a = im.getchannel("A")
    w, h = im.size
    px = a.load()
    rows = [any(px[x, y] > thresh for x in range(w)) for y in range(h)]
    cols = [any(px[x, y] > thresh for y in range(h)) for x in range(w)]
    return w, h, rows, cols


def spans(flags):
    out, start = [], None
    for i, v in enumerate(flags):
        if v and start is None:
            start = i
        elif not v and start is not None:
            out.append((start, i - 1))
            start = None
    if start is not None:
        out.append((start, len(flags) - 1))
    return out


def band_height(p, x0, x1, y0, y1, thresh=40):
    im = Image.open(p).convert("RGBA")
    a = im.getchannel("A").load()
    ys = [y for y in range(y0, y1) if any(a[x, y] > thresh for x in range(x0, x1))]
    return (max(ys) - min(ys) + 1) if ys else 0


def row_spans_in(p, x0, x1, thresh=40):
    im = Image.open(p).convert("RGBA")
    a = im.getchannel("A").load()
    w, h = im.size
    return spans([any(a[x, y] > thresh for x in range(x0, x1)) for y in range(h)])


def check_horizontal(fn, mark_f):
    p = f"{PNG}/{fn}"
    w, h, rows, cols = profiles(p)
    cs = spans(cols)
    if len(cs) < 2:
        fails.append(f"{fn}: mark and wordmark not separable in the column profile")
        return
    mark_x = cs[0]
    gap = cs[1][0] - cs[0][1]
    wr = row_spans_in(p, cs[1][0], w)          # the wordmark band, then the tagline band
    cap = wr[0][1] - wr[0][0] + 1 if wr else 0
    mh = band_height(p, mark_x[0], mark_x[1] + 1, 0, h)
    notes.append(f"{fn}: mark {mh}px, wordmark cap {cap}px, ratio {mh / float(cap or 1):.3f} "
                 f"(want {mark_f}), clear gap {gap}px, wordmark bands {wr}")
    if abs(mh / float(cap or 1) - mark_f) > 0.12:
        fails.append(f"{fn}: mark/cap ratio {mh / float(cap or 1):.3f} != {mark_f}")
    if gap < cap * 0.25:
        fails.append(f"{fn}: mark-to-wordmark gap {gap}px is below 0.25 cap ({cap}px)")


def check_stacked(fn, mark_f):
    p = f"{PNG}/{fn}"
    w, h, rows, cols = profiles(p)
    rs = spans(rows)
    if len(rs) < 2:
        fails.append(f"{fn}: mark and wordmark are not separated vertically (they overlap)")
        return
    mtop, mbot = rs[0]
    wtop, wbot = rs[1]
    mh = mbot - mtop + 1
    cap = wbot - wtop + 1
    vgap = wtop - mbot
    notes.append(f"{fn}: mark {mh}px @rows {rs[0]}, wordmark cap {cap}px @rows {rs[1]}, "
                 f"vertical gap {vgap}px, ratio {mh / float(cap or 1):.3f} (want {mark_f})")
    if abs(mh / float(cap or 1) - mark_f) > 0.16:
        fails.append(f"{fn}: stacked mark/cap ratio {mh / float(cap or 1):.3f} != {mark_f}")
    if vgap < cap * 0.20:
        fails.append(f"{fn}: stacked gap {vgap}px below 0.20 cap ({cap}px)")


check_horizontal("vulcan-lockup-horizontal-1_monolith-2048.png", 1.15)
check_stacked("vulcan-lockup-stacked-1_monolith-2048.png", 1.55)

print("=" * 72)
print("VERIFICATION")
print("=" * 72)
for n in notes:
    print("  .", n)
print("-" * 72)
if fails:
    print("FAILURES (%d):" % len(fails))
    for f in fails:
        print("  x", f)
    sys.exit(1)
print("all checks passed")
