# THE GUEST *DOES* OPEN core.gt4 — the retraction is now positively contradicted (2026-10-01, Caine)

This supersedes the "guest never asks" claim in `WALL-CDROM-GAMEDATA.md` and `WALL-INSTRUMENT.md`.
Not merely withdrawn as vacuous — **actively disproved from the guest's own binary.** Read the ELF,
not the log. Three independent strings died, and the call sites are unambiguous.

## What is in the ELF, with addresses

| string | file offset | runtime address |
|---|---|---|
| `core.gt4` | `0x03E1D8` | **`0x0103D1D8`** |
| `GAMEDATA` | `0x03E548` | **`0x0103D548`** |
| `IOPRP300` | `0x03E668` | `0x0103D668` (full `cdrom0:\IOPRP300.IMG;1` at `0x0103D660`) |

`vaddr = fileoff + 0x00FFF000`, consistent across both LOAD segments.

**`BASCUS-97328GAMEDATA`, `CORE.GT4` and `SYSTEM.CNF` return ZERO hits**, verified with partial
probes (`BASCUS`, `CORE`, `SYSTEM` all zero). **The uppercase spellings do not exist in the ELF.**
That kills the "GT4 opens `CORE.GT4`" shape outright, and it means the `mc0:\BASCUS-97328GAMEDATA`
string in the boot log is **not** coming from here as a literal.

## The call sites — this is the part that matters

There is exactly **one** reference to the `core.gt4` data, at `0x0102DC80`: a `FileName` vtable
whose slots 0/4/8 hold `"core.gt4"`, then `DISK`, `MCARD 0`, `MCARD 1`, `HOST`. It is reached by
`lui` + **`lw offset(reg)`**, *not* `lui/ori` — a search for the PIC-style pair returns nothing, and
that is not a tooling gap: `.reginfo` gives `ri_gp_value = 0x01049770` (in `.bss`, past the image)
and nothing ever writes `$gp`, so the binary is non-PIC and does not use that idiom at all.

The site is in the function at **`0x010002C8`** (an FP-heavy C++ constructor, 3-iteration loop with
`sdc1` register saves):

- `0x01000318` / `:1C` / `:24` — pull the three `"core.gt4"` pointers
- `0x01000338: jal 0x0101546C` — build the object
- **`0x0100036C: jal 0x010057A0` with `a0` = the literal `"core.gt4"`** — the first real open
  attempt, tested at `0x01000374`
- on failure, **`0x010057F4` with `a1 = 1`** — a second attempt, most likely the `cdrom0:\`-prefixed
  path
- `0x0100043C` — loops back to `0x0100030C`, i.e. **it retries**

**So the guest opens `core.gt4` at least twice and retries on failure.** The wall is therefore *not*
"the guest never asks". It asks, it fails, and it loops — which is exactly the shape a missing
`GAMEDATA` parent directory would produce, and exactly why the `H1` experiment in
`WALL-CDROM-GAMEDATA.md` is worth running after all.

## The ISO9660 question, answered

**No disc-filesystem parsing in the guest. Confirmed by exhaustive string search:** `CD001`,
`\x01CD001\x01` (PVD), `@CD001`, `CD-XA`, `SYSTEM.CNF`, `BOOT2`, `BASCUS`, `GT4.VOL`, `ISO`, `9660`
— **all zero hits, file-wide.** Disc access is purely *path string + syscall*. Reading the ISO
structures is the **BIOS's** job, and we run without a BIOS, so that path does not exist for us and
does not need to. This closes one hypothesis permanently.

Also: **`GAMEDATA` is referenced by nothing in `.text`.** It is very likely a data-format tag rather
than a directory name. That is a real possibility now, not a footnote — do not build a
`GAMEDATA/` directory on the assumption it is a path until the first open is actually observed.

## What is still unknown, and the one next step

The callees **`0x010057A0`** and **`0x010057F4`** are not disassembled. The ELF has **no symtab and no
strtab**, so the kernel call cannot be named from the guest alone. Disassembling those two functions
is the single most useful next step: it will name the actual open syscall and show what the retry
path does on failure.

Note the interaction with the instrument work in `WALL-INSTRUMENT.md`: a retry loop at
`0x0100036C` → `0x0100043C` is precisely the kind of thing that would have shown up in the syscall
tally had `sceOpen` not been recompiled as a forwarder. The bypass and the loop are the same fact
seen from two sides — which is a good reason to believe this one.

Scratch scripts live in `/tmp/gt4_step*.py`, the full report in `/tmp/gt4_disc_findings.md`. Nothing
in the repo was modified.
