#!/usr/bin/env bash
# Cooks every pack folder in a package's packs/ (the team's packs: the game pack and the
# translations) with dwx cook and dwx pack into a .pak archive beside it, then removes the folder: a
# package ships the team's packs only as archives, and a package build loads only those
# (content::teamPacksFolder, docs/adr/0017-cocinado-y-archivos-pak.md). The packs hold nothing read
# from a disc, so any dwx of this commit cooks them the same, on any system.
#
# Usage: scripts/package/cook_team_packs.sh <packs folder> <dwx>
set -euo pipefail

[[ $# -eq 2 ]] || {
  echo "usage: $0 <packs folder> <dwx>" >&2
  exit 2
}
packs="$1"
dwx="$2"
work="$(mktemp -d "${TMPDIR:-/tmp}/dwe-cook-team-packs.XXXXXX")"
trap 'rm -rf "$work"' EXIT

count=0
for folder in "$packs"/*/; do
  folder="${folder%/}"
  name="$(basename "$folder")"
  [[ -f "$folder/pack.yaml" ]] || continue
  "$dwx" cook "$folder" -o "$work/$name" >/dev/null
  "$dwx" pack "$work/$name" -o "$packs/$name.pak" >/dev/null
  rm -rf "$folder"
  count=$((count + 1))
done
[[ -f "$packs/game.pak" ]] || {
  echo "cook_team_packs: no game pack in $packs" >&2
  exit 1
}
echo "cook_team_packs: $count packs cooked into archives in $packs"
