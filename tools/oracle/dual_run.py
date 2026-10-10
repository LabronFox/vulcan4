#!/usr/bin/env python3
"""dual_run - stop BOTH machines at the same PC and print their state side by side.

WHY. VULCAN 4's law 13: *no wall without an oracle reading*. That rule has been obeyed by hand, one
wall at a time, and every manual differential took a morning. This is the wire that makes it one
command — the thing the previous crew never built:

  1. our recompiled runtime is run headless with the harness probe armed at <pc>
     (`VULCAN4_PC_PROBE="<pc>@<memaddr>:<len>"`, added 2026-10-10 / W280), which prints the GPRs and a
     window of RDRAM every time the guest reaches that instruction;
  2. the live PCSX2 (DebugServer, port 21512) is broken at the SAME pc, and its registers + the same
     memory window are read;
  3. the two are printed as a table, with the FIRST DIVERGENCE named — register, or byte offset.

That first divergence is the wall, as a value. Everything after it is consequence.

USAGE
    dual_run.py 0x5ae068 --mem 0x874300:64 [--entries 2000000] [--seconds 45] [--timeout 120]
    dual_run.py 0x5608e0 --mem 0x874304:32 --oracle-only     # just ask the real machine
    dual_run.py 0x5608e0 --mem 0x874304:32 --ours-only       # just ask ours

KNOWN LIMIT, measured 2026-10-10: the harness probe fires when the guest reaches that PC **as a
function entry** (the same hook the older W247 probe uses), so a mid-function anchor like 0x5ae068 is
reachable on the oracle but not yet on our side. Two ways forward: anchor on a real function entry, or
give the harness a true per-instruction hook. Until then, use `--oracle-only` for mid-function
addresses — a one-sided oracle reading is still a reading, and it is how the `ra=0x5b0d8c` match with
our runtime's own SIF log was established.

PREQUISITE for the oracle half: PCSX2 running with -debugger and the game loaded. It is resumed and
left paused on the breakpoint; re-arm/clear as needed with vg_oracle.py.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vg_oracle import Oracle  # noqa: E402

REPO = "/home/or/vulcan4"
RUN = "/mnt/ssd/vulcan4-build/run"
ENGINE = os.environ.get("VULCAN4_ENGINE_DIR", "/mnt/ssd/vulcan4-build/recomp_engine_r30")
GPR_NAMES = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
             "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
             "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]


def run_ours(pc, mem, length, entries, seconds, tag="dualrun"):
    spec = f"{pc:x}@{mem:x}:{length}" if mem is not None else f"{pc:x}"
    log = f"{RUN}/boot_{tag}.log"
    env = dict(os.environ, VULCAN4_PC_PROBE=spec)
    cmd = ["bash", "tools/harness/run_boot_named.sh", tag, str(entries), str(seconds)]
    subprocess.run(cmd, cwd=REPO, env=env, capture_output=True, text=True, timeout=entries and 900)
    hits = []
    with open(log, "r", errors="replace") as fh:
        for line in fh:
            if line.startswith("[pcprobe]"):
                hits.append(line.strip())
    return hits, log


def parse_ours(hit_line, length):
    regs = {}
    for m in re.finditer(r"\b(a[0-3]|v[01]|ra)=0x([0-9a-fA-F]+)", hit_line):
        regs[m.group(1)] = int(m.group(2), 16)
    words = []
    m = re.search(r"mem\[0x[0-9a-fA-F]+:\d+\]=(.*)$", hit_line)
    if m:
        words = [int(w, 16) for w in m.group(1).split() if w]
    return regs, words


def run_oracle(pc, mem, length, timeout):
    o = Oracle()
    try:
        o.send(cmd="set_breakpoint", address=hex(pc))
        o.send(cmd="resume")
        deadline = time.time() + timeout
        st = None
        while time.time() < deadline:
            time.sleep(3)
            st = o.send(cmd="status")
            if st.get("paused"):
                break
        if not st or not st.get("paused"):
            return None, None, "oracle never hit the breakpoint"
        regs_raw = o.send(cmd="read_registers")
        regs = {}
        for r in regs_raw.get("GPR", {}).get("regs", []):
            n = r.get("name")
            disp = r.get("display", "")
            # DebugServer renders a 128-bit register as "0xLOW.MID.HIGH.MID2" — the LOW 32 bits (the
            # value the MIPS code actually uses) are the FIRST field. Reading the LAST field instead
            # produced a table of zeros on 2026-10-10 and made a real hit look like a dead one.
            low = disp.split(".")[0] if disp else ""
            if low.lower().startswith("0x"):
                low = low[2:]
            if n and re.fullmatch(r"[0-9a-fA-F]{1,8}", low):
                regs[n] = int(low, 16)
        words = []
        if mem is not None:
            got = b""
            while len(got) < length:
                n = min(0x10000, length - len(got))
                d = o.send(cmd="read_memory", address=hex(mem + len(got)), length=n)
                got += bytes.fromhex(d.get("hex") or "")
            words = [int.from_bytes(got[i:i + 4], "little") for i in range(0, len(got) - 3, 4)]
        return regs, words, None
    finally:
        o.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pc")
    ap.add_argument("--mem", help="addr:len window to compare")
    ap.add_argument("--entries", type=int, default=2000000)
    ap.add_argument("--seconds", type=int, default=45)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--ours-only", action="store_true")
    ap.add_argument("--oracle-only", action="store_true")
    a = ap.parse_args()

    pc = int(a.pc, 16) if a.pc.lower().startswith("0x") else int(a.pc)
    mem = length = None
    if a.mem:
        addr, _, ln = a.mem.partition(":")
        mem = int(addr, 16) if addr.lower().startswith("0x") else int(addr)
        length = int(ln) if ln else 64

    print(f"=== dual_run  pc=0x{pc:08x}" + (f"  window=0x{mem:08x}:{length}" if mem is not None else ""))

    ours = oracle = None
    if not a.oracle_only:
        hits, log = run_ours(pc, mem, length or 0, a.entries, a.seconds)
        if not hits:
            print(f"OURS: the probe never hit 0x{pc:08x} (log {log})")
        else:
            ours = parse_ours(hits[0], length or 0)
            print(f"OURS : {hits[0]}")
    if not a.ours_only:
        regs, words, err = run_oracle(pc, mem, length or 0, a.timeout)
        if err:
            print(f"ORACLE: {err}")
        else:
            oracle = (regs, words)
            regs_s = " ".join(f"{k}=0x{v:08x}" for k, v in regs.items()
                              if k in ("a0", "a1", "a2", "a3", "v0", "v1", "ra"))
            print(f"ORACLE: pc=0x{pc:08x} {regs_s}")

    if ours and oracle:
        print("\n--- register diff (ours vs oracle) ---")
        for k in ("a0", "a1", "a2", "a3", "v0", "v1", "ra"):
            ov, hv = ours[0].get(k), oracle[0].get(k)
            if ov is None or hv is None:
                continue
            flag = "same" if ov == hv else "DIFFERS"
            print(f"  {k:3s}: ours=0x{ov:08x}  oracle=0x{hv:08x}  {flag}")
        if length:
            print("--- memory window diff ---")
            ow, hw = ours[1], oracle[1]
            first = next((i for i in range(min(len(ow), len(hw))) if ow[i] != hw[i]), None)
            if first is None:
                print(f"  identical for {min(len(ow), len(hw)) * 4} bytes")
            else:
                print(f"  FIRST DIVERGENCE at word {first} (0x{mem + first * 4:08x}): "
                      f"ours=0x{ow[first]:08x} oracle=0x{hw[first]:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
