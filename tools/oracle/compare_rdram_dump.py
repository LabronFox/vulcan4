#!/usr/bin/env python3
"""compare_rdram_dump - diff a VULCAN 4 RDRAM dump against GT4's decoded CORE.GT4 image.

WHY. Two dumps answer two different questions, and mixing them up cost a day's worth of clarity:

    VULCAN4_RDRAM_DUMP              fires at HALT -- the engine has been running, so .data contains
                                    the guest's own writes and a diff cannot tell a bad decode from
                                    the game initialising itself.
    VULCAN4_RDRAM_DUMP_AT_HANDOFF   fires the moment the bootstrap gives control to 0x00100008 --
                                    here RDRAM must hold exactly the file's segments, nothing else.

Run this against the HAND-OFF dump to judge the decode. Against the halt dump it still tells you
which bytes the engine touched, which is its own useful signal.

USAGE
    compare_rdram_dump.py <dump.bin> [base_addr_hex]        # default base 0x100000
"""
import json
import sys

GOLD = "/mnt/ssd/gt4/work/CORE.GT4.dec.bin"
MANIFEST = "/mnt/ssd/gt4/work/CORE.GT4.manifest.json"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dump = open(sys.argv[1], "rb").read()
    base = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x100000
    gold = open(GOLD, "rb").read()
    man = json.load(open(MANIFEST))
    print(f"dump {sys.argv[1]}: {len(dump)} B at 0x{base:06x}   golden: {len(gold)} B")

    off = 2 + man["rsa1_len"] + 2 + man["rsa2_len"] + 4 + 4
    verdict_ok = True
    for i, s in enumerate(man["segments"], 1):
        off += 8
        seg = gold[off:off + s["size"]]
        off += s["size"]
        start = s["target"] - base
        got = dump[start:start + s["size"]] if 0 <= start < len(dump) else b""
        if len(got) != len(seg):
            print(f"  seg{i} 0x{s['target']:08x}: dump does not cover this segment "
                  f"(got {len(got)} of {len(seg)})")
            continue
        diffs = [j for j in range(len(seg)) if got[j] != seg[j]]
        first = f"+0x{diffs[0]:x} (addr 0x{s['target'] + diffs[0]:08x})" if diffs else "none"
        print(f"  seg{i} 0x{s['target']:08x} {s['size']:>9} B  diffbytes={len(diffs):>7}  first={first}")
        if diffs:
            verdict_ok = False
            z = sum(1 for j in diffs if seg[j] == 0 and got[j] != 0)
            m = sum(1 for j in diffs if got[j] == 0 and seg[j] != 0)
            b = len(diffs) - z - m
            print(f"        file=0 ours!=0: {z} (guest writes)   ours=0 file!=0: {m} (MISSING content)"
                  f"   both differ: {b}")
            j = diffs[0]
            print("        golden: " + seg[max(0, j - 8):j + 16].hex())
            print("        ours  : " + got[max(0, j - 8):j + 16].hex())
    print("VERDICT:", "decode is byte-identical to the file" if verdict_ok else "differences above")
    return 0 if verdict_ok else 1


if __name__ == "__main__":
    sys.exit(main())
