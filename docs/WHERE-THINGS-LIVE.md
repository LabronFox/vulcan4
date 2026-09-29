# WHERE THINGS LIVE

**Rule: the root filesystem holds CODE and SYSTEM. Everything generated goes on the SSD.**

This exists because of a real failure, not tidiness. On **2026-09-29** the root filesystem hit
**100% (120 MB free)** and the symptom was a *code-looking bug*: PS2Recomp's `toml11`
FetchContent clone failed and the whole configure aborted. It looked like an MSVC/Linux
portability problem. It was a full disk. That cost a dish, a retry, and a wrong diagnosis in a
brief — so it is now written down instead of remembered.

## The map

| What | Where | Size / why |
|---|---|---|
| The repository (code, docs) | `/home/or/vulcan4` | small, versioned, on root |
| **Build trees** (CMake, objects, binaries) | **`/mnt/ssd/vulcan4-build`** | GB-scale, regenerable |
| **Recompiler output** (generated C++ from GT4's code) | **`/mnt/ssd/vulcan4-build/recomp`** | can be hundreds of MB of generated source |
| **Scratch** (`TMPDIR`, intermediate dumps) | **`/mnt/ssd/tmp`** | keeps `/tmp` from filling root |
| **The game dump** (ISO, extracted content) | `/mnt/ssd/gt4` | **never** inside the repo — see `.gitignore` |
| Session/agent data (opencode's own DB) | `~/.local/share/opencode` | ~257 MB, root; small enough to leave alone |

**Numbers at the time of writing:** root `/` = 178 G, **35 G free** · `/mnt/ssd` = 440 G,
**116 G free**.

## How to use it

```bash
# always give the build tree an explicit path outside the repo
cmake -S tools/PS2Recomp -B /mnt/ssd/vulcan4-build -DCMAKE_BUILD_TYPE=Release
cmake --build /mnt/ssd/vulcan4-build -j"$(nproc)"
```

The dish driver exports both, so every dish inherits them without thinking about it:

```
VULCAN4_BUILD=/mnt/ssd/vulcan4-build
TMPDIR=/mnt/ssd/tmp
```

`/mnt/ssd/**` is allowed in opencode's `permission.external_directory` — before 2026-09-29 it
was not, and the auto-reject *terminated the headless turn mid-task*, which is why a build tree
outside the repo was dangerous rather than merely untidy.

## The guard, before any big build

```bash
df -h / /mnt/ssd      # if either is near full, fix THAT first — it masquerades as a code bug
```

If root ever fills again, the fix is on this box (docker build cache, journal, stale `/tmp`
corpora), not in the build commands.
