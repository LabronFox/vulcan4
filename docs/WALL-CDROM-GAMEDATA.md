# THE LIVE WALL, MEASURED — not a mount bug (2026-10-01, Caine, after a 3-way research fan-out)

**I was wrong in `PRIOR-ART.md` and `PRIOR-ART-VERIFIED.md`.** Both told the chef to treat
`cdrom0:\GAMEDATA\...` as a *mount or prefix* question first. That is disproved by our own code.
The prefix machinery is sound. Read this before acting on either of those files.

## What the code actually does (verified by reading it, not by assuming)

- `PS2VfsMounts` (`ps2_vfs.h:18-23`) is three fixed roots, not a mount table: `hostRoot`, `cdRoot`,
  `memoryCard0Root`.
- `parsePs2Path` (`ps2_path.cpp:37-102`) matches device prefixes **case-insensitively, colon
  included** — `cdrom0:`, `CDROM0:` and a bare path all resolve to Cdrom.
- `normalizeSuffix` (`ps2_path.cpp:18-34`) already does `\` → `/` and strips an all-digit `;N`
  suffix. `FileIO.cpp:40` calls `vfs().open(ps2Path, flags, mounts, romDevice())`.
- `cdRoot` is set by `configureIoPathsFromElf` (`ps2_runtime.cpp:1067-1071`) to the ELF's own
  directory. With the documented boot line, **`cdRoot == /mnt/ssd/gt4/work`**, which is exactly
  where the extracted disc files live. `harness/vulcan4_harness.cpp:802` calls `loadELF` and never
  overrides it.
- So `cdrom0:\IRX\SIO2MAN.IRX;1` resolves — and it does, all five drivers load.
- `romDevice()` is NOT a disc. It is an in-memory `unordered_map` of BIOS ROM files
  (`ps2_rom_device.h:40`) whose default profile is a single 14-byte `ROMVER` string
  (`ps2_rom_device.cpp:166-172`). In the whole repo the only `registerProfile` caller is a test.
  **No BIOS is in the path, and none is needed.**

## The wall is TWO stacked problems, and the outer one is not a filesystem bug at all

**H2 — CONFIRMED, and it is the real blocker.** Measured on the 56 MB boot log
`/mnt/ssd/vulcan4-build/run/boot_span.log`:

| pattern | occurrences |
|---|---|
| `fioOpen` | **0** |
| `sceCdSearchFile` | **0** |
| `cdrom0:` | 5 — **all five are the IRX paths that succeed** |
| `CORE.GT4` | 0 |
| `GAMEDATA` | 20 |
| `core.gt4` | 7 |

The guest is **never asked to open `core.gt4`**. The only disc opens in the whole run are the five
IRX modules, which come in through `sceSifLoadModule`, a different path. So the wall is
"the guest never reaches the open", not "the open fails". Every minute spent on mounts, prefixes or
directory layout before that is spent in the wrong place.

**H1 — CONFIRMED, but it is the SECOND wall, not the first.** There is no `GAMEDATA` directory
anywhere under `/mnt/ssd/gt4`. `CORE.GT4` (2,020,861 B) is flat at the root of the work directory.
`resolveHostPath` (`ps2_vfs.cpp:300`) is pure lexical joining and `ps2_vfs.cpp:181-187` requires
`std::filesystem::exists`, so once the guest *does* ask, it gets `-1` for the missing parent.
`ps2_vfs.cpp` has no case-insensitive retry, no leaf-index fallback and no search-anywhere.

## The cheapest experiment that kills or confirms both, today

```sh
mkdir -p /mnt/ssd/gt4/work/GAMEDATA
ln -s ../CORE.GT4 /mnt/ssd/gt4/work/GAMEDATA/core.gt4
# re-run the harness, then: does any fioOpen line for cdrom0:\GAMEDATA appear?
```

If no `GAMEDATA` line ever appears in the log, H1 is moot and the wall is upstream of the
filesystem entirely. That is the fork in the road, and it costs one run.

**Note on the two chains disagreeing:** `Kernel/Stubs/CD.cpp:545-649` (`registerCdFile`) *does*
tolerate the missing directory — it tries a direct hit, then a case-insensitive component walk,
then a **recursive leaf-index fallback**, and that algorithm resolves
`cdrom0:\GAMEDATA\core.gt4;1` to `/mnt/ssd/gt4/work/CORE.GT4`. So the CD path and the VFS path
already return **different answers for the same string**. If GT4 uses both, that divergence is a
bug in its own right, and it is worth knowing which one it reaches first.

## What I did not check

- `IOPRP300.IMG` is **not** in the work directory, and `SYSTEM.CNF` declares only
  `BOOT2 = cdrom0:\SCUS_973.28;1`. No `cannot be served` message appears in the log and five IRX
  modules did load, so this is not currently biting — but it is a real gap.
- Whether the `mc0:\BASCUS-97328GAMEDATA` string (present in the log at `0x010519CD`) is a path or
  a record table is **still undecided**; `docs/HANDOFF.md:5910-5925` argues record table.
  `HANDOFF.md:5939` says decode the record at `0x010519C0` to see which field `$a2` points at.
- I have not touched the code. Findings only.
