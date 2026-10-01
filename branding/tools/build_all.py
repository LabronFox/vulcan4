"""build_all.py — writes every VULCAN branding deliverable. One run, no manual steps.

Outputs (all under /home/or/vulcan4/branding):
  svg/     logo sources, outlined, no live text objects
  png/     raster exports on transparency
  contact/ the contact sheet Boss judges
"""
import os, subprocess, json
import logo as L

ROOT = "/home/or/vulcan4/branding"
SVG, PNG, CON = f"{ROOT}/svg", f"{ROOT}/png", f"{ROOT}/contact"
for d in (SVG, PNG, CON):
    os.makedirs(d, exist_ok=True)

CAP = 158.1
manifest = []


def write_svg(name, w, h, body, extra="", folder=SVG):
    path = f"{folder}/{name}.svg"
    open(path, "w").write(L.svg(w, h, body, extra))
    manifest.append(("svg", path, os.path.getsize(path)))
    return path


def raster(svg_name, png_name, width, transparent=True):
    src = f"{SVG}/{svg_name}.svg"
    out = f"{PNG}/{png_name}.png"
    cmd = ["rsvg-convert", "-w", str(width), "-o", out, src]
    subprocess.run(cmd, check=True)
    manifest.append(("png", out, os.path.getsize(out)))
    return out


DIRS = ["1_monolith", "2_wingpair", "3_grounded"]

# ---------------------------------------------------------------- 1. wordmark (deliverable 1)
body, w, h = L.wordmark_svg(fill=L.INK, cap=CAP)
write_svg("vulcan-wordmark", w, h, body)
body, w, h = L.wordmark_svg(fill="#FFFFFF", cap=CAP)
write_svg("vulcan-wordmark-inverse", w, h, body)
raster("vulcan-wordmark", "vulcan-wordmark-1024", 1024)
raster("vulcan-wordmark", "vulcan-wordmark-512", 512)
raster("vulcan-wordmark", "vulcan-wordmark-256", 256)
raster("vulcan-wordmark-inverse", "vulcan-wordmark-inverse-1024", 1024)

# ---------------------------------------------------------------- 2. emblems (deliverable 2)
for d in DIRS:
    b, w, h = L.mark_svg(d, size=100.0)
    write_svg(f"vulcan-mark-{d}", w, h, b)
    raster(f"vulcan-mark-{d}", f"vulcan-mark-{d}-1024", 1024)
    raster(f"vulcan-mark-{d}", f"vulcan-mark-{d}-512", 512)
    raster(f"vulcan-mark-{d}", f"vulcan-mark-{d}-256", 256)
    raster(f"vulcan-mark-{d}", f"vulcan-mark-{d}-64", 64)
    b, w, h = L.mark_svg(d, size=100.0, mono=True)
    write_svg(f"vulcan-mark-{d}-mono", w, h, b)

# ---------------------------------------------------------------- favicons (deliverable 5)
fav_body, fw, fh = L.mark_svg("1_monolith", size=100.0, pad=0.04, square=True)
write_svg("vulcan-favicon", fw, fh, fav_body)
raster("vulcan-favicon", "vulcan-favicon-64", 64)
raster("vulcan-favicon", "vulcan-favicon-32", 32)

# ---------------------------------------------------------------- 3. lockups (deliverable 3)
for d in DIRS:
    b, w, h, wmx, ms = L.lockup_horizontal(d, cap=CAP)
    write_svg(f"vulcan-lockup-h-{d}", w, h, b)
    raster(f"vulcan-lockup-h-{d}", f"vulcan-lockup-horizontal-{d}-2048", 2048)
    b, w, h = L.lockup_stacked(d, cap=CAP)
    write_svg(f"vulcan-lockup-v-{d}", w, h, b)
    raster(f"vulcan-lockup-v-{d}", f"vulcan-lockup-stacked-{d}-2048", 2048)

# recommended direction, all the practical variants
REC = "1_monolith"
b, w, h, _, _ = L.lockup_horizontal(REC, cap=CAP, tagline=False)
write_svg("vulcan-lockup-horizontal-plain", w, h, b)
raster("vulcan-lockup-horizontal-plain", "vulcan-lockup-horizontal-plain-2048", 2048)
b, w, h = L.lockup_stacked(REC, cap=CAP)
write_svg("vulcan-lockup-stacked", w, h, b)
raster("vulcan-lockup-stacked", "vulcan-lockup-stacked-2048", 2048)
b, w, h = L.lockup_stacked(REC, cap=CAP, mono=True)
write_svg("vulcan-lockup-stacked-mono", w, h, b)
raster("vulcan-lockup-stacked-mono", "vulcan-lockup-stacked-mono-2048", 2048)
b, w, h = L.lockup_stacked(REC, cap=CAP, fill="#FFFFFF")
write_svg("vulcan-lockup-stacked-inverse", w, h, b)

# ---------------------------------------------------------------- 4. colour chips (deliverable 4)
chips = [(L.INK, "INK", "primary"), ("#FFFFFF", "WHITE", "primary"),
         (L.COPPER, "COPPER", "single accent"), (L.PAPER, "PAPER", "warm ground")]
cw = 260
b = []
for i, (hexv, name, note) in enumerate(chips):
    x = i * cw
    b.append(f'<rect x="{x + 10}" y="70" width="{cw - 40}" height="150" fill="{hexv}"/>')
    b.append(f'<text x="{x + 10}" y="255" fill="#5A5A60" font-family="monospace" font-size="21">{name}</text>')
    b.append(f'<text x="{x + 10}" y="285" fill="#3A3A40" font-family="monospace" font-size="18">{hexv}</text>')
    b.append(f'<text x="{x + 10}" y="312" fill="#3A3A40" font-family="monospace" font-size="18">{note}</text>')
DOCS = f"{ROOT}/docs"
os.makedirs(DOCS, exist_ok=True)
write_svg("vulcan-colour-card", cw * 4, 360, "".join(b), folder=DOCS)

# ---------------------------------------------------------------- 6. contact sheet
import contact_sheet  # noqa: E402  (imported for its side effect of writing the sheet)
json.dump(manifest, open(f"{ROOT}/manifest.json", "w"), indent=1)
print("files written:", len(manifest))
for kind, path, size in manifest:
    print("%-4s %8d  %s" % (kind, size, path))
