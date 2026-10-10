# WIRING PLAN — GT4.VOL as a real filesystem (the streaming wall's data source)

Written 2026-10-10 (Caine). Why this exists: the file *load* lane is closed and proven; what remains is
**streaming**. Streamed assets (`.pss` movies, `.es`, `.sqt`, `.ins`, `.ads` scripts, `carsound`) are not
loose files on the disc — they live inside `GT4.VOL`, pulled by the IOP's PDI modules. Today our runtime
has no idea what is inside the volume: it serves whatever the guest asks for by path from the extracted
tree and knows nothing about **offsets inside the volume**, which is exactly what the streamer speaks.

The format is documented by the GT modding community (MIT, `nenkai.github.io/gt-modding-hub`) and the
extractor is open source (`github.com/Razer2015/GT4FS`, C#): **RoFS — a read-only b-tree filesystem**.

## The facts that matter (from the format doc, not from memory)

- **The first table of contents in the file is a DECOY** — a GT3-style ToC planted to crash old GT3
  tools. The real one is addressed by *page number*, one page = `0x800` bytes: **GT4 = page `0x2159`**.
  Anyone who trusts the start of the file extracts nothing and never learns why.
- Header (`0x40` bytes) carries: magic `RoFS`/`AC B9 90 AD` (a negated magic means "not encrypted"),
  version 3.1, compressed-ToC length, total page count, page length (`0x800`), ToC page count, then a
  page-offset table.
- **Page offsets are encrypted**: `offset = raw ^ (pageIndex * 0x14AC327A) + 0x14AC327A` (per the doc's
  snippet) — so a naive read gives garbage offsets, not an error.
- Each ToC page is **compressed (inflate) and XOR-encrypted with a single key byte `0x55`** (bit-flip)
  when the header magic is encrypted.
- Pages hold **b-tree nodes** (index pages / entry pages), always ordered for binary search.

## What wiring this in buys us

1. **A real `cdrom0:` for the volume**: resolve a streamed request (LBA/offset/length) to a file name,
   so the runtime can serve the game's *own* data instead of an HLE guess.
2. **An oracle for the streamer**: when the PDI modules ask for bytes at an offset, we can say
   *which file and which part* — which turns "the tag never arrives" into "the tag never arrives
   **because we answered offset 0x… with the wrong bytes**".
3. **It retires a class of speculation**: no more guessing what `CORE.GT4`-adjacent data the game wants.

## The work, in order

1. **`tools/analysis/gt4vol.py`** — a dependency-free Python RoFS reader: parse the header, decrypt the
   page offsets, inflate+XOR the ToC pages, walk the b-tree, and expose `list()` and
   `read(name) -> bytes`. Verify against `GT4FS extract` output for a handful of files (same SHA-256).
2. **Cross-check with the oracle**: at a breakpoint where the game issues a read, take the offset/length
   and ask the reader which file it falls in; confirm the file is one of the documented streamed types.
3. **Mount it**: expose the volume to the runtime's VFS so `cdrom0:`-style paths resolve from the volume
   (or by offset), and log every served request as a value — a streamer wall must be readable in the log.
4. **Then** revisit the SIF0/streamer wall (`docs/W279-TODAY-SIF0-ROUTING.md`) with real data behind it.

## Honest caveats

- `GT4FS` is C#/.NET; `dotnet` is **not installed on Cortex**, so the plan is a Python reader rather than
  running the original tool. The format doc is detailed enough to do that, but the doc's field names must
  be checked against real bytes — the extraction of a few files and a hash comparison is the test.
- The volume is **3,084,849,152 bytes** (the US v2.00 disc's `GT4.VOL`) — reads must be offset-based and
  never whole-file in memory.
