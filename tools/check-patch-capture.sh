#!/bin/bash
# Law 8 capture check: for every file in the live numstat, the patch must carry
# AT LEAST that many added (+) lines.
set -u
PATCH="$1"; REPO="$2"
cd "$REPO" || exit 2
git diff HEAD --numstat > /mnt/ssd/tmp/.live_numstat
awk '/^diff --git /{f=$3; sub(/^a\//,"",f)} /^\+/ && !/^\+\+\+/ {c[f]++} END{for(k in c) printf "%d %s\n", c[k], k}' "$PATCH" | sort -k2 > /mnt/ssd/tmp/.patch_numstat
fail=0
printf "%-52s %8s %8s %s\n" FILE LIVE_ADD PATCH_ADD VERDICT
while read -r add del file; do
  want="$add"
  have=$(awk -v f="$file" '$2==f{print $1}' /mnt/ssd/tmp/.patch_numstat)
  have="${have:-0}"
  if [ "$have" -ge "$want" ]; then v=OK; else v="SHORT"; fail=1; fi
  printf "%-52s %8s %8s %s\n" "$file" "$want" "$have" "$v"
done < /mnt/ssd/tmp/.live_numstat
echo "----"
if [ "$fail" -eq 0 ]; then echo "CAPTURE CHECK: PASS (every file's added lines are carried)"; else echo "CAPTURE CHECK: FAIL"; fi
exit $fail
