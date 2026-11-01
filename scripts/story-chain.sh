#!/usr/bin/env bash
# Plays the whole story chain: the routes tests/autotest/route-full-NN-*.yaml in order, each one
# starting from the save the one before it wrote in slot_01 (the first one starts at the title
# screen with a new game). It stops at the first chunk that fails and prints one PASS/FAIL line per
# chunk. A failed chunk is played once more before it counts as failed (the routes are timed and
# flake on a loaded machine). docs/guides/prueba-vertical.md, "La cadena completa".
#
# usage: scripts/story-chain.sh --content <dir> --out <dir> [--from NN] [--to NN] [-- <more game
# options>]
#   --out   where each chunk keeps its log and captures (<out>/NN/) and the saves it starts from and
#   leaves
#           (<out>/saves/NN/: the slot after chunk NN). --from NN starts at chunk NN with
#           <out>/saves/<NN-1>/.
# env: PRESET (ci) the build to run, CHUNK_TIMEOUT (1500) seconds per chunk, CHAIN_RETRIES (1), SEED
# (1).
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
content=""
out=""
from=1
to=99
extra=()
while (($#)); do
  case "$1" in
    --content)
      content="$2"
      shift 2
      ;;
    --out)
      out="$2"
      shift 2
      ;;
    --from)
      from=$((10#$2))
      shift 2
      ;;
    --to)
      to=$((10#$2))
      shift 2
      ;;
    --)
      shift
      extra=("$@")
      break
      ;;
    *)
      sed -n '7,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 3
      ;;
  esac
done
if [[ -z "$content" || -z "$out" ]]; then
  sed -n '7,11p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 3
fi
mkdir -p "$out/saves"
out="$(cd "$out" && pwd)"
content="$(cd "$content" && pwd)"

failed=0
for route in "$root"/tests/autotest/route-full-[0-9][0-9]-*.yaml; do
  name="$(basename "$route" .yaml)"
  number="${name#route-full-}"
  number="${number%%-*}"
  chunk=$((10#$number))
  ((chunk < from || chunk > to)) && continue
  previous="$out/saves/$(printf '%02d' $((chunk - 1)))"
  load=()
  if ((chunk > 1)); then
    if [[ ! -d "$previous" ]]; then
      echo "full-$number FAIL: no save from chunk $(printf '%02d' $((chunk - 1))) in $previous"
      failed=1
      break
    fi
    load=(--load slot_01)
  fi
  for attempt in $(seq 0 "${CHAIN_RETRIES:-1}"); do
    run="$out/$number"
    saves="$out/saves/work-$number"
    rm -rf "$run" "$saves"
    mkdir -p "$run" "$saves"
    ((chunk > 1)) && cp -a "$previous/." "$saves/"
    start=$(date +%s)
    (cd "$root" && timeout "${CHUNK_TIMEOUT:-1500}" env PRESET="${PRESET:-ci}" scripts/dev.sh run \
      --content "$content" --lang en --seed "${SEED:-1}" --saves "$saves" "${load[@]}" \
      --autotest "$route" --shots "$run" "${extra[@]}" >"$run/run.log" 2>&1 </dev/null)
    rc=$?
    seconds=$(($(date +%s) - start))
    wall="$((seconds / 60))m$(printf '%02d' $((seconds % 60)))s"
    if ((rc == 0)); then
      rm -rf "$out/saves/$number"
      mv "$saves" "$out/saves/$number"
      echo "full-$number PASS $wall $name"
      break
    fi
    why="$(grep -o 'autotest: FAIL.*' "$run/run.log" | tail -1 | cut -c1-200)"
    [[ -z "$why" ]] && why="exit $rc (log $run/run.log)"
    if ((attempt < ${CHAIN_RETRIES:-1})); then
      echo "full-$number retry after $wall: $why (log kept in $run-failed/)"
      rm -rf "$run-failed"
      mv "$run" "$run-failed"
      continue
    fi
    echo "full-$number FAIL $wall $name: $why"
    failed=1
  done
  ((failed)) && break
done
exit "$failed"
