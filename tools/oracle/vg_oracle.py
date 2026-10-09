#!/usr/bin/env python3
"""vg_oracle - a scriptable client for the LIVE PCSX2 ground truth (VULCAN 4, law 13).

WHY THIS EXISTS. VULCAN 4's law 13 says no wall without an oracle reading: a wall must come with the
reference emulator's VALUE next to ours. The oracle was running on this box for weeks while the crew
worked from our own logs, because reading it meant going through an MCP server written for an AI
assistant over stdio. This is the direct path: newline-delimited JSON straight to PCSX2's DebugServer
(127.0.0.1:21512), so a shell script or a dish can ask the real machine a question in one line.

    vg_oracle.py status                     -> alive/paused/pc/cycles
    vg_oracle.py regs [ee|iop]              -> registers (128-bit)
    vg_oracle.py threads                    -> EE/IOP threads + state
    vg_oracle.py read 0x00874300 64         -> hex dump of guest memory
    vg_oracle.py eval 'v0 + 0x100'          -> expression with symbol support
    vg_oracle.py bp <addr> [cond]           -> set breakpoint
    vg_oracle.py bps | clearbps             -> list / clear breakpoints
    vg_oracle.py watch <addr> [onchange|read|write]  -> memory watchpoint
    vg_oracle.py continue | pause | step | stepover
    vg_oracle.py disasm <addr> [n]
    vg_oracle.py modules                    -> loaded IOP modules
    vg_oracle.py backtrace
    vg_oracle.py send '<json cmd>'          -> raw escape hatch

PROTOCOL. Requests are `{"cmd": "...", ...}` + "\\n"; replies are one JSON object + "\\n":
`{"ok": true, "data": {...}}` or `{"ok": false, "error": "..."}`. Verified against the live
DebugServer on 2026-10-09 (this file's `status` call answered on the first try).
"""
import json
import socket
import sys

HOST = "127.0.0.1"
PORT = 21512
TIMEOUT = 8.0


class Oracle:
    def __init__(self, host=HOST, port=PORT, timeout=TIMEOUT):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buf = b""

    def send(self, **cmd):
        self.sock.sendall((json.dumps(cmd) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("DebugServer closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        resp = json.loads(line.decode("utf-8", "replace"))
        if not resp.get("ok", False):
            raise RuntimeError(resp.get("error", f"command failed: {cmd}"))
        return resp.get("data", resp)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def _hexdump(data_hex, base=0, width=16):
    raw = bytes.fromhex(data_hex)
    out = []
    for off in range(0, len(raw), width):
        chunk = raw[off:off + width]
        hexpart = " ".join(f"{b:02x}" for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {base + off:08x}  {hexpart:<{width * 3}}  {text}")
    return "\n".join(out)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    o = Oracle()
    try:
        if cmd == "status":
            print(json.dumps(o.send(cmd="status", cpu=(argv[2] if len(argv) > 2 else "ee")), indent=2))
        elif cmd == "regs":
            print(json.dumps(o.send(cmd="read_registers", cpu=(argv[2] if len(argv) > 2 else "ee")), indent=2))
        elif cmd == "threads":
            print(json.dumps(o.send(cmd="get_threads"), indent=2))
        elif cmd == "modules":
            print(json.dumps(o.send(cmd="get_modules"), indent=2))
        elif cmd == "read":
            addr = int(argv[2], 16) if argv[2].lower().startswith("0x") else int(argv[2])
            n = int(argv[3]) if len(argv) > 3 else 64
            d = o.send(cmd="read_memory", address=hex(addr), length=n)
            payload = d.get("hex") or d.get("data") or d.get("bytes") or ""
            print(_hexdump(payload, addr) if payload else json.dumps(d)[:2000])
        elif cmd == "eval":
            print(json.dumps(o.send(cmd="evaluate", expression=argv[2],
                                     cpu=(argv[3] if len(argv) > 3 else "ee")), indent=2))
        elif cmd == "bp":
            args = {"cmd": "set_breakpoint", "address": argv[2]}
            if len(argv) > 3:
                args["condition"] = argv[3]
            print(json.dumps(o.send(**args), indent=2))
        elif cmd == "bps":
            print(json.dumps(o.send(cmd="list_breakpoints"), indent=2))
        elif cmd == "clearbps":
            print(json.dumps(o.send(cmd="clear_all_breakpoints"), indent=2))
        elif cmd == "watch":
            args = {"cmd": "set_watchpoint", "address": argv[2],
                    "type": (argv[3] if len(argv) > 3 else "onchange")}
            print(json.dumps(o.send(**args), indent=2))
        elif cmd in ("resume", "continue", "pause", "step", "stepover"):
            # The DebugServer's own verb is `resume`; "continue" is NOT a command it knows
            # (measured 2026-10-09: the wrong verb is how a probe sequence got out of order and
            # ended with a dead emulator). "continue" is accepted here as a human alias only.
            wire = {"continue": "resume", "stepover": "step_over"}.get(cmd, cmd)
            print(json.dumps(o.send(cmd=wire), indent=2))
        elif cmd == "disasm":
            print(json.dumps(o.send(cmd="disassemble", address=argv[2],
                                     count=int(argv[3]) if len(argv) > 3 else 8), indent=2))
        elif cmd == "backtrace":
            print(json.dumps(o.send(cmd="get_backtrace"), indent=2))
        elif cmd == "send":
            print(json.dumps(o.send(**json.loads(argv[2])), indent=2))
        else:
            print(f"unknown command: {cmd}")
            return 2
    finally:
        o.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
