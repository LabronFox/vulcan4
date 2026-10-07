#!/usr/bin/env python3
"""W251. Independent check: does the emitted engine function set match Ghidra's ground truth?

Deliberately does NOT read the recompiler's own progress line ("W251 coverage: ..."), because a
check that believes the thing it is checking is not a check. It reads the two artifacts on disk:

  ground truth   the authoritative CSV (address, name, size, thunk, external)
  what was emitted   ps2_recompiled_functions.h in the emit dir -- one declaration per generated
                     function, named sub_<start>_0x<start>, plus register_functions.cpp's
                     distinct owning-function set

and reports both directions of set difference. Exit 0 only if every CSV address has an emitted
function. A gap is printed as a LOUD VULCAN 4 LIMITATION line, never as a silent count.

Usage: check_engine_symbols.py <engine-symbols.csv> <emit-dir> [--limit N]
"""
import re
import sys

DECL = re.compile(r"\bsub_[0-9a-f]{8}_(0x[0-9a-f]{1,8})\b")
LIMITATION = "VULCAN 4 LIMITATION: {what} — {why}"


def csv_addresses(path):
    """First CSV field of every data row, as ints. Header and junk rows are named, not dropped."""
    out, skipped = set(), []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for lineno, raw in enumerate(fh, 1):
            line = raw.strip()
            if not line:
                continue
            field = line.split(",")[0].strip().strip('"').strip()
            if field.lower().startswith("0x"):
                field = field[2:]
            try:
                out.add(int(field, 16))
            except ValueError:
                # Line 1 is the column header in every CSV this project has ever exported
                # (address,name,size,thunk,external). It is not a gap -- there is no function
                # named "address". Any OTHER unreadable row is a gap and gets a LOUD line.
                if lineno != 1:
                    skipped.append((lineno, line[:60]))
    return out, skipped


def emitted_addresses(emit_dir):
    """Starts declared in the emitted header. The register_functions.cpp set is cross-checked
    against it, so a header/table disagreement is caught here rather than at link time."""
    import os
    path = os.path.join(emit_dir, "ps2_recompiled_functions.h")
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        starts = {int(m.group(1), 16) for m in DECL.finditer(fh.read())}
    reg = os.path.join(emit_dir, "register_functions.cpp")
    if os.path.exists(reg):
        with open(reg, "r", encoding="utf-8", errors="replace") as fh:
            body = fh.read()
        named = set(re.findall(r"\bsub_[0-9a-f]{8}_(0x[0-9a-f]{1,8})\b", body))
        reg_starts = {int(a, 16) for a in named}
        if reg_starts != starts:
            print("W251 CHECK: header declares %d start(s), register_functions.cpp names %d"
                  % (len(starts), len(reg_starts)))
            for a in sorted(reg_starts - starts)[:20]:
                print("  in table, not declared: 0x%08x" % a)
            for a in sorted(starts - reg_starts)[:20]:
                print("  declared, not in table: 0x%08x" % a)
            return starts, reg_starts
    return starts, starts


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    limit = 20
    if "--limit" in argv:
        limit = int(argv[argv.index("--limit") + 1])

    truth, skipped = csv_addresses(argv[1])
    starts, _ = emitted_addresses(argv[2])

    for lineno, text in skipped:
        print(LIMITATION.format(
            what="CSV line %d is not an address (%r)" % (lineno, text),
            why="the authoritative list is the input to the whole emit; a row the parser "
                "cannot read is a function that will not exist"))

    missing = sorted(truth - starts)
    extra = sorted(starts - truth)

    print("W251 CHECK: csv=%d emitted=%d matched=%d" % (len(truth), len(starts), len(truth & starts)))
    for a in missing[:limit]:
        print(LIMITATION.format(
            what="authoritative entry point 0x%08x has no emitted function" % a,
            why="Ghidra found a function here; the emitted image cannot be entered at this address"))
    if len(missing) > limit:
        print("  ... and %d more (use --limit)" % (len(missing) - limit))
    for a in extra[:limit]:
        print("W251 CHECK: emitted function 0x%08x is not a CSV start (boundary the emit added)" % a)
    if len(extra) > limit:
        print("  ... and %d more" % (len(extra) - limit))

    if missing:
        print("W251 CHECK: FAILED -- %d of %d authoritative entry points are not emitted"
              % (len(missing), len(truth)))
        return 1
    print("W251 CHECK: PASSED -- every authoritative entry point is an emitted function")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
