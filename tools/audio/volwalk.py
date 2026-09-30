#!/usr/bin/env python3
"""Walk the GT4.VOL (PS2 packed-archive, magic 0xacb990ad) directory tree.

Prints the REAL tree: every entry with offset and size, measured from the disc.

Format, derived from the bytes (see docs/AUDIO-DECODE.md for the evidence):

  The volume header IS the root directory record.

    u32 magic        0xacb990ad
    u32 w1           0x00020002
    u32 w2, w3       volume size/offset words (meaning not decoded)
    u32 w4           0x00002f41 (unused)
    --- root directory record at +0x14 ---
    u32 nameOffset   0x0100ba61, decodes to "."   (XOR-0xFF)
    u32 count        number of children + 1  (0x17 = 23 -> 22 top-level dirs)
    u32 parent       0 at the root
    u32 children[]   count-1 absolute VOL offsets of the child records

  A record's name word carries FLAG bits in the top byte:
    0x01<<24 set   -> this record is a DIRECTORY
    0x02<<24 set   -> this record is a FILE
  and the real name offset is the low 24 bits. (0x01000000 here is a FLAG,
  NOT the PS2 user segment -- that burned wall W2, but it is a different
  field: the name table's flag bit. Both must be masked, differently.)

  DIRECTORY record: {nameOffset, count, parent, u32 children[count-1]}
                     -> variable length, records are NOT a fixed stride
  FILE record:      {nameOffset|0x02000000, f1, f2, f3}   (16 bytes)

Names are XOR-0xFF obfuscated and terminate at the decoded NUL.

Usage: volwalk.py [--path bgm/jp] [--files N] [--max-depth N] [--find SUB]
"""
import argparse
import struct
import sys

ISO = "/mnt/ssd/gt4/Gran Turismo 4 (USA) (v2.00).iso"
GT4_VOL_LBA = 105879
SECTOR = 2048
MAGIC = 0xACB990AD
ROOT = 0x14
ADDR_MASK = 0x00FFFFFF
FLAG_DIR = 0x01000000
FLAG_FILE = 0x02000000
VOL_SIZE = 2459502592

STATS = {"dirs": 0, "files": 0, "bytes": 0}


class Vol:
    def __init__(self, path=ISO):
        self.f = open(path, "rb")
        self.base = GT4_VOL_LBA * SECTOR
        self._nc = {}

    def rd(self, off, n):
        self.f.seek(self.base + off)
        return self.f.read(n)

    def u32(self, off):
        return struct.unpack("<I", self.rd(off, 4))[0]

    def name(self, w):
        o = w & ADDR_MASK
        if o not in self._nc:
            raw = self.rd(o, 0x200)
            dec = bytes(0xFF - b for b in raw)
            e = dec.find(b"\x00")
            self._nc[o] = dec[:e].decode("ascii", "replace")
        return self._nc[o]


def rec(v, off):
    """Read one record header. Returns (name, kind, f1, f2, f3)."""
    w, f1, f2, f3 = struct.unpack("<4I", v.rd(off, 16))
    kind = "FILE" if (w & FLAG_FILE) else "DIR"
    return v.name(w), kind, w, f1, f2, f3


def dir_children(v, off, count):
    ch = []
    for i in range(count - 1):
        p = v.u32(off + 12 + 4 * i)
        if p >= VOL_SIZE:
            break
        ch.append(p)
    return ch


def walk(v, off, depth, path, opts, seen):
    if off in seen:
        print("%s[CYCLE] @0x%08x" % ("  " * depth, off))
        return
    seen.add(off)
    name, kind, w, f1, f2, f3 = rec(v, off)

    if kind == "FILE":
        STATS["files"] += 1
        STATS["bytes"] += f2
        if depth <= opts.max_depth or opts.path.startswith(path):
            print("%s  FILE %-30s size=%-10d dataOff=0x%08x n=%-4d rec@0x%08x"
                  % ("  " * depth, name, f2, f3, f1, off))
        return

    ch = dir_children(v, off, f1) if 1 <= f1 <= 100000 else []
    STATS["dirs"] += 1
    here = path + "/" + name if path else name
    if depth <= opts.max_depth or opts.path.startswith(here):
        print("%s  DIR  %-30s entries=%-4d parent=0x%06x dir@0x%08x"
              % ("  " * depth, name, f1, f2, off))
    if opts.max_depth and depth >= opts.max_depth:
        return
    for c in ch:
        walk(v, c, depth + 1, here, opts, seen)


def descend(v, off, parts, opts, seen):
    """Follow an explicit path like bgm/jp."""
    if not parts:
        return
    name, kind, w, f1, f2, f3 = rec(v, off)
    if kind != "DIR":
        return
    for c in dir_children(v, off, f1) if 1 <= f1 <= 100000 else []:
        cn, ck, _cw, cf1, _cf2, _cf3 = rec(v, c)
        if cn == parts[0]:
            print("  -> %s (rec@0x%08x)" % (cn, c))
            descend(v, c, parts[1:], opts, seen)
            if parts[1:]:
                return
            walk(v, c, 1, (name if opts.path else "") + "/" + cn, opts, seen)
            return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--path", default="", help="explicit path, e.g. bgm/jp")
    ap.add_argument("--max-depth", type=int, default=2)
    ap.add_argument("--files", type=int, default=0)
    ap.add_argument("--find", default="")
    opts = ap.parse_args()

    v = Vol()
    h = struct.unpack("<6I", v.rd(0, 0x18))
    magic = h[0]
    nm, count, parent = struct.unpack("<3I", v.rd(ROOT, 12))
    print("GT4.VOL  LBA %d -> ISO file offset %d (0x%x)" % (GT4_VOL_LBA, v.base, v.base))
    print("header magic=0x%08x %s  w1=0x%08x w2=0x%08x w3=0x%08x w4=0x%08x"
          % (magic, "OK" if magic == MAGIC else "**MISMATCH**", h[1], h[2], h[3], h[4]))
    print("ROOT @0x%04x name=%r count=%d parent=0x%08x  (=> %d top-level dirs)"
          % (ROOT, v.name(nm), count, parent, count - 1))
    print("-" * 104)

    seen = set()
    if opts.path:
        descend(v, ROOT, opts.path.split("/"), opts, seen)
    else:
        for c in dir_children(v, ROOT, count):
            walk(v, c, 1, "", opts, seen)

    print("-" * 104)
    print("TOTALS: %d dirs, %d files, %d bytes (%.2f MiB) of file payload"
          % (STATS["dirs"], STATS["files"], STATS["bytes"], STATS["bytes"] / 1048576.0))


if __name__ == "__main__":
    sys.exit(main())
