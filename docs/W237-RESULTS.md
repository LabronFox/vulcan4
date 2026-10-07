# W237 — the giant "functions" are an entry-point SLICING defect, not a size problem

## 1. Ground truth (measured from `w231-engine.bin`)

The real MIPS functions at the addresses our tool merged are **tiny** — first `jr $ra`:
- `0x48EF90` → +0x14, `0x5A3140` → +0x1A8, `0x5ADF20` → +0x08, `0x269940` → +0x0C, `0x616D90` → +0x48.
- The image contains **21,130** `addiu sp,sp,-N` prologues (≈21 k functions in 5.34 MB, ≈253 B each).

So the engine is ~21 k small functions, exactly as expected.

## 2. What our tool emitted

The giants are not real functions — they are `entry_*` RESUME functions from the discovery, sliced
with multi-megabyte ranges:
```
entry_108040   range [0x108040, 0x48EF90)  = 3.7 MB     3.90 M lines (during emit)
entry_269940   1,046,632 emitted lines      real function is 3 instructions (jr ra at +0x0C)
entry_48efb0   1,349,426 emitted lines      (this is the block that made the TU 297 MB at cap=1)
```
The functions the crt0 actually calls (`sub_0048EF90` = 0x20 B, `sub_005ADF20` = tiny crt0, etc.) emit
normally.

## 3. Mechanism — entry-point SLICING, not function count / not fallbacks

`PS2RECOMP_MAX_FUNCTIONS` (cap the frontier) and `PS2RECOMP_NO_FALLBACKS` both failed (W236) because the
unit of the problem is an `entry_` slice: `discoverAdditionalEntryPointsImpl` / `collectInternalEntry
TargetsImpl` bound each resume function by `containingEnd` or `findNextBoundaryStart`, and when that
boundary set is sparse (or the containing function's `end` is itself far away) the slice spans
megabytes. `jr $ra` and the ~21 k prologues are NOT used as boundaries for resume slices.

Attempted fix (W237, `tools/patches/ps2recomp-linux-w237-entry-slice.patch`, OFF by default): run the
discovery BEFORE the reachability prune (so slices are computed against the full boundary set) and keep
`entry_` functions that lie inside a reachable range. **It did not fix it** — re-emit still produced a
217.5 MB TU with `entry_269940` = 1.05 M lines, i.e. the slice bound is still far away. So the defect
is deeper than the prune ordering: resume slices must be bounded by the **actual function boundaries**
(the 21 k prologues / `jr $ra` ends), which is an analyzer change in `control_flow_analyzer.cpp` /
`ps2_recompiler.cpp` slicing, not a recompiler size cap.

## 4. State

- Ground truth + mechanism named. No truncation used (per the rule). Reorder/entry-keep landed but is
  NOT sufficient.
- Compile a bounded frontier, second dispatch, boot: NOT reached (no compilable TU). Halt unchanged
  `execps2_unmapped_entry`; capture unchanged
  (`/mnt/ssd/vulcan4-build/run/w231b-capture.png`, disclaimer). Suite green; loader's 723 functions
  untouched; no generated output hand-edited; java/Minecraft never touched.

## 5. NEXT (exact)

In `discoverAdditionalEntryPointsImpl` / `resliceEntryFunctionsImpl`, bound every `entry_` slice by the
next **real** function boundary (the scan's starts, which already include the 21 k prologues), not by
`containingEnd` / section end. Then re-emit: `Generated functions` should be ~6.5 k with a normal
max-function size, and the ≤8 MB TU split + frontier compile + ExecPS2 dispatch wiring follow.
