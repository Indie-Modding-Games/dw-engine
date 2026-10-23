#!/usr/bin/env bash
# Tests of the follow-up routes (scripts/gate/route_jobs.sh, as scripts/gate.sh sources it): the
# check of the save route never runs alone, runs right after the save route in the same job only
# when it passed, and its saves folder starts empty. The runs of the game are stubbed: run_game
# writes the log a route would.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck source=../route_jobs.sh
source "$root/scripts/gate/route_jobs.sh"

failures=0
check() {
  local description="$1"
  shift
  if "$@"; then echo "ok: $description"; else
    echo "FAILED: $description"
    failures=$((failures + 1))
  fi
}

die() {
  echo "die: $*" >&2
  exit 1
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
DWE_CONTENT="$scratch/content"
cooked_archive="build/gate-cooked/archive"
routes_extra=""

# The routes whose run fails, by route name (the stub's answer); every other run passes. Each log
# names the display its run had, and a run takes run_seconds, so the workers of a parallel test
# overlap.
failing_routes=""
run_seconds=0
run_game() {
  local display="$1" log="$2" route
  route="$(basename "$log" .log)"
  mkdir -p "$(dirname "$logs/$log")"
  sleep "$run_seconds"
  if [[ " $failing_routes " == *" $route "* ]]; then
    printf '%s\n' "display :$display" "[error] autotest: FAIL: step 1 of 2 (wait) failed" \
      >"$logs/$log"
    return 1
  fi
  printf '%s\n' "display :$display" "validation layer on, 0 messages" \
    "autotest: 0 [error] lines in the log" "autotest: PASS" >"$logs/$log"
  return 0
}

# The display a run's log names.
run_display() {
  sed -n 's/^display ://p' "$logs/$1"
}

# new_logs <name>: a fresh logs folder of a gate, with the folders run_job writes into.
new_logs() {
  logs="$scratch/$1"
  mkdir -p "$logs/results" "$logs/seconds"
}

# The names of the jobs of a list: the first field of each line.
job_names() {
  cut -d"$field_separator" -f1 "$1"
}

# run_all <jobs file>: runs every job as the gate does and writes the summary it would, one line per
# run.
run_all() {
  local count index
  count="$(wc -l <"$1")"
  for ((index = 1; index <= count; index++)); do
    run_job "$index" 99
  done
  for ((index = 1; index <= count; index++)); do
    cat "$logs/results/$index"
  done >"$logs/summary.txt"
}

# shellcheck disable=SC2016 # the line of gate.sh, word for word
check "gate.sh sources the route jobs this test checks" \
  grep -q 'source "$root/scripts/gate/route_jobs.sh"' \
  "$root/scripts/gate.sh"
check "the save route is a smoke route" \
  test " ${smoke_routes[*]} " != "${smoke_routes[*]/route-save-load/}"
check "its follow-up is the check route" \
  test "${follow_up_routes["route-save-load"]}" == "route-save-load-check"

# The job lists: the check route is never a job of its own, not even when asked for by name.
new_logs short
routes_extra="route-save-load-check,route-save-load"
write_short_jobs "" >"$logs/jobs.txt"
check "the short tier lists no job of the check route" \
  bash -c "! grep -q '^route-save-load-check' '$logs/jobs.txt'"
check "  and the save route once per language, en from the smoke list" \
  test "$(job_names "$logs/jobs.txt" | grep -c '^route-save-load-')" == 2
new_logs daily
routes_extra=""
write_daily_jobs "" >"$logs/jobs.txt"
check "the daily tier lists no job of the check route" \
  bash -c "! grep -q '^route-save-load-check' '$logs/jobs.txt'"
check "  and the save route in en and es" \
  test "$(job_names "$logs/jobs.txt" | grep -c '^route-save-load-')" == 2

# The pair's job line: the save run, then the check with the same saves folder and the saved slot.
new_logs pair
mkdir -p "$logs/en/route-save-load/saves"
echo "stale" >"$logs/en/route-save-load/saves/slot_01.yaml"
write_short_jobs "" >"$logs/jobs.txt"
pair_line="$(grep '^route-save-load-en' "$logs/jobs.txt" | tr '\t' ' ')"
check "the pair's job has the save run and its follow-up" \
  test "$(awk -F"$field_separator" '{ print NF }' <<<"$pair_line")" == 5
check "  the save run saves to the job's own folder" \
  grep -q -- "--saves $logs/en/route-save-load/saves --lang en" <<<"$pair_line"
check "  the check loads the saved slot from that folder" grep -q -- \
  "--saves $logs/en/route-save-load/saves --load slot_01 .*route-save-load-check.yaml" \
  <<<"$pair_line"
check "  a slot an earlier run left is gone before the job runs" \
  test ! -e "$logs/en/route-save-load/saves"
check "the runs are one per job and one per follow-up" test "$(count_runs "$logs/jobs.txt")" == 4

# A route with saves of its own and no follow-up: its job saves to an empty folder of its own.
new_logs saves
mkdir -p "$logs/es/route-sleep-save/saves"
echo "stale" >"$logs/es/route-sleep-save/saves/slot_01.yaml"
routes_extra="route-sleep-save"
write_short_jobs "" >"$logs/jobs.txt"
routes_extra=""
saves_line="$(grep '^route-sleep-save-es' "$logs/jobs.txt" | tr '\t' ' ')"
check "a route with saves of its own runs once, with no follow-up" \
  test "$(awk -F"$field_separator" '{ print NF }' <<<"$saves_line")" == 3
check "  saving to the job's own folder" \
  grep -q -- "--saves $logs/es/route-sleep-save/saves --lang es" <<<"$saves_line"
check "  emptied before the job runs" test ! -e "$logs/es/route-sleep-save/saves"

# A passing pair: two summary lines, both passes.
run_all "$logs/jobs.txt"
check "a passing save route gives two summary lines" \
  test "$(grep -c '^route-save-load' "$logs/summary.txt")" == 2
check "  the save run's" grep -q '^route-save-load-en rc=0 | autotest: PASS' "$logs/summary.txt"
check "  and the check's" \
  grep -q '^route-save-load-check-en rc=0 | autotest: PASS' "$logs/summary.txt"
check "  and every run of the list has its line" test "$(grep -c ' rc=' "$logs/summary.txt")" == \
  "$(count_runs "$logs/jobs.txt")"

# A failing save route: the check does not run and counts as a run that did not pass.
new_logs failing
failing_routes="route-save-load"
write_short_jobs "" >"$logs/jobs.txt"
run_all "$logs/jobs.txt"
check "a failing save route fails its line" \
  grep -q '^route-save-load-en rc=1 | autotest: FAIL' "$logs/summary.txt"
check "  and its check is NOT RUN with rc=?" \
  grep -q '^route-save-load-check-en rc=? | NOT RUN' "$logs/summary.txt"
check "  which the gate counts as a run" test "$(grep -c ' rc=' "$logs/summary.txt")" == 4
check "  that did not pass" test "$(grep -cE ' rc=0 \| autotest: PASS' "$logs/summary.txt")" == 2
check "  and the check never ran" test ! -e "$logs/en/route-save-load-check.log"
failing_routes=""

# The parallel routes: workers on three displays take the daily list's jobs, the longest first as
# the gate orders them, and each pair still runs on one display, the check right after its save.
new_logs parallel
mkdir -p "$logs/claims"
write_daily_jobs "" >"$logs/jobs.txt"
job_count="$(wc -l <"$logs/jobs.txt")"
# The pairs first, as if they were the longest, so they start while the other workers are busy too.
{
  grep -n '^route-save-load-' "$logs/jobs.txt" | cut -d: -f1
  grep -vn '^route-save-load-' "$logs/jobs.txt" \
    | cut -d: -f1
} >"$logs/order.txt"
run_seconds=0.05
for display in 97 98 99; do
  worker "$display" &
done
wait
run_seconds=0
check "every job of the list ran once" \
  test "$(find "$logs/claims" -mindepth 1 | wc -l)" == "$job_count"
check "  and gave its runs' lines" \
  test "$(cat "$logs/results/"* | grep -c ' rc=0 | autotest: PASS')" == \
  "$(count_runs "$logs/jobs.txt")"
check "  on more than one display" \
  test "$(cut -d"$field_separator" -f3 "$logs/seconds/"* | sort -u | wc -l)" -gt 1
for lang in en es; do
  check "the $lang save and its check ran on one display" \
    test "$(run_display "$lang/route-save-load.log")" == \
    "$(run_display "$lang/route-save-load-check.log")"
  check "  the check after the save" test "$logs/$lang/route-save-load-check.log" -nt \
    "$logs/$lang/route-save-load.log"
done

# The saves folder that is emptied: only a pair's folder inside this gate's logs.
new_logs guard
mkdir -p "$logs/en/route-save-load/saves" "$scratch/saves-play" "$logs/en/saves-play/saves"
check "a pair's saves folder in the logs is emptied" \
  empty_saves_folder "$logs/en/route-save-load/saves"
check "an empty name is refused" \
  bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; empty_saves_folder '')"
refused() {
  ! empty_saves_folder "$1" && test -d "$1"
}
check "a folder outside the logs is refused and kept" refused "$scratch/saves-play"
check "a saves-play folder is refused and kept" refused "$logs/en/saves-play/saves"
check "the logs folder itself is refused and kept" refused "$logs"
check "a path climbing out with .. is refused" \
  bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$logs/en/../../saves-play')"
check "  and keeps it" test -d "$scratch/saves-play"
# Links: a saves folder that is a link, or a link to a folder on the way to it, is refused, wherever
# it leads.
mkdir -p "$scratch/elsewhere/route-x/saves" "$logs/es/route-y/saves" "$logs/en/route-z"
ln -s "$scratch/elsewhere/route-x/saves" "$logs/en/route-z/saves"
check "a saves folder that is a link is refused" \
  bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$logs/en/route-z/saves')"
check "  and keeps the folder it leads to" test -d "$scratch/elsewhere/route-x/saves"
ln -s "$scratch/elsewhere/route-x" "$logs/en/route-out"
check "a link on the way, out of the logs, is refused" bash -c "! (logs='$logs'; \
    source '$root/scripts/gate/route_jobs.sh'; empty_saves_folder '$logs/en/route-out/saves')"
check "  and keeps the folder it leads to" test -d "$scratch/elsewhere/route-x/saves"
ln -s "$logs/es/route-y" "$logs/en/route-in"
check "a link on the way, to another route's folder in the logs, is refused" \
  bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$logs/en/route-in/saves')"
check "  and keeps that folder" test -d "$logs/es/route-y/saves"
ln -s "$logs" "$scratch/logs-link"
check "a link above the logs folder, the logs reached through it, is not a link inside them" \
  bash -c "logs='$scratch/logs-link'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$scratch/logs-link/es/route-y/saves'"
check "  and empties the pair's folder" test ! -e "$logs/es/route-y/saves"
logs=""
check "no logs folder refuses everything" \
  bash -c "! (logs=''; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$scratch/guard/en/route-save-load/saves')"

((failures == 0)) || {
  echo "$failures failed"
  exit 1
}
echo "all passed"
