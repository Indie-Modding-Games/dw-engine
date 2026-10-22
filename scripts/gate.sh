#!/usr/bin/env bash
# The gate of a tip (docs/guides/gate.md), under the machine-wide gate lock, in one of two tiers:
#   short  the push gate: dev.sh check-short (format, build, every test with DWE_DISC and DWE_CONTENT unset so those
#          on the disc and the content skip, clang-tidy of what changed, the disc tests named with both set), the
#          Windows cross build and the smoke routes, plus a package's own routes;
#   daily  the full run on main: dev.sh check-gate in full (check-full's steps, every test with the disc), the Windows
#          cross build, every route in en and es on the extracted content and the routes on the cooked .pak.
# The routes run one after another or on several Xvfb displays at once; either way each run writes one summary line,
# joined in a fixed order. Linux host only: it needs Docker, Xvfb and lavapipe.
#
# It stops only what it started: every Xvfb, route run and Docker step is recorded by PID (a run by the process group
# its timeout leads, a step by its sg's) and stopped by it on exit, never by name; the containers by this gate's prefix.
set -uo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dev="$root/scripts/dev.sh"
# shellcheck source=gate/build_dirs.sh
source "$root/scripts/gate/build_dirs.sh"
# shellcheck source=gate/route_jobs.sh
source "$root/scripts/gate/route_jobs.sh"

default_tier="short"
# The short tier's clang-tidy is incremental and the daily one's full; the daily cook is from scratch unless asked.
default_short_tidy="incremental"
default_daily_cook="full"
# The short tier does not cook (check-short has no cook step).
short_cook="none"
all_steps=(check windows routes)
# Parallel since two sequential and parallel pairs of one tip agreed, 2.9 to 3 times faster (docs/guides/gate.md);
# sequential stays for a bisect or a busy host.
default_routes="parallel"
default_displays=4
max_displays=4
# Displays the runner may take, and those it never takes: :71 is the reference emulator rig's, :61-:66 are assigned
# to other work.
display_pool_first=72
display_pool_last=79
reserved_displays=(61 62 63 64 65 66 71)
xvfb_screen="2560x1440x24"
xvfb_start_seconds=10
route_timeout_seconds=900
lock_poll_seconds=20
# The free space each step needs, in GB, added up over the chosen steps (docs/guides/gate.md has the measurements): the
# check a fresh build/ci (7.3) with the cook (2.0) and its cache entry (2.0); Windows little more than its folder holds
# already, as the short tier reuses it and the daily one deletes it before building again (4.4); the routes their logs
# and saves (under 0.1).
check_free_gb=12
windows_free_gb=2
routes_free_gb=1
lavapipe_icd="/usr/share/vulkan/icd.d/lvp_icd.json"
route_preset="ci"
game_binary="build/$route_preset/apps/game/dwe-game"
windows_preset="windows-cross"
windows_dir="$root/build/$windows_preset"
# Written by the gate after a Windows cross build of the folder with 0 warnings: the tip, the image, the .ninja_log hash.
windows_marker="$windows_dir/.dwe-gate-zero-warnings"
# The tools the steps in Docker run inside the image, checked before any step starts (the preflight). The routes run on
# the host: their preflight checks the host's programs and the lavapipe driver instead.
check_tools=(cmake ninja ctest ccache clang++ clang-format clang-tidy python3)
windows_tools=(cmake ninja clang-cl lld-link llvm-lib llvm-rc llvm-mt)
routes_host_programs=(Xvfb timeout)
main_branch="main"
dev_assets_file="$root/dev-assets.yaml"
cooked_archive="build/gate-cooked/archive"
cooked_font_line="ui: original bitmap font font/main"
pack_folders=(base ui_original)

usage() {
    cat <<USAGE
Usage: scripts/gate.sh [options]

  --tier short|daily        The push gate (default) or the daily full run
  --tidy full|incremental   short: the scope of clang-tidy (default $default_short_tidy); daily is always full
  --cook full|incremental   daily: cook from scratch (default $default_daily_cook) or reuse the cache
  --routes-extra LIST       short: comma-separated routes (names in $route_dir, without .yaml) to run in en and es too
  --disc-tests REGEX        short: the tests on the user's disc to run with it, a ctest regular expression (needs
                            DWE_DISC); check-short's full ctest runs with DWE_DISC and DWE_CONTENT unset, so the
                            other tests on the disc and the content skip
  --routes sequential|parallel
                            The runs on several displays at once, or one after another on one display, as for a
                            bisect (default $default_routes)
  --displays N              Displays of the parallel routes, 1-$max_displays (default $default_displays), taken from
                            :$display_pool_first-:$display_pool_last when free
  --lp-threads N            LP_NUM_THREADS of every run (lavapipe's threads); unset by default
  --steps LIST              Comma-separated steps among check,windows,routes (default all three)
  --repeat N                Run the route list N times (to measure flakes); default 1
  --lock DIR                The machine-wide gate lock, an atomic mkdir (default _work/logs/p4-locks/full.d next to
                            the main checkout when _work/logs exists, otherwise <state dir>/full.d)
  --logs DIR                Where the logs go (default _work/logs/daily-<date> or _work/logs/<branch>/gate-<time>
                            when _work/logs exists, otherwise build/gate-logs/<tier>-<time>)
  --tag NAME                The name written into the lock's owner file (default the branch)

Environment: DWE_CONTENT (required: the extracted content, with ${pack_folders[*]}), DWE_DISC (required by daily and
--disc-tests), DWE_IMAGE, DWE_JOBS and DWE_GATE_STATE_DIR as dev.sh reads them.
USAGE
}

die() {
    echo "gate.sh: $*" >&2
    exit 1
}

# --- options ------------------------------------------------------------------------------------------------------

tier="$default_tier"
tidy_mode=""
cook_mode=""
routes_extra=""
disc_tests=""
routes_mode="$default_routes"
display_count="$default_displays"
lp_threads=""
steps="$(IFS=,; echo "${all_steps[*]}")"
repeat=1
lock_dir=""
logs=""
tag="$(git -C "$root" rev-parse --abbrev-ref HEAD)"

parse_options() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --tier) tier="${2:-}" ;;
            --tidy) tidy_mode="${2:-}" ;;
            --cook) cook_mode="${2:-}" ;;
            --routes-extra) routes_extra="${2:-}" ;;
            --disc-tests) disc_tests="${2:-}" ;;
            --routes) routes_mode="${2:-}" ;;
            --displays) display_count="${2:-}" ;;
            --lp-threads) lp_threads="${2:-}" ;;
            --steps) steps="${2:-}" ;;
            --repeat) repeat="${2:-}" ;;
            --lock) lock_dir="${2:-}" ;;
            --logs) logs="${2:-}" ;;
            --tag) tag="${2:-}" ;;
            -h | --help)
                usage
                exit 0
                ;;
            *) die "unknown option $1 (see --help)" ;;
        esac
        [[ $# -ge 2 ]] || die "option $1 needs a value"
        shift 2
    done
    check_options
}

check_options() {
    [[ "$tier" =~ ^(short|daily)$ ]] || die "--tier must be short or daily"
    if [[ "$tier" == "short" ]]; then
        [[ -z "$cook_mode" ]] || die "--cook is for the daily tier, which cooks; the short one does not"
        tidy_mode="${tidy_mode:-$default_short_tidy}"
        cook_mode="$short_cook"
    else
        [[ -z "$tidy_mode" || "$tidy_mode" == "full" ]] || die "the daily tier always runs a full clang-tidy"
        [[ -z "$routes_extra$disc_tests" ]] || die "--routes-extra and --disc-tests are for the short tier"
        tidy_mode="full"
        cook_mode="${cook_mode:-$default_daily_cook}"
        [[ "$cook_mode" =~ ^(full|incremental)$ ]] || die "--cook must be full or incremental"
    fi
    [[ "$tidy_mode" =~ ^(full|incremental)$ ]] || die "--tidy must be full or incremental"
    [[ "$routes_mode" =~ ^(sequential|parallel)$ ]] || die "--routes must be sequential or parallel"
    [[ "$display_count" =~ ^[0-9]+$ ]] && ((display_count >= 1 && display_count <= max_displays)) ||
        die "--displays must be 1-$max_displays"
    [[ -z "$lp_threads" || "$lp_threads" =~ ^[1-9][0-9]*$ ]] || die "--lp-threads must be a positive number"
    [[ "$repeat" =~ ^[1-9][0-9]*$ ]] || die "--repeat must be a positive number"
    local step_name
    for step_name in ${steps//,/ }; do
        [[ " ${all_steps[*]} " == *" $step_name "* ]] || die "--steps takes ${all_steps[*]} separated by commas"
    done
    [[ "$routes_extra" =~ ^[a-z0-9,-]*$ ]] || die "--routes-extra takes route names separated by commas"
    local route
    for route in ${routes_extra//,/ }; do
        [[ -f "$root/$route_dir/$route.yaml" ]] || die "no route $route_dir/$route.yaml"
    done
    [[ "$routes_mode" == "sequential" ]] && display_count=1
    return 0
}

has_step() {
    [[ ",$steps," == *",$1,"* ]]
}

# The team's folder next to the main checkout (the parent of the repository's shared git folder), when it has logs.
team_logs_dir() {
    local common
    common="$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)" || return 1
    [[ -d "$(dirname "$(dirname "$common")")/_work/logs" ]] || return 1
    echo "$(dirname "$(dirname "$common")")/_work/logs"
}

# --- processes this run started -----------------------------------------------------------------------------------

pid_dir=""

# record <kind> <pid>: a process to stop on exit; a run is the process group its timeout leads, a step (a dev.sh command
# in Docker) the session its sg leads.
record_pid() {
    echo "$2" >"$pid_dir/$1-$2"
}

forget_pid() {
    rm -f "$pid_dir/$1-$2"
}

# A route run is `env ... timeout ... dev.sh run`: timeout leads a process group of its own (the game in it), which is
# stopped whole; right after the start, before timeout has made its group, only the process itself is.
stop_run() {
    local pid="$1" args="$2"
    [[ "$args" == timeout* || "$args" == env* ]] || return 0
    stop_group "$pid"
}

# Stops the process group that the process leads, or only the process when it leads none (yet).
stop_group() {
    local pid="$1"
    if [[ "$(ps -o pgid= -p "$pid" | tr -d ' ')" == "$pid" ]]; then
        kill -TERM -- "-$pid" 2>/dev/null
    else
        kill -TERM "$pid" 2>/dev/null
    fi
}

# Stops the recorded processes, each only after checking it is still the one started (its command line): the workers
# first, so none starts another run, then the runs, then the displays.
stop_recorded() {
    local kind file pid args
    [[ -d "$pid_dir" ]] || return 0
    for kind in worker run step xvfb; do
        for file in "$pid_dir/$kind"-*; do
            [[ -e "$file" ]] || continue
            pid="$(cat "$file")"
            stop_recorded_one "$kind" "$pid"
            rm -f "$file"
        done
    done
}

stop_recorded_one() {
    local kind="$1" pid="$2" args
    args="$(ps -o args= -p "$pid" 2>/dev/null)" || return 0
    case "$kind" in
        run) stop_run "$pid" "$args" ;;
        step) [[ "$args" == *"sg docker -c "* ]] && stop_group "$pid" ;;
        xvfb) [[ "$args" == Xvfb* ]] && kill -TERM "$pid" 2>/dev/null ;;
        worker) [[ "$args" == *gate.sh* ]] && kill -TERM "$pid" 2>/dev/null ;;
    esac
    return 0
}

# --- lock ---------------------------------------------------------------------------------------------------------

lock_taken=false

take_lock() {
    mkdir -p "$(dirname "$lock_dir")"
    echo "gate: waiting for the lock $lock_dir"
    # Another kind of gate lock next to it (a folder full-<name>) also holds the machine; files there are only files.
    until ! ls -d "$(dirname "$lock_dir")"/full-*/ >/dev/null 2>&1 && claim_lock; do
        sleep "$lock_poll_seconds"
    done
}

# The mkdir and the owner file with signals held back, so an interrupt cannot leave a lock that no one releases: a
# signal in between is acted on once the lock is recorded as taken.
claim_lock() {
    local signalled=false
    trap 'signalled=true' INT TERM
    if mkdir "$lock_dir" 2>/dev/null; then
        lock_taken=true
        printf 'tag: %s\ntip: %s\nstart: %s\npid: %s\n' "$tag" "$tip" "$(date -Is)" "$$" >"$lock_dir/owner"
    fi
    trap 'exit 130' INT TERM
    ! $signalled || exit 130
    $lock_taken
}

# Removes the lock this run created: its owner file names this run, or it has none (the run stopped before writing it).
release_lock() {
    $lock_taken || return 0
    if [[ ! -e "$lock_dir/owner" ]] || grep -qx "pid: $$" "$lock_dir/owner"; then
        rm -f "$lock_dir/owner" && rmdir "$lock_dir"
    fi
    lock_taken=false
}

dev_assets_written=false

clean_up() {
    stop_recorded
    stop_own_containers
    $dev_assets_written && rm -f "$dev_assets_file"
    release_lock
}

# --- displays -----------------------------------------------------------------------------------------------------

displays=()

is_reserved() {
    local reserved
    for reserved in "${reserved_displays[@]}"; do
        [[ "$1" == "$reserved" ]] && return 0
    done
    return 1
}

display_looks_free() {
    [[ ! -e "/tmp/.X$1-lock" && ! -e "/tmp/.X11-unix/X$1" ]]
}

# Starts Xvfb on a free display; Xvfb takes the display's lock itself, so a display taken meanwhile only fails here.
start_xvfb() {
    local number="$1" pid waited=0
    Xvfb ":$number" -screen 0 "$xvfb_screen" -nolisten tcp >"$logs/xvfb-$number.log" 2>&1 &
    pid=$!
    record_pid xvfb "$pid"
    while ((waited < xvfb_start_seconds)); do
        if ! kill -0 "$pid" 2>/dev/null; then
            forget_pid xvfb "$pid"
            return 1
        fi
        [[ -S "/tmp/.X11-unix/X$number" ]] && return 0
        sleep 1
        waited=$((waited + 1))
    done
    kill -TERM "$pid" 2>/dev/null
    forget_pid xvfb "$pid"
    return 1
}

take_displays() {
    local number
    for ((number = display_pool_first; number <= display_pool_last; number++)); do
        ((${#displays[@]} < display_count)) || break
        if is_reserved "$number"; then
            echo "gate: display :$number reserved, not used"
        elif ! display_looks_free "$number"; then
            echo "gate: display :$number in use, skipped"
        elif start_xvfb "$number"; then
            displays+=("$number")
        else
            echo "gate: display :$number did not start (taken meanwhile?), skipped"
        fi
    done
    ((${#displays[@]} > 0)) || die "no free display in :$display_pool_first-:$display_pool_last"
    ((${#displays[@]} == display_count)) ||
        echo "gate: only ${#displays[@]} of $display_count displays free; running on fewer"
}

# --- routes -------------------------------------------------------------------------------------------------------

# The job list, one per line: <name>|<log>|<arguments separated by tabs>, in the summary's fixed order.
write_jobs() {
    local pass suffix
    for ((pass = 1; pass <= repeat; pass++)); do
        suffix=""
        ((repeat > 1)) && suffix="-pass$pass"
        if [[ "$tier" == "short" ]]; then
            write_short_jobs "$suffix"
        else
            write_daily_jobs "$suffix"
        fi
    done
}

durations_file() {
    echo "$state_dir/route-seconds.txt"
}

# The order the workers take the jobs in: the fixed order with one display, the longest first (by the last gate's
# times) with several, so the long routes do not end up last.
job_order() {
    local count
    count="$(wc -l <"$logs/jobs.txt")"
    if ((${#displays[@]} == 1)) || [[ ! -f "$(durations_file)" ]]; then
        seq 1 "$count"
        return
    fi
    awk -F"$field_separator" 'NR == FNR { seconds[$1] = $2; next } { name = $1; sub(/-pass[0-9]+$/, "", name);
        print (name in seconds ? seconds[name] : 0), FNR }' "$(durations_file)" "$logs/jobs.txt" |
        sort -k1,1nr -k2,2n | cut -d' ' -f2
}

# run_game <display> <log> <arguments separated by tabs>: one run of the game; returns its exit code.
run_game() {
    local display="$1" log="$2" arguments=() pid rc
    IFS="$argument_separator" read -ra arguments <<<"$3"
    mkdir -p "$(dirname "$logs/$log")"
    env DISPLAY=":$display" VK_ICD_FILENAMES="$lavapipe_icd" PRESET="$route_preset" \
        ${lp_threads:+"LP_NUM_THREADS=$lp_threads"} \
        timeout "$route_timeout_seconds" "$dev" run "${arguments[@]}" >"$logs/$log" 2>&1 </dev/null &
    pid=$!
    record_pid run "$pid"
    wait "$pid"
    rc=$?
    forget_pid run "$pid"
    return "$rc"
}

write_dev_assets() {
    [[ ! -e "$dev_assets_file" ]] || die "$dev_assets_file exists; move it away, the gate writes its own"
    printf 'extracted_content:\n  - %s\n' "$DWE_CONTENT" >"$dev_assets_file"
    dev_assets_written=true
}

remove_dev_assets() {
    rm -f "$dev_assets_file"
    dev_assets_written=false
}

run_routes() {
    local display pid workers=() count display_list
    mkdir -p "$logs/claims" "$logs/results" "$logs/seconds"
    write_jobs >"$logs/jobs.txt"
    take_displays
    job_order >"$logs/order.txt"
    count="$(wc -l <"$logs/jobs.txt")"
    display_list="$(printf ':%s,' "${displays[@]}" | sed 's/,$//')"
    echo "gate: routes=$routes_mode jobs=$count runs=$(count_runs "$logs/jobs.txt") displays=$display_list" \
        "lp-threads=${lp_threads:-unset}" | tee -a "$summary"
    if grep -q -- '--dev-assets' "$logs/jobs.txt"; then write_dev_assets; fi
    for display in "${displays[@]}"; do
        worker "$display" &
        pid=$!
        record_pid worker "$pid"
        workers+=("$pid")
    done
    for pid in "${workers[@]}"; do
        wait "$pid"
        forget_pid worker "$pid"
    done
    remove_dev_assets
    stop_recorded
    local index
    for ((index = 1; index <= count; index++)); do
        cat "$logs/results/$index" 2>/dev/null ||
            echo "$(sed -n "${index}p" "$logs/jobs.txt" | cut -d"$field_separator" -f1) rc=? | NOT RUN"
    done >>"$summary"
    check_cooked_fonts
    save_durations
}

# The runs on the cooked .pak must load the bitmap font: without it the game only warns and the route still passes.
check_cooked_fonts() {
    local log
    for log in "$logs"/cooked-fade-*.log; do
        [[ -f "$log" ]] || continue
        if grep -q "$cooked_font_line" "$log"; then
            echo "font $(basename "$log" .log): loaded" >>"$summary"
        else
            echo "font $(basename "$log" .log): MISSING" >>"$summary"
        fi
    done
}

# The seconds of each run, in the logs and, newest first per route, for the next gate's order.
save_durations() {
    local file
    file="$(durations_file)"
    mkdir -p "$state_dir"
    cat "$logs"/seconds/* >"$logs/route-seconds.txt" 2>/dev/null || return 0
    touch "$file"
    sed "s/-pass[0-9]*$field_separator/$field_separator/" "$logs/route-seconds.txt" | cat - "$file" |
        awk -F"$field_separator" '!seen[$1]++' >"$file.new" && mv "$file.new" "$file"
}

# --- the other steps ----------------------------------------------------------------------------------------------

# The start of the names of this gate's containers: unique to this run, as it holds the gate's PID.
container_prefix() {
    echo "dwe-gate-$tier-$tag-$$" | tr -c 'a-zA-Z0-9_.\n-' '-'
}

# Stops the containers this gate started that are still running (an interrupted check): the names that start with
# this run's own prefix, so no one else's.
stop_own_containers() {
    local prefix ids
    prefix="$(container_prefix)"
    ids="$(sg docker -c "docker ps -q --filter name=^$prefix-" 2>/dev/null)" || return 0
    [[ -z "$ids" ]] || sg docker -c "docker stop $(echo $ids)" >/dev/null 2>&1
    return 0
}

# in_docker <step> <command...>: runs a dev.sh command with the docker group, passing on the variables dev.sh reads,
# its containers named after the gate and the step. It runs in the background and is waited for, because bash runs a
# trap only once a foreground command ends: a signal during an hour-long check stops the gate at once. sg leads a
# session of its own (setsid), recorded and stopped whole on exit; its containers are stopped by name.
in_docker() {
    local passed=("DWE_CONTAINER_NAME=$(container_prefix)-$1") name pid rc
    shift
    for name in DWE_IMAGE DWE_JOBS DWE_NICE DWE_DISC DWE_CONTENT DWE_DECOMP DWE_GATE_STATE_DIR DWE_CACHE_DIR \
        DWE_CCACHE_DIR DWE_XWIN_DIR; do
        [[ -n "${!name:-}" ]] && passed+=("$name=${!name}")
    done
    setsid sg docker -c "$(printf '%q ' env "${passed[@]}" "$@")" </dev/null &
    pid=$!
    record_pid step "$pid"
    wait "$pid"
    rc=$?
    forget_pid step "$pid"
    return "$rc"
}

# The tier's check: check-short, or check-gate with check-full's steps (full clang-tidy, every test, the cook).
run_check() {
    local rc check="check-short" modes=(DWE_GATE_TIDY="$tidy_mode")
    if [[ "$tier" == "daily" ]]; then
        check="check-gate"
        modes+=(DWE_GATE_COOK="$cook_mode")
    fi
    echo "gate: tier=$tier check=$check tidy-mode=$tidy_mode cook-mode=$cook_mode" | tee -a "$summary"
    in_docker check PRESET="$route_preset" DWE_LOG_DIR="$logs/check-logs" "${modes[@]}" \
        DWE_GATE_DISC_TESTS="$disc_tests" "$dev" "$check" >"$logs/check.log" 2>&1
    rc=$?
    echo "$check EXIT $rc" >>"$summary"
    grep -h '^gate: \(tidy\|cook\)' "$logs/check.log" >>"$summary"
    if [[ -n "$disc_tests" ]]; then disc_test_summary "$logs/check-logs/disc-tests.log" >>"$summary"; fi
    return "$rc"
}

# One line per test on the user's disc that check-short ran (ctest -V): its name, its exact test-case filter, doctest's
# count of assertions and ctest's result.
disc_test_summary() {
    echo "gate: disc-tests=$disc_tests"
    [[ -f "$1" ]] || return 0
    awk '
        /^[0-9]+: Test command: / {
            number = $1; sub(/:$/, "", number)
            filter[number] = "?"
            if (match($0, /--test-case=[^"]*/)) filter[number] = substr($0, RSTART, RLENGTH)
            next
        }
        /^[0-9]+: \[doctest\] assertions:/ {
            number = $1; sub(/:$/, "", number)
            text = $0; sub(/^[0-9]+: \[doctest\] /, "", text); sub(/ *\| *$/, "", text)
            assertions[number] = text
            next
        }
        / Test +#[0-9]+: / {
            line = $0; sub(/.* Test +#/, "", line)
            number = line; sub(/:.*/, "", number)
            name = line; sub(/^[0-9]+: /, "", name); sub(/ \.\.+.*/, "", name)
            result = line; sub(/.* \.\.+ */, "", result); sub(/ +[0-9.]+ sec.*/, "", result)
            order[++count] = number; names[number] = name; results[number] = result
        }
        END {
            for (i = 1; i <= count; i++) {
                n = order[i]
                printf "disc-test %s | filter=%s | %s | result=%s\n", names[n], filter[n],
                    (n in assertions ? assertions[n] : "assertions: NONE"), results[n]
            }
        }' "$1"
}

# How the Windows cross build runs. What the gate needs is 0 warnings on the exact tip; /WX fails the build on a warning
# in the project's code, but third-party code and the linker only warn, and an incremental build does not compile an
# unchanged source again, so it cannot print its warnings again. So the short tier builds incrementally only when the
# folder's last build was the gate's, with 0 warnings, the same image, and nothing built in the folder since (its
# .ninja_log unchanged); otherwise it cleans the project's outputs and builds them all again, which the compiler cache
# keeps short and which prints every source's warnings again (the cache replays them). The daily tier builds in a new
# folder. Sets windows_mode (incremental, rebuild or fresh) and windows_reason.
windows_build_mode() {
    local marker_image marker_log marker_commit
    if [[ "$tier" == "daily" ]]; then
        windows_mode=fresh windows_reason="the daily tier builds in a new folder"
    elif [[ ! -d "$windows_dir" ]]; then
        windows_mode=fresh windows_reason="no build folder yet"
    elif [[ ! -f "$windows_marker" ]]; then
        windows_mode=rebuild windows_reason="no record that the folder's last build had 0 warnings"
    else
        { read -r marker_commit; read -r marker_image; read -r marker_log; } <"$windows_marker"
        if [[ "$marker_image" != "$(image_id)" ]]; then
            windows_mode=fresh windows_reason="the image changed since the folder's last 0-warning build"
        elif [[ "$marker_log" != "$(ninja_log_digest)" ]]; then
            windows_mode=rebuild windows_reason="the folder was built outside the gate since its last 0-warning build"
        else
            windows_mode=incremental
            windows_reason="the folder's last build (${marker_commit:0:12}) had 0 warnings and nothing was built since"
        fi
    fi
}

image_id() {
    sg docker -c "$(printf '%q ' docker image inspect --format '{{.Id}}' "$image")" 2>/dev/null
}

# Checks, before any step starts, that what the chosen steps need is there, so a stale image or a missing program fails
# in seconds and by name rather than an hour into the gate: the image and its tools, and the routes' host programs.
preflight() {
    image_preflight && routes_preflight
}

preflight_fail() {
    echo "gate: preflight=fail reason=$*" | tee -a "$summary" >&2
    return 1
}

image_preflight() {
    local tools=() step_name list missing id
    for step_name in check windows; do
        has_step "$step_name" || continue
        list="${step_name}_tools[@]"
        tools+=("${!list}")
    done
    ((${#tools[@]} > 0)) || return 0
    mapfile -t tools < <(printf '%s\n' "${tools[@]}" | awk '!seen[$0]++')
    id="$(image_id)"
    echo "gate: image=$image id=${id:-none}" >>"$summary"
    [[ -n "$id" ]] || preflight_fail "no image $image" || return 1
    missing="$(sg docker -c "$(printf '%q ' docker run --rm --pull=never --name "$(container_prefix)-preflight" \
        "$image" bash -c 'for tool; do command -v "$tool" >/dev/null || echo "$tool"; done' preflight "${tools[@]}")")" ||
        preflight_fail "cannot run the image $image" || return 1
    [[ -z "$missing" ]] || preflight_fail "the image $image ($id) has no $(paste -sd, <<<"$missing")" || return 1
    echo "gate: preflight=ok image-tools=$(IFS=,; echo "${tools[*]}")" >>"$summary"
}

# The routes run the game on the host, on Xvfb with lavapipe; without the check step, the game must be built already.
routes_preflight() {
    local program missing=()
    has_step routes || return 0
    for program in "${routes_host_programs[@]}"; do
        command -v "$program" >/dev/null || missing+=("$program")
    done
    ((${#missing[@]} == 0)) || preflight_fail "the host has no $(IFS=,; echo "${missing[*]}")" || return 1
    [[ -f "$lavapipe_icd" ]] || preflight_fail "the host has no lavapipe driver ($lavapipe_icd)" || return 1
    if ! has_step check && [[ ! -x "$root/$game_binary" ]]; then
        preflight_fail "no $game_binary: the routes run without the check step, which builds it" || return 1
    fi
    echo "gate: preflight=ok host=$(IFS=,; echo "${routes_host_programs[*]}"),$lavapipe_icd" >>"$summary"
}

ninja_log_digest() {
    sha256sum "$windows_dir/.ninja_log" 2>/dev/null | cut -d' ' -f1
}

run_windows() {
    local rc warnings
    windows_build_mode
    echo "gate: windows-cross=$windows_mode reason=$windows_reason" | tee -a "$summary"
    rm -f "$windows_marker"
    if [[ "$windows_mode" == "fresh" ]]; then
        remove_build_dir "$root" "$windows_dir" || die "cannot start the Windows build in a new folder"
    fi
    in_docker windows PRESET="$windows_preset" "$dev" configure >"$logs/windows-cross.log" 2>&1
    rc=$?
    if ((rc == 0)) && [[ "$windows_mode" == "rebuild" ]]; then
        in_docker windows PRESET="$windows_preset" "$dev" build --target clean >>"$logs/windows-cross.log" 2>&1
        rc=$?
    fi
    if ((rc == 0)); then
        in_docker windows PRESET="$windows_preset" "$dev" build >>"$logs/windows-cross.log" 2>&1
        rc=$?
    fi
    warnings="$(grep -c 'warning:' "$logs/windows-cross.log")"
    echo "windows-cross EXIT $rc warnings=$warnings" >>"$summary"
    ((rc == 0 && warnings == 0)) || return 1
    printf '%s\n%s\n%s\n' "$tip" "$(image_id)" "$(ninja_log_digest)" >"$windows_marker"
}

routes_passed() {
    local runs passed
    runs="$(grep -c ' rc=' "$summary")"
    passed="$(grep -cE ' rc=0 \| autotest: PASS \| validation layer on, 0 messages \| errors=0 ' "$summary")"
    echo "runs: $runs passed: $passed end $(date -Is)" >>"$summary"
    ((runs > 0 && runs == passed)) && ! grep -q '^font .*: MISSING' "$summary"
}

# --- main ---------------------------------------------------------------------------------------------------------

check_environment() {
    [[ -n "${DWE_CONTENT:-}" ]] || die "DWE_CONTENT must name the extracted content"
    DWE_CONTENT="$(cd "$DWE_CONTENT" && pwd)" || die "DWE_CONTENT is not a folder"
    export DWE_CONTENT
    local folder
    for folder in "${pack_folders[@]}"; do
        [[ -d "$DWE_CONTENT/$folder" ]] || die "no $folder pack in $DWE_CONTENT"
    done
    if [[ "$tier" == "daily" || -n "$disc_tests" ]]; then
        [[ -f "${DWE_DISC:-}" ]] || die "DWE_DISC must name the user's disc image for the disc tests"
    fi
    [[ -z "$(git -C "$root" status --porcelain)" ]] || die "the working tree has changes; a gate runs on a commit"
    local free_gb needed_gb=0 step_name floor
    for step_name in "${all_steps[@]}"; do
        floor="${step_name}_free_gb"
        has_step "$step_name" && needed_gb=$((needed_gb + ${!floor}))
    done
    free_gb="$(df --output=avail -BG "$root" | tail -1 | tr -dc 0-9)"
    ((free_gb >= needed_gb)) || die "only $free_gb GB free, under the $needed_gb GB that the steps $steps need"
}

default_logs_dir() {
    local team stamp
    stamp="$(date +%m%d-%H%M)"
    if team="$(team_logs_dir)"; then
        if [[ "$tier" == "daily" ]]; then
            echo "$team/daily-$(date +%F)"
        else
            echo "$team/$tag/gate-$stamp"
        fi
    else
        echo "$root/build/gate-logs/$tier-$stamp"
    fi
}

default_lock_dir() {
    local team
    if team="$(team_logs_dir)"; then
        echo "$team/p4-locks/full.d"
    else
        echo "$state_dir/full.d"
    fi
}

parse_options "$@"
# The routes read their paths, and route-dev-assets its dev-assets.yaml, from the repository's root.
cd "$root" || die "cannot enter $root"
check_environment
state_dir="${DWE_GATE_STATE_DIR:-$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)/dwe-gate}"
lock_dir="${lock_dir:-$(default_lock_dir)}"
logs="${logs:-$(default_logs_dir)}"
[[ "$logs$DWE_CONTENT" != *[$field_separator$argument_separator]* ]] ||
    die "the logs and content paths may not hold '$field_separator' or tabs"
mkdir -p "$logs"
logs="$(cd "$logs" && pwd)"
pid_dir="$logs/pids"
mkdir -p "$pid_dir"
summary="$logs/summary.txt"
tip="$(git -C "$root" rev-parse HEAD)"
main_tip="$(git -C "$root" rev-parse --verify --quiet "$main_branch")"
# The image dev.sh runs in: DWE_IMAGE or dev.sh's own default, which the gate does not repeat.
image="$("$dev" image-name)"

trap clean_up EXIT
trap 'exit 130' INT TERM
take_lock
gate_start=$SECONDS
{
    echo "gate: tier=$tier tag=$tag tip=$tip main=$main_tip on-main=$([[ "$tip" == "$main_tip" ]] && echo yes || echo no)" \
        "base=$(git -C "$root" merge-base HEAD "$main_branch" 2>/dev/null)"
    echo "gate: content=$DWE_CONTENT start=$(date -Is) load=$(cut -d' ' -f1-3 /proc/loadavg)"
} >"$summary"
status=0
times=""
skipped=()
if ! preflight; then
    echo "gate: result=FAIL preflight total=$((SECONDS - gate_start))s" >>"$summary"
    cat "$summary"
    exit 1
fi
for step_name in "${all_steps[@]}"; do
    if ! has_step "$step_name"; then
        echo "gate: $step_name=skipped reason=--steps $steps" >>"$summary"
        skipped+=("$step_name")
        continue
    fi
    start=$SECONDS
    case "$step_name" in
        check) run_check || status=1 ;;
        windows) run_windows || status=1 ;;
        routes)
            run_routes
            routes_passed || status=1
            ;;
    esac
    times+=" $step_name=$((SECONDS - start))s"
done
# A pass with steps left out is not a gate: the result says which.
result="FAIL"
if ((status == 0)); then
    result="PASS"
    ((${#skipped[@]} == 0)) || result="PASS (partial: $(IFS=,; echo "${skipped[*]}") skipped)"
fi
echo "gate: result=$result$times total=$((SECONDS - gate_start))s" >>"$summary"
cat "$summary"
exit "$status"
