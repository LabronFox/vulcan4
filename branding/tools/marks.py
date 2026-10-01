"""marks.py — the VULCAN emblems. Three directions, all cut from the logotype's own V.

Every mark is a set of closed flat polygons: no curves, no strokes, no gradients, no
half-tones. That is not a style choice — it is the only thing that survives 32 px, and the
32 px render is on the contact sheet as proof.

ONE BOX FOR ALL THREE. x is in mark units, y runs 0 at the CAP LINE down to 100 at the
BASELINE. `logo.mark_paths()` flips that for the SVG. The letter's own landmarks, taken from
`custom.V` so the emblem can never drift away from the wordmark:

    cap line  y = 0     TL 0.00   IL 27.45   CX 33.87   IR 40.29   TR 67.74
    baseline  y = 100   BL 15.05                                   BR 54.52

The three directions differ in silhouette, not in decoration:

    1 monolith   the V as one solid mass — the letter itself, nothing added
    2 wingpair   the V's two strokes drawn apart; the DELTA IS THE GAP between them
    3 grounded   the V standing on a copper rule — the disc the game runs from

Accent discipline: copper appears in direction 3 only, on one element, once.
"""
from custom import V, REF_CAP

SC = 100.0 / REF_CAP
X0 = V["x_l"]


def _x(v):
    """Letter x -> mark x (left side bearing stripped, scaled to cap = 100)."""
    return (v - X0) * SC


TL = _x(V["x_l"])       # 0.000    outer left, cap line
TR = _x(V["x_r"])       # 67.740   outer right, cap line
IL = _x(V["inn_l"])     # 27.450   inner left, cap line
IR = _x(V["inn_r"])     # 40.290   inner right, cap line
CX = _x(V["cx"])        # 33.870   the notch centre
BL = _x(V["base_l"])    # 15.050   outer left, baseline
BR = _x(V["base_r"])    # 54.520   outer right, baseline

FA = 4.5 * SC           # 2.846    half the width of the counter's flat floor (the chisel)


def v_mark(void_apex=18.0):
    """The letter V's own outline, lifted from the wordmark and normalised to cap = 100.

    Vertex order follows the drawn outline exactly (top-right, base-right, base-left,
    top-left, inner-left, chisel-floor-left, chisel-floor-right, inner-right). Any other order
    makes the contour self-cross.

    `void_apex` is how far ABOVE the baseline the counter's flat floor sits, in reference
    units: 18 is the logotype's own setting (the counter is driven down to 11.4% of the cap).
    Larger = a shallower, chunkier cut; smaller = the strokes are nearly severed.
    """
    ay = (REF_CAP - void_apex) * SC
    return [(TR, 0.0), (BR, 100.0), (BL, 100.0), (TL, 0.0), (IL, 0.0),
            (CX - FA, ay), (CX + FA, ay), (IR, 0.0)]


def wingpair(gap_top=26.0, gap_bot=6.0):
    """The V's two strokes drawn apart. THE DELTA IS THE GAP.

    The inner edges are not the letter's own counter walls — they are re-cut so the gap opens
    from `gap_bot` units at the foot to `gap_top` at the cap line. That is the whole point:
    the triangle between the strokes is the Vulkan delta, and it is not an added shape, it is
    what the letter leaves behind when its strokes are pulled apart.

    Sizing is a 32 px decision, measured not guessed: gap_bot 6 = 1.9 px of daylight at 32 px
    (the split is visible), gap_top 26 = 8.3 px (the delta reads as a triangle), strokes 6.5 px
    wide (thick enough to survive the rasteriser). The first attempt used the letter's own
    counter walls and collapsed into two bars at 32 px — this is the fix.
    """
    left = [(TL, 0.0), (CX - gap_top / 2.0, 0.0), (CX - gap_bot / 2.0, 100.0), (BL, 100.0)]
    right = [(CX + gap_top / 2.0, 0.0), (TR, 0.0), (BR, 100.0), (CX + gap_bot / 2.0, 100.0)]
    return [left, right]


def grounded(v_scale=0.80, rule_h=9.0, overhang=8.0):
    """The V standing on a rule: the disc. Returns (v_poly, rule_poly).

    The letter is the mark; the rule is the surface the game runs off. Flat, two elements,
    no fill tricks — at 32 px it is a solid V with a bar under it, which is a silhouette no
    other direction on the sheet shares.

    The V is squeezed to `v_scale` so the whole mark still spans exactly y = 0..100 (the
    coordinate convention assumes the box is full; a mark that stops short of it drifts).
    """
    v = [(x, y * v_scale) for x, y in v_mark()]
    top = 100.0 - rule_h
    rule = [(TL - overhang, top), (TR + overhang, top),
            (TR + overhang, 100.0), (TL - overhang, 100.0)]
    return v, rule


DIRECTIONS = {
    "1_monolith": lambda: [("fg", [v_mark()])],
    "2_wingpair": lambda: [("fg", wingpair())],
    "3_grounded": lambda: [("fg", [grounded()[0]]), ("cu", [grounded()[1]])],
}

ORDER = ["1_monolith", "2_wingpair", "3_grounded"]
