#!/usr/bin/env python3
"""Generate runtime/syscall_names.h from the runtime's numeric syscall dispatcher.

The PS2 runtime's syscall dispatch is a switch in
    tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp
that maps a numeric syscall id ($v1) to a C++ handler. VULCAN 4's harness needs to *name*
the syscalls GT4 issues so it can produce a no-BIOS backlog, and we do not want a second,
hand-maintained copy of that table living in the runtime where it could silently drift.

So we transcribe the dispatcher's own switch into a generated header. Regenerate with:

    python3 tools/harness/gen_syscall_names.py

after changing the dispatcher. The generated header is NOT committed to the repository --
it is written into the upstream tree, which is gitignored, and is rebuilt as part of the
harness build (see docs/FIRST-BOOT.md).
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
DISPATCHER = REPO / "tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp"
OUTPUT = REPO / "tools/PS2Recomp/ps2xRuntime/include/runtime/syscall_names.h"

# Anything matching this is not a handler call.
NOT_HANDLERS = {
    "TODO", "dispatchNumericSyscall", "dispatchSyscall", "dispatchSyscallOverride",
    "return", "std", "if", "while", "for", "switch", "sizeof", "static_assert",
}

# Syscalls the runtime implements OUTSIDE the numeric dispatcher's switch, so the dispatcher
# cannot be their source of truth. 0x07 ExecPS2 lives in System.cpp's TODO() on purpose: it
# must not be a plain `return true` handler, because it has to stop the guest rather than let
# the emitted `jr $ra` resume the old frame. Without an entry here the harness prints it as
# `sce_unnamed_syscall`, which is how a wiring gap becomes invisible in a log.
EXTRA_NAMES = {0x07: "ExecPS2"}

# A case label, alone on its line:  case 0x79:   /   case static_cast<uint32_t>(-0x76):
# The static_cast form is the "i" (interrupt-disabled) alias of -0xNN, i.e. its two's
# complement id. Either form is optional-trailing-commented.
CASE_LABEL_RE = re.compile(
    r"^\s*case\s+"
    r"(?:static_cast<uint32_t>\(\s*-\s*(0x[0-9A-Fa-f]+|\d+)\s*\)|(0x[0-9A-Fa-f]+|\d+))"
    r"\s*:\s*(?://[^\n]*)?$"
)

# The body's first handler call:  sceSifSetReg(...)  /  ps2_stubs::sceSifSetReg(...)
# The namespace qualifier is optional and NOT captured -- the name we want is the last one.
CALL_RE = re.compile(
    r"^\s*(?:[A-Za-z_][A-Za-z_0-9]*\s*::\s*)*([A-Za-z_][A-Za-z_0-9]*)\s*\("
)

UINT32 = 1 << 32


def parse_names(source: str) -> tuple[dict[int, str], set[int]]:
    """Transcribe the dispatcher's switch into {syscall id: handler name}.

    Line-based on purpose. The previous single-regex form silently dropped every handler
    written `ps2_stubs::name(...)` (so 0x76..0x7B printed as unnamed) and every FALL-THROUGH
    label (`case 0x76:` / `case static_cast<...>(-0x76):` / one handler), and a case whose
    body is not a bare call is exactly where a log line loses its name. Labels accumulate
    until the first non-label call line; that call's name is assigned to all of them.
    """
    names: dict[int, str] = {}
    labelled: set[int] = set()
    pending: list[int] = []

    for line in source.splitlines():
        label = CASE_LABEL_RE.match(line)
        if label is not None:
            negative, positive = label.group(1), label.group(2)
            number = (-int(negative, 0)) % UINT32 if negative is not None else int(positive, 0)
            pending.append(number)
            labelled.add(number)
            continue

        call = CALL_RE.match(line)
        if call is None:
            continue
        handler = call.group(1)
        if handler in NOT_HANDLERS:
            # Not a handler: a control statement or the override probe. Do NOT drop the
            # pending labels -- the real handler call is still below them.
            continue
        # A numeric id can appear more than once (shared handler); first one wins.
        for number in pending:
            names.setdefault(number, handler)
        pending.clear()

    names.update(EXTRA_NAMES)
    return names, labelled


def main() -> int:
    if not DISPATCHER.is_file():
        print(f"STUCK: dispatcher not found at {DISPATCHER}", file=sys.stderr)
        return 1

    source = DISPATCHER.read_text(encoding="utf-8", errors="replace")

    names, labelled = parse_names(source)

    if not names:
        print("STUCK: no syscall handlers parsed -- the dispatcher shape changed", file=sys.stderr)
        return 1

    # A NUMBERED label with no name is the silent failure this parser exists to stop: the guest
    # issues the syscall, the log calls it `sce_unnamed_syscall`, and a wiring gap looks like an
    # unknown. Report it loudly; do not fail the build -- an unnamed id is a fact, not an error.
    unnamed = sorted(labelled - set(names))
    if unnamed:
        print("WARNING: dispatcher cases with no handler name parsed: "
              + ", ".join(f"0x{n:02X}" for n in unnamed), file=sys.stderr)

    lines = [
        "// syscall_names.h - GENERATED by tools/harness/gen_syscall_names.py from",
        "//   tools/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Dispatcher.cpp",
        "//",
        "// Do not edit by hand. Regenerate if the dispatcher changes.",
        "#pragma once",
        "#include <cstdint>",
        "",
        "struct SyscallName { uint32_t id; const char *name; };",
        "",
        "static const SyscallName kSyscallNames[] = {",
    ]
    for number in sorted(names):
        lines.append(f'    {{ 0x{number:02X}, "{names[number]}" }},')
    lines.append("};")
    lines.append("")
    lines.append(f"static const unsigned kSyscallNameCount = {len(names)};")
    lines.append("")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUTPUT} with {len(names)} syscall names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
