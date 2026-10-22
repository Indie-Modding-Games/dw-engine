# The gate's route jobs (scripts/gate.sh): which routes each tier runs, with what options, and how a job's runs are
# run and summed up. Sourced; scripts/gate/tests/test_follow_up.sh sources it too. It needs from its caller: root,
# logs, DWE_CONTENT, cooked_archive, routes_extra, die, and run_game <display> <log> <arguments separated by tabs>.

route_dir="tests/autotest"
# The smoke routes of the short tier, in en: boot, the main menu, a new game and the first map; the field and the menu;
# a save and its load.
smoke_routes=(route-loop-keyboard route-keyboard route-save-load)
# Routes that check what another one left, run right after it in the same job, so they stay together on one display
# when the routes run in parallel: the load of the save route's slot, from that run's own saves folder.
declare -gA follow_up_routes=([route-save-load]=route-save-load-check)
saved_slot="slot_01"
smoke_lang="en"
languages=(en es)
# The job list's separators: fields, and the arguments of a run.
field_separator="|"
argument_separator=$'\t'

# One summary line per run, in the team's format (errors= is the count the autotest prints).
route_summary() {
    local name="$1" rc="$2" log="$3" result validation errors warnings
    result=$(grep -o 'autotest: \(PASS\|FAIL\).*' "$log" | tail -1 | cut -c1-100)
    validation=$(grep -o 'validation layer on, [0-9]* messages' "$log" | tail -1)
    errors=$(grep -o 'autotest: [0-9]* \[error\] lines' "$log" | tail -1 | grep -o '[0-9]*')
    warnings=$(grep -c '\[warning\]' "$log")
    echo "$name rc=$rc | ${result:-autotest: NO RESULT} | ${validation:-validation: NONE} | errors=${errors:-?} | warnings=$warnings"
}

# job <name> <log> <arguments...>
job() {
    local name="$1" log="$2"
    shift 2
    local IFS="$argument_separator"
    echo "$name$field_separator$log$field_separator$*"
}

# follow_up <log> <arguments...>: the run that follows a job's run, appended to the job's line.
follow_up() {
    local log="$1"
    shift
    local IFS="$argument_separator"
    echo "$field_separator$log$field_separator$*"
}

# empty_saves_folder <folder>: deletes a pair's saves folder before its job runs, so a slot an earlier run left can never
# pass the check. Only a folder named saves inside this gate's logs (logs/<lang>/<route>/saves) is deleted; anything
# else, the player's saves among them, is refused with status 1.
empty_saves_folder() {
    local folder="$1" logs_root resolved
    [[ -n "${logs:-}" && -n "$folder" ]] || return 1
    logs_root="$(realpath -m "$logs")"
    resolved="$(realpath -m "$folder")"
    [[ "$resolved" == "$logs_root"/*/*/saves && "$resolved" != *saves-play* && ! -L "$folder" ]] || return 1
    rm -rf "$resolved"
}

# Whether a route only follows another one (follow_up_routes) and never runs alone.
is_follow_up() {
    local route="$1" first
    for first in "${!follow_up_routes[@]}"; do
        [[ "${follow_up_routes[$first]}" == "$route" ]] && return 0
    done
    return 1
}

# route_job <route> <lang> <suffix>: a route on the extracted content, with the flags it needs
# (docs/guides/prueba-vertical.md, "Las ejecuciones del gate").
route_job() {
    local route="$1" lang="$2" suffix="$3"
    local flags=(--content "$DWE_CONTENT")
    case "$route" in
        route-dev-assets) flags=(--dev-assets) ;;
        route-skins) flags+=(--mods tests/mods/ui_skin) ;;
        route-mod-map) flags+=(--mods tests/mods/example_mod) ;;
    esac
    local follow="${follow_up_routes[$route]:-}" saves="$logs/$lang$suffix/$route/saves" line
    if [[ -n "$follow" ]]; then
        empty_saves_folder "$saves" || die "refusing to empty the saves folder $saves"
        flags+=(--saves "$saves")
    fi
    line="$(job "$route-$lang$suffix" "$lang$suffix/$route.log" "${flags[@]}" --lang "$lang" --skip-steps fullscreen \
        --autotest "$route_dir/$route.yaml" --shots "$logs/$lang$suffix/$route")"
    if [[ -n "$follow" ]]; then
        line+="$(follow_up "$lang$suffix/$follow.log" --content "$DWE_CONTENT" --saves "$saves" --load "$saved_slot" \
            --lang "$lang" --skip-steps fullscreen --autotest "$route_dir/$follow.yaml" \
            --shots "$logs/$lang$suffix/$follow")"
    fi
    echo "$line"
}

write_short_jobs() {
    local suffix="$1" route lang
    for route in "${smoke_routes[@]}"; do
        route_job "$route" "$smoke_lang" "$suffix"
    done
    for route in ${routes_extra//,/ }; do
        is_follow_up "$route" && continue
        for lang in "${languages[@]}"; do
            [[ "$lang" == "$smoke_lang" && " ${smoke_routes[*]} " == *" $route "* ]] && continue
            route_job "$route" "$lang" "$suffix"
        done
    done
}

write_daily_jobs() {
    local suffix="$1" route lang
    for lang in "${languages[@]}"; do
        for route in $(cd "$root/$route_dir" && ls route-*.yaml | sed 's/\.yaml$//'); do
            is_follow_up "$route" && continue
            route_job "$route" "$lang" "$suffix"
        done
    done
    for lang in "${languages[@]}"; do
        job "cooked-route-mod-map-$lang$suffix" "cooked-mod-map-$lang$suffix.log" --content "$cooked_archive" \
            --mods tests/mods/example_mod --lang "$lang" --autotest "$route_dir/route-mod-map.yaml" \
            --skip-steps fullscreen
        job "cooked-route-fade-$lang$suffix" "cooked-fade-$lang$suffix.log" --content "$cooked_archive" \
            --text-font original --lang "$lang" --autotest "$route_dir/route-fade.yaml" --skip-steps fullscreen
    done
}

# A job: its run and, when the route has one (follow_up_routes), the run that follows it, only after a pass. Each run
# has its own summary line; a follow-up that could not run counts as one that did not pass.
run_job() {
    local index="$1" display="$2" name log rest follow_log follow_rest start rc follow_name
    IFS="$field_separator" read -r name log rest follow_log follow_rest < <(sed -n "${index}p" "$logs/jobs.txt")
    start=$SECONDS
    run_game "$display" "$log" "$rest"
    rc=$?
    route_summary "$name" "$rc" "$logs/$log" >"$logs/results/$index"
    if [[ -n "$follow_log" ]]; then
        follow_name="$(basename "$follow_log" .log)-$(dirname "$follow_log")"
        if ((rc == 0)); then
            run_game "$display" "$follow_log" "$follow_rest"
            route_summary "$follow_name" "$?" "$logs/$follow_log" >>"$logs/results/$index"
        else
            echo "$follow_name rc=? | NOT RUN: $name did not pass" >>"$logs/results/$index"
        fi
    fi
    echo "$name$field_separator$((SECONDS - start))$field_separator:$display" >"$logs/seconds/$index"
}

# The runs a job list makes: one per job, and one more for each job with a follow-up.
count_runs() {
    awk -F"$field_separator" '{ runs += (NF > 3) ? 2 : 1 } END { print runs + 0 }' "$1"
}
