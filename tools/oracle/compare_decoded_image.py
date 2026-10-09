#!/usr/bin/env python3
"""compare_decoded_image - diff a decoded CORE.GT4 image against the LIVE PCSX2 ground truth.

WHY. VULCAN 4 law 13: a wall is not a wall without an oracle reading. The boot's whole job is to turn
CORE.GT4 (2,020,861 B on the disc) into a 6,119,116-byte image in EE RAM and jump to 0x00100008. So the
first question is never "where does the guest spin" -- it is "does OUR decoded image match the REAL
machine's, and if not, at which byte". This tool answers exactly that.

HOW. It decodes CORE.GT4 itself (tools/analysis/decode_core_gt4.py), reads the same three segments out
of the paused emulator's memory (DebugServer, port 21512), and compares them byte for byte, reporting
the first divergence offset per segment plus SHA-256 on both sides.

PRECONDITION. The oracle must be paused with the decoded image in place -- i.e. stopped at the image's
entry point. Get there with:

    vg_oracle.py bp 0x00100008 ; vg_oracle.py resume     # break on the decoded entry
    compare_decoded_image.py                            # then compare

MEASURED 2026-10-09 (PCSX2 paused at pc=0x00100008, cycles=1,633,677,773):
    seg1 0x006179fc       24 B  IDENTICAL
    seg2 0x00100000  5,339,668 B  IDENTICAL
    seg3 0x00617a80    779,132 B  IDENTICAL
    6,118,824 bytes compared, zero differences: our reading of the format is byte-exact, so any boot
    failure after this point is OUR RUNTIME's decode, not our understanding of the file.
"""
import hashlib
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "analysis"))
from vg_oracle import Oracle  # noqa: E402

CORE = "/mnt/ssd/gt4/work/CORE.GT4"


def decode(path=CORE):
    import decode_core_gt4  # noqa: WPS433  (local tool, kept dependency-free)
    return decode_core_gt4.decode(path)


def main():
    d = decode()
    image = d["image"]
    o = Oracle()
    off = 2 + d["rsa1_len"] + 2 + d["rsa2_len"] + 4 + 4
    ok = True
    print(f"entry=0x{d['entry']:08x}  sections={d['sections']}  image={len(image)} B")
    for i, s in enumerate(d["segments"], 1):
        off += 8                                   # target+size header for this segment
        seg = image[off:off + s["size"]]
        off += s["size"]
        got = b""
        while len(got) < s["size"]:
            n = min(0x10000, s["size"] - len(got))
            r = o.send(cmd="read_memory", address=hex(s["target"] + len(got)), length=n)
            chunk = bytes.fromhex(r.get("hex") or r.get("data") or "")
            if not chunk:
                break
            got += chunk
        got = got[:s["size"]]
        same = got == seg
        ok &= same
        fd = next((j for j in range(min(len(got), len(seg))) if got[j] != seg[j]), None)
        print(f"  seg{i} 0x{s['target']:08x} {s['size']:>9} B  "
              f"{'IDENTICAL' if same else f'DIFFERS at +0x{fd:x}'}"
              f"   (oracle {hashlib.sha256(got).hexdigest()[:20]} / ours {hashlib.sha256(seg).hexdigest()[:20]})")
    o.close()
    print("VERDICT:", "oracle == our decode" if ok else "MISMATCH -> the divergence above is the wall")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
