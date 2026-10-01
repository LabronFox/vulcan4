"""contact_sheet.py — the one picture Boss judges: three directions, side by side, each proved
at 32 px in ink on paper, plus the construction proof for the one bespoke letter.

Rendered dark, sparse, grid-locked. Everything on it is generated from logo.py — if a picture
disagrees with the SVGs, one of them is a bug, not a rendering choice.
"""
import os, subprocess
import logo as L
from geom import n

ROOT = "/home/or/vulcan4/branding"
CON = f"{ROOT}/contact"
CAP = 158.1
BG = "#0E0E10"
CHIP = "#17171A"
LINE = "#222228"
WHITE = "#F2F0EB"
GREY = "#8A8A92"
DIM = "#5A5A62"
MONO = "DejaVu Sans Mono"

NAMES = {
    "1_monolith": ("01  MONOLITH",
                   ["The logotype's V, scaled up — nothing added and",
                    "nothing taken away. In all three directions the",
                    "emblem and the first letter are one drawn object."]),
    "2_wingpair": ("02  WINGPAIR",
                   ["The V's two strokes drawn apart: the gap opens",
                    "from 6 units at the foot to 26 at the cap line.",
                    "The delta is the gap — it is not an added shape."]),
    "3_grounded": ("03  GROUNDED",
                   ["The V standing on a copper rule: the disc the",
                    "software runs off. Two elements, one accent —",
                    "copper is used exactly once in the whole system."]),
}
ORDER = ["1_monolith", "2_wingpair", "3_grounded"]


def nest(body, x, y, w, h, vb_w, vb_h):
    return (f'<svg x="{x}" y="{y}" width="{w}" height="{h}" viewBox="0 0 {vb_w} {vb_h}" '
            f'preserveAspectRatio="xMidYMid meet">{body}</svg>')


def spec(d, fill, cap, floor=None):
    """One glyph specimen, framed to its own ink bounds, upright. `floor` = (x, y) in the
    glyph's own frame; draws the copper tick that shows where the counter's point was cut."""
    pts = L.pairs(d)
    x0, y0, x1, y1 = L.bounds(pts)
    w, h = x1 - x0, y1 - y0
    ann = ""
    if floor:
        fx, fy = floor
        X, Y = fx - x0, fy - y0
        ann = (f'<line x1="{n(X - 24)}" y1="{n(Y)}" x2="{n(X + 24)}" y2="{n(Y)}" '
               f'stroke="{L.COPPER}" stroke-width="2.5"/>')
    body = (f'<g transform="translate({n(-x0)} {n(-y0)})">'
            f'<path d="{d}" fill="{fill}" fill-rule="nonzero"/>{ann}</g>')
    return body, w, h


W, H = 1980, 1840
out = [f'<rect width="{W}" height="{H}" fill="{BG}"/>']
t = lambda x, y, s, fill, size, anchor="start", fam=MONO: out.append(  # noqa: E731
    f'<text x="{x}" y="{y}" fill="{fill}" font-family="{fam}" font-size="{size}" '
    f'text-anchor="{anchor}">{s}</text>')

# ---- header: the shared wordmark
wb, ww, wh = L.wordmark_svg(fill=WHITE, cap=CAP)
scale = 980.0 / ww
out.append(nest(wb, 90, 96, 980, wh * scale, ww, wh))
t(90, 64, "VULCAN", GREY, 26)
t(W - 90, 64, "logo directions  01-03  2026-10-01", DIM, 20, "end")
t(1140, 150, "the wordmark", GREY, 21)
t(1140, 184, "VULCAN — heavy condensed oblique, printed in", DIM, 19)
t(1140, 212, "outlines. One bespoke letter: the V.", DIM, 19)
t(1140, 252, "Impact, condensed 0.90, sheared 12 deg, tracked", GREY, 19)
t(1140, 278, "0.115 cap — generous, deliberately the opposite", GREY, 19)
t(1140, 304, "of the reference's tight setting.", GREY, 19)
out.append(f'<line x1="90" y1="392" x2="{W - 90}" y2="392" stroke="{LINE}"/>')

# ---- three columns
COLW, GUT, TOP = 560, 60, 440
for i, d in enumerate(ORDER):
    x = 90 + i * (COLW + GUT)
    name, lines = NAMES[d]
    t(x, TOP - 20, name, WHITE, 25)
    # big mark, white on the dark chip (inverse use)
    mb, mw, mh = L.mark_svg(d, size=150.0, fill=WHITE)
    ms = 300.0 / mh
    out.append(f'<rect x="{x}" y="{TOP + 12}" width="{COLW}" height="380" fill="{CHIP}"/>')
    out.append(nest(mb, x + (COLW - mw * ms) / 2, TOP + 12 + (380 - mh * ms) / 2,
                    mw * ms, mh * ms, mw, mh))
    # horizontal lockup
    lb, lw, lh, _, _ = L.lockup_horizontal(d, cap=CAP, fill=WHITE)
    ls = (COLW - 40) / lw
    out.append(nest(lb, x + 20, TOP + 430, lw * ls, lh * ls, lw, lh))
    # 32 px proof row: INK marks on PAPER, the way a favicon actually ships
    px = x + 20
    py = TOP + 430 + lh * ls + 62
    t(x + 20, py - 26, "what survives at 32 px  (ink on paper)", GREY, 19)
    for size, label in ((32, "32"), (24, "24"), (16, "16")):
        bb, bw, bh = L.mark_svg(d, size=100.0, pad=0.02, fill=L.INK)
        k = (size * 0.78) / bh
        cw = size * 1.6
        out.append(f'<rect x="{px}" y="{py}" width="{cw}" height="{cw}" '
                   f'fill="{L.PAPER}" rx="2"/>')
        out.append(nest(bb, px + (cw - bw * k) / 2, py + (cw - bh * k) / 2,
                        bw * k, bh * k, bw, bh))
        t(px + cw / 2, py + cw + 24, label, DIM, 17, "middle")
        px += cw + 20
    # caption
    cy = py + 110
    for j, line in enumerate(lines):
        t(x + 20, cy + j * 28, line, GREY, 19)

# ---- construction proof: the one bespoke letter
out.append(f'<line x1="90" y1="1310" x2="{W - 90}" y2="1310" stroke="{LINE}"/>')
t(90, 1352, "THE BESPOKE LETTER — V", WHITE, 22)
from typekit import glyph_d  # noqa: E402
CHISEL_CAP = 150.0
_vs = CHISEL_CAP / L.REF_CAP
ref_d = glyph_d(L.FONT, "V", L.REF_SIZE * _vs, L.CONDENSE)
floor = (L.V["cx"] * _vs * L.CONDENSE, -L.VOID_APEX * _vs)
our_d = L.bespoke_v(CHISEL_CAP)
rb, rw, rh = spec(ref_d, DIM, CHISEL_CAP)
ob, ow, oh = spec(our_d, WHITE, CHISEL_CAP, floor=floor)
out.append(nest(rb, 140, 1390, rw, rh, rw, rh))
out.append(nest(ob, 400, 1390, ow, oh, ow, oh))
t(140, 1580, "impact V  (foundation)", DIM, 18)
t(400, 1580, "OUR V  — chisel floor", WHITE, 18)
notes = [
    ("what changed, and why", WHITE),
    ("Impact's V keeps a pointed counter and a 62.4-unit base.", GREY),
    ("Ours holds the same cap height, stem weight and side bearings,", GREY),
    ("but the counter walls are drawn dead straight, the point is cut", GREY),
    ("off flat where the copper tick sits, and the base narrows to 50.", GREY),
    ("One letter, two contour decisions — both wordmark-scale details on", GREY),
    ("purpose. The reference's own tell is invisible at icon size too:", GREY),
    ("their R leg is straight where a grotesque would curl.", GREY),
]
for j, (s, col) in enumerate(notes):
    t(620, 1380 + j * 25, s, col, 20 if j == 0 else 19)

# ---- footer: palette + tagline
fy = H - 150
out.append(f'<line x1="90" y1="{fy - 40}" x2="{W - 90}" y2="{fy - 40}" stroke="{LINE}"/>')
t(90, fy, "COLOUR", WHITE, 21)
cx = 230
for hexv, nm in ((L.INK, "INK #0C0C0D"), ("#FFFFFF", "WHITE #FFFFFF"),
                 (L.COPPER, "COPPER #C4622D  (the only accent)")):
    out.append(f'<rect x="{cx}" y="{fy - 24}" width="34" height="34" fill="{hexv}" '
               f'stroke="{LINE}"/>')
    t(cx + 46, fy, nm, DIM, 19)
    cx += 360
t(W - 90, fy + 40, "tagline, kept as its own layer: THE SAME DISC, NATIVELY COMPILED", DIM, 19, "end")

body = "".join(out)
open(f"{CON}/vulcan-contact-sheet.svg", "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">{body}</svg>')
subprocess.run(["rsvg-convert", "-w", str(W), "-o", f"{CON}/vulcan-contact-sheet.png",
                f"{CON}/vulcan-contact-sheet.svg"], check=True)
print("wrote", f"{CON}/vulcan-contact-sheet.png")
