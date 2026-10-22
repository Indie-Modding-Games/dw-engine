#!/usr/bin/env bash
# Tests of the follow-up routes (scripts/gate/route_jobs.sh, as scripts/gate.sh sources it): the check of the save route
# never runs alone, runs right after the save route in the same job only when it passed, and its saves folder starts
# empty. The runs of the game are stubbed: run_game writes the log a route would.
set -uo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
# shellcheck source=../route_jobs.sh
source "$root/scripts/gate/route_jobs.sh"

failures=0
check() {
    local description="$1"
    shift
    if "$@"; then echo "ok: $description"; else echo "FAILED: $description"; failures=$((failures + 1)); fi
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

# The routes whose run fails, by route name (the stub's answer); every other run passes.
failing_routes=""
run_game() {
    local log="$2" route
    route="$(basename "$log" .log)"
    mkdir -p "$(dirname "$logs/$log")"
    if [[ " $failing_routes " == *" $route "* ]]; then
        echo "[error] autotest: FAIL: step 1 of 2 (wait) failed" >"$logs/$log"
        return 1
    fi
    printf '%s\n' "validation layer on, 0 messages" "autotest: 0 [error] lines in the log" "autotest: PASS" >"$logs/$log"
    return 0
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

# run_all <jobs file>: runs every job as the gate does and writes the summary it would, one line per run.
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

check "gate.sh sources the route jobs this test checks" grep -q 'source "$root/scripts/gate/route_jobs.sh"' \
    "$root/scripts/gate.sh"
check "the save route is a smoke route" test " ${smoke_routes[*]} " != "${smoke_routes[*]/route-save-load/}"
check "its follow-up is the check route" test "${follow_up_routes[route-save-load]}" == "route-save-load-check"

# The job lists: the check route is never a job of its own, not even when asked for by name.
new_logs short
routes_extra="route-save-load-check,route-save-load"
write_short_jobs "" >"$logs/jobs.txt"
check "the short tier lists no job of the check route" bash -c "! grep -q '^route-save-load-check' '$logs/jobs.txt'"
check "  and the save route once per language, en from the smoke list" \
    test "$(job_names "$logs/jobs.txt" | grep -c '^route-save-load-')" == 2
new_logs daily
routes_extra=""
write_daily_jobs "" >"$logs/jobs.txt"
check "the daily tier lists no job of the check route" bash -c "! grep -q '^route-save-load-check' '$logs/jobs.txt'"
check "  and the save route in en and es" test "$(job_names "$logs/jobs.txt" | grep -c '^route-save-load-')" == 2

# The pair's job line: the save run, then the check with the same saves folder and the saved slot.
new_logs pair
mkdir -p "$logs/en/route-save-load/saves"
echo "stale" >"$logs/en/route-save-load/saves/slot_01.yaml"
write_short_jobs "" >"$logs/jobs.txt"
pair_line="$(grep '^route-save-load-en' "$logs/jobs.txt" | tr '\t' ' ')"
check "the pair's job has the save run and its follow-up" test "$(awk -F"$field_separator" '{ print NF }' \
    <<<"$pair_line")" == 5
check "  the save run saves to the job's own folder" grep -q -- "--saves $logs/en/route-save-load/saves --lang en" \
    <<<"$pair_line"
check "  the check loads the saved slot from that folder" grep -q -- \
    "--saves $logs/en/route-save-load/saves --load slot_01 .*route-save-load-check.yaml" <<<"$pair_line"
check "  a slot an earlier run left is gone before the job runs" test ! -e "$logs/en/route-save-load/saves"
check "the runs are one per job and one per follow-up" test "$(count_runs "$logs/jobs.txt")" == 4

# A passing pair: two summary lines, both passes.
run_all "$logs/jobs.txt"
check "a passing save route gives two summary lines" test "$(grep -c '^route-save-load' "$logs/summary.txt")" == 2
check "  the save run's" grep -q '^route-save-load-en rc=0 | autotest: PASS' "$logs/summary.txt"
check "  and the check's" grep -q '^route-save-load-check-en rc=0 | autotest: PASS' "$logs/summary.txt"
check "  and every run of the list has its line" test "$(grep -c ' rc=' "$logs/summary.txt")" == \
    "$(count_runs "$logs/jobs.txt")"

# A failing save route: the check does not run and counts as a run that did not pass.
new_logs failing
failing_routes="route-save-load"
write_short_jobs "" >"$logs/jobs.txt"
run_all "$logs/jobs.txt"
check "a failing save route fails its line" grep -q '^route-save-load-en rc=1 | autotest: FAIL' "$logs/summary.txt"
check "  and its check is NOT RUN with rc=?" grep -q '^route-save-load-check-en rc=? | NOT RUN' "$logs/summary.txt"
check "  which the gate counts as a run" test "$(grep -c ' rc=' "$logs/summary.txt")" == 4
check "  that did not pass" test "$(grep -cE ' rc=0 \| autotest: PASS' "$logs/summary.txt")" == 2
check "  and the check never ran" test ! -e "$logs/en/route-save-load-check.log"
failing_routes=""

# The saves folder that is emptied: only a pair's folder inside this gate's logs.
new_logs guard
mkdir -p "$logs/en/route-save-load/saves" "$scratch/saves-play" "$logs/en/saves-play/saves"
check "a pair's saves folder in the logs is emptied" empty_saves_folder "$logs/en/route-save-load/saves"
check "an empty name is refused" bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '')"
refused() {
    ! empty_saves_folder "$1" && test -d "$1"
}
check "a folder outside the logs is refused and kept" refused "$scratch/saves-play"
check "a saves-play folder is refused and kept" refused "$logs/en/saves-play/saves"
check "the logs folder itself is refused and kept" refused "$logs"
check "a path climbing out with .. is refused" bash -c "! (logs='$logs'; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$logs/en/../../saves-play')"
check "  and keeps it" test -d "$scratch/saves-play"
logs=""
check "no logs folder refuses everything" bash -c "! (logs=''; source '$root/scripts/gate/route_jobs.sh'; \
    empty_saves_folder '$scratch/guard/en/route-save-load/saves')"

((failures == 0)) || { echo "$failures failed"; exit 1; }
echo "all passed"
