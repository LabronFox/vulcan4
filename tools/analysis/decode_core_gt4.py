#!/usr/bin/env python3
"""decode_core_gt4 - decode GT4's CORE.GT4 into the image the game actually runs (golden reference).

WHY. VULCAN 4's boot is: SCUS_973.28 (bootstrap, 273 KB, this is what we recompiled) finds CORE.GT4,
inflates it (raw DEFLATE), reads a small header, and ExecPS2's into the REAL game at 0x00100008. Every
wall about "the game never talks to its files" is really about that decode. Until now we had no
byte-exact target for it. Now we do: this script produces it, from the game's own file, with a plain
zlib inflate -- no game code, no guessing.

FORMAT (documented by the GT modding community, MIT: nenkai.github.io/gt-modding-hub, PDTools
GT4ElfBuilderTool/GTImageLoader.cs):
    CORE.GT4
      0x00 ushort  boot load flags
      0x02 uint    decompressed size
      0x06 ...     raw DEFLATE stream            -> the image below
    image (after inflate)
      short  rsa1_len ; byte[rsa1_len] RSA modulus
      short  rsa2_len ; byte[rsa2_len] RSA value (SHA-512 of the body is signed with it)
      int    section count
      int    entry point                         <- where the bootstrap jumps
      per section: int target_addr, int size, byte[size] data
    (GT4 Online additionally encrypts the body with Salsa20, key = XOR 0x55 of "PolyphonyDigital";
     the NTSC-U retail disc we own does NOT -- it is plain DEFLATE. Verified here.)

MEASURED on our own disc, /mnt/ssd/gt4/work/CORE.GT4 (2,020,861 B, 2026-10-09):
    flags=0x0101  declared=6,119,116  inflate -> 6,119,116  (EXACT match)
    entry = 0x00100008
      seg1 target 0x006179FC  size 24      (.reginfo)
      seg2 target 0x00100000  size 5,339,668 (.text -- the engine)
      seg3 target 0x00617A80  size 779,132  (.data)
    header 292 B + segments 6,118,824 B = 6,119,116 B, the whole image. Nothing left over.

USAGE
    decode_core_gt4.py [CORE.GT4] [-o out.bin] [--emit-manifest manifest.json]
"""
import argparse
import hashlib
import json
import struct
import sys
import zlib


def decode(path):
    raw = open(path, "rb").read()
    flags, dec_size = struct.unpack_from("<HI", raw, 0)
    body = raw[6:]

    # GT4 Online's extra layer (not our disc): if the raw stream will not inflate, try Salsa20 first.
    image = None
    try:
        image = zlib.decompress(body, -15)
    except zlib.error:
        try:
            sys.path.insert(0, "/home/or/vulcan4/tools/oracle")
            from salsa20_gt4o import decrypt_gt4o  # only needed for GT4 Online discs
            image = zlib.decompress(decrypt_gt4o(raw), -15)
        except Exception as exc:
            raise SystemExit(f"not a raw DEFLATE CORE image and GT4O decrypt unavailable: {exc}")

    if len(image) != dec_size:
        print(f"WARNING: header says {dec_size} bytes, inflated {len(image)}", file=sys.stderr)

    off = 0
    rsa1_len = struct.unpack_from("<h", image, off)[0]; off += 2
    modulus = image[off:off + rsa1_len]; off += rsa1_len
    rsa2_len = struct.unpack_from("<h", image, off)[0]; off += 2
    rsa2 = image[off:off + rsa2_len]; off += rsa2_len
    nsec = struct.unpack_from("<i", image, off)[0]; off += 4
    entry = struct.unpack_from("<i", image, off)[0]; off += 4

    segments = []
    for _ in range(nsec):
        target, size = struct.unpack_from("<ii", image, off); off += 8
        segments.append({
            "target": target, "size": size,
            "sha256": hashlib.sha256(image[off:off + size]).hexdigest(),
            "data": image[off:off + size],
        })
        off += size

    return {
        "file": path, "raw_size": len(raw), "flags": flags, "declared_size": dec_size,
        "image": image, "entry": entry, "sections": nsec,
        "rsa1_len": rsa1_len, "rsa2_len": rsa2_len,
        "modulus_sha256": hashlib.sha256(modulus).hexdigest(),
        "rsa_value_sha256": hashlib.sha256(rsa2).hexdigest(),
        "image_sha512": hashlib.sha512(image).hexdigest(),
        "header_bytes": off, "segments": segments,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("core", nargs="?", default="/mnt/ssd/gt4/work/CORE.GT4")
    ap.add_argument("-o", "--out")
    ap.add_argument("--emit-manifest")
    a = ap.parse_args()

    d = decode(a.core)
    print(f"{d['file']}: raw={d['raw_size']} flags=0x{d['flags']:04x} "
          f"declared={d['declared_size']} inflated={len(d['image'])} "
          f"({'exact' if len(d['image']) == d['declared_size'] else 'MISMATCH'})")
    print(f"entry=0x{d['entry']:08x}  sections={d['sections']}  "
          f"header_bytes={d['header_bytes']}  image_sha512={d['image_sha512'][:32]}...")
    for i, s in enumerate(d["segments"], 1):
        print(f"  seg{i}: target=0x{s['target']:08x} size={s['size']} sha256={s['sha256'][:16]}...")

    if a.out:
        open(a.out, "wb").write(d["image"])
        print(f"wrote {a.out} ({len(d['image'])} bytes)")
    if a.emit_manifest:
        m = {k: v for k, v in d.items() if k not in ("image",)}
        m["segments"] = [{k: v for k, v in s.items() if k != "data"} for s in d["segments"]]
        open(a.emit_manifest, "w").write(json.dumps(m, indent=2))
        print(f"wrote {a.emit_manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
