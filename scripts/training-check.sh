#!/usr/bin/env bash
# The training check's content level (docs/guides/comprobar-entrenamiento.md): every route in
# tests/autotest/training plays one whole session of a gym course on the real content, headless,
# and checks the partner's farthest point from its spot, the ground it covered, the session's frames
# and the stat gains against the original's bounds (the route's expect_range steps). Prints one
# line per course and a PASS/FAIL total; exit 1 if any failed.
#
# usage: scripts/dev.sh training-check --content <dir> [--out <dir>] [--only <pattern>]
#   --content  a content folder extracted with dwx (DWE_CONTENT by default)
#   --out      where each course keeps its log (default build/training-check)
#   --only     only the routes whose name matches the shell pattern (trai00-*, *-speed)
# env: PRESET (ci) the build to run, TRAINING_JOBS (4) courses at a time, each on an Xvfb display of
# its own from TRAINING_DISPLAY (90) up, TRAINING_TIMEOUT (900) seconds per course
# (the defense course on icsa03 alone plays about 11 minutes).
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
content="${DWE_CONTENT:-}"
out="$root/build/training-check"
only="*"
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
    --only)
      only="$2"
      shift 2
      ;;
    *)
      sed -n '8,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 3
      ;;
  esac
done
if [[ -z "$content" || ! -d "$content" ]]; then
  echo "training-check: no content folder (--content or DWE_CONTENT)" >&2
  exit 3
fi
content="$(cd "$content" && pwd)"
game="$root/build/${PRESET:-ci}/apps/game/dwe-game"
if [[ ! -x "$game" ]]; then
  echo "training-check: no game at $game (scripts/dev.sh build)" >&2
  exit 3
fi
jobs="${TRAINING_JOBS:-4}"
timeout_seconds="${TRAINING_TIMEOUT:-900}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# The longest routes first (their own timeout says how long they are), so the slots end together.
routes=()
while read -r _ route; do
  name="$(basename "$route" .yaml)"
  name="${name#route-training-}"
  # shellcheck disable=SC2053 # the pattern is a glob on purpose
  [[ "$name" == $only ]] && routes+=("$route")
done < <(for route in "$root"/tests/autotest/training/route-training-*.yaml; do
  echo "$(sed -n 's/^timeout: *//p' "$route" | head -1) $route"
done | sort -k1,1nr -k2)
if ((${#routes[@]} == 0)); then
  echo "training-check: no route matches $only" >&2
  exit 3
fi

# A free X display from `from` up: no socket and no lock file.
free_display() {
  local display="$1"
  while [[ -e "/tmp/.X11-unix/X$display" || -e "/tmp/.X$display-lock" ]]; do
    display=$((display + 1))
  done
  echo "$display"
}

# run_course <display> <route>: plays one route and writes its result line to <out>/<name>/result.
run_course() {
  local display="$1" route="$2" name map dir start rc seconds result line why
  name="$(basename "$route" .yaml)"
  name="${name#route-training-}"
  map="${name%%-*}"
  dir="$out/$name"
  rm -rf "$dir"
  mkdir -p "$dir/saves"
  start=$(date +%s)
  # The game binary straight, without scripts/dev.sh run and its validation layer: the check judges
  # the courses, and the layer slows every run down and fails it on messages of its own.
  (cd "$root" && DISPLAY=":$display" timeout "$timeout_seconds" \
    "$game" --content "$content" --lang en --size 640x360 \
    --new-game --new-game-chain seeded --seed 1 --start "$map" --saves "$dir/saves" \
    --autotest "$route" --shots "$dir" >"$dir/run.log" 2>&1 </dev/null)
  rc=$?
  seconds=$(($(date +%s) - start))
  line="$(grep -o 'gym: session .*' "$dir/run.log" | tail -1)"
  # The game's own exit status can carry a sanitizer's leak report: the route's verdict decides.
  if grep -q 'autotest: PASS' "$dir/run.log"; then
    result="PASS"
    why=""
  else
    result="FAIL"
    why="$(grep -o 'autotest: FAIL.*' "$dir/run.log" | tail -1 | cut -c1-160)"
    [[ -z "$why" ]] && why="exit $rc, no verdict (log $dir/run.log)"
  fi
  printf '%-16s %-4s %4ss  %s%s\n' "$name" "$result" "$seconds" "${line#gym: session }" \
    "${why:+  | $why}" >"$dir/result"
}

# The next route nobody has taken yet, or nothing when all are taken.
echo 0 >"$out/.next"
next_route() {
  local index
  {
    flock 9
    read -r index <"$out/.next"
    echo $((index + 1)) >"$out/.next"
  } 9>"$out/.lock"
  ((index < ${#routes[@]})) && echo "${routes[index]}"
}

# Each slot takes the next route on a display of its own until none is left.
slots=()
display="${TRAINING_DISPLAY:-90}"
total_start=$(date +%s)
for ((slot = 0; slot < jobs && slot < ${#routes[@]}; slot++)); do
  display="$(free_display "$display")"
  (
    Xvfb ":$display" -screen 0 1280x720x24 -nolisten tcp >"$out/xvfb-$display.log" 2>&1 &
    xvfb=$!
    trap 'kill "$xvfb" 2>/dev/null; wait "$xvfb" 2>/dev/null' EXIT
    while route="$(next_route)"; do
      run_course "$display" "$route"
    done
  ) &
  slots+=($!)
  # Give the display a moment, and the next slot another number.
  sleep 1
  display=$((display + 1))
done
for pid in "${slots[@]}"; do
  wait "$pid"
done
rm -f "$out/.next" "$out/.lock"

failed=0
passed=0
echo "course           ok   time   frames, offset, path, gains (hp mp off def spd brain)"
mapfile -t sorted < <(printf '%s\n' "${routes[@]}" | sort)
for route in "${sorted[@]}"; do
  name="$(basename "$route" .yaml)"
  name="${name#route-training-}"
  if [[ -f "$out/$name/result" ]]; then
    cat "$out/$name/result"
    if grep -q ' PASS ' "$out/$name/result"; then
      passed=$((passed + 1))
    else
      failed=$((failed + 1))
    fi
  else
    printf '%-16s FAIL no result (log %s)\n' "$name" "$out/$name/run.log"
    failed=$((failed + 1))
  fi
done
seconds=$(($(date +%s) - total_start))
elapsed="$((seconds / 60))m$(printf '%02d' $((seconds % 60)))s"
echo "training-check: $passed passed, $failed failed in $elapsed"
((failed == 0))
