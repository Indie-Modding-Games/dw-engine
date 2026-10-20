#!/usr/bin/env bash
# Development entry point. Builds and tests run inside the development container, so the host needs nothing but
# Docker; the game itself runs on the host, which has the GPU and the display.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
self="$root/scripts/dev.sh"
image="${DWE_IMAGE:-dwe-dev}"
# The vcpkg binary cache and the compiler cache: named volumes by default, or host folders when DWE_CACHE_DIR and
# DWE_CCACHE_DIR are set (CI caches them).
cache_volume="dwe-vcpkg-cache"
cache_dir="${DWE_CACHE_DIR:-}"
ccache_volume="dwe-ccache"
ccache_dir="${DWE_CCACHE_DIR:-}"
preset="${PRESET:-linux-clang-debug}"
jobs="${DWE_JOBS:-$(nproc)}"
log_dir="${DWE_LOG_DIR:-$root/build/logs}"

usage() {
    cat <<EOF
Usage: scripts/dev.sh <command> [args]

  image               Build the development image
  shell               Open a shell in the container
  configure           Configure the preset (PRESET=$preset)
  build               Build the preset
  test                Run the tests of the preset, in parallel
  format [--check]    Format the sources with clang-format
  tidy [args]         Run clang-tidy on the sources in parallel, passing it args (needs a configure, not a build)
  check-commit [args] The check of every commit: configure (with args), build and the tests but the slow ones
  check-full [args]   The check of the tip: format, configure (with args), build, every test and clang-tidy
  run [args]          Run the game on the host

Environment:
  PRESET              CMake preset (default linux-clang-debug; ci for the checks)
  DWE_IMAGE           Development image (default dwe-dev)
  DWE_JOBS            Parallel jobs of the build, the tests, clang-tidy and clang-format (default: the CPU count)
  DWE_CACHE_DIR       Host folder for the vcpkg binary cache (default: the $cache_volume volume)
  DWE_CCACHE_DIR      Host folder for the compiler cache (default: the $ccache_volume volume)
  DWE_CCACHE_SIZE     Size limit of the compiler cache (default 10G)
  DWE_LOG_DIR         Folder for the logs of the checks (default build/logs)
  DWE_DISC            Path to the user's disc image (.bin or .cue); its folder is mounted read-only in the container at
                      the same path, so the tools and the disc tests can read it
EOF
}

in_container() {
    # -i always, so commands can read piped input; -t only when there is a terminal.
    local tty_flags=(-i)
    if [[ -t 0 && -t 1 ]]; then tty_flags=(-it); fi
    docker run --rm "${tty_flags[@]}" \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp/home \
        -e VCPKG_DOWNLOADS=/cache/vcpkg/downloads \
        -e CTEST_PARALLEL_LEVEL="$jobs" \
        -e CCACHE_DIR=/cache/ccache \
        -e CCACHE_MAXSIZE="${DWE_CCACHE_SIZE:-10G}" \
        -e CCACHE_BASEDIR="$root" -e CCACHE_NOHASHDIR=true \
        -v "$root:$root" -w "$root" \
        -v "$(mount_of "$cache_dir" "$cache_volume"):/cache/vcpkg" \
        -v "$(mount_of "$ccache_dir" "$ccache_volume"):/cache/ccache" \
        "${disc_flags[@]}" \
        "$image" "$@"
}

# The disc image is the user's: mount only its folder, read-only, at the same path so DWE_DISC means the same inside.
disc_flags=()
if [[ -n "${DWE_DISC:-}" ]]; then
    if [[ ! -f "$DWE_DISC" ]]; then
        echo "dev.sh: DWE_DISC is not a file: $DWE_DISC" >&2
        exit 1
    fi
    DWE_DISC="$(cd "$(dirname "$DWE_DISC")" && pwd)/$(basename "$DWE_DISC")"
    export DWE_DISC
    disc_flags=(-v "$(dirname "$DWE_DISC"):$(dirname "$DWE_DISC"):ro" -e DWE_DISC)
fi

# The host folder when one is given, otherwise the named volume.
mount_of() {
    if [[ -n "$1" ]]; then echo "$1"; else echo "$2"; fi
}

ensure_cache() {
    local dir="$1" volume="$2"
    if [[ -n "$dir" ]]; then
        mkdir -p "$dir"
        return
    fi
    docker volume inspect "$volume" >/dev/null 2>&1 && return
    docker volume create "$volume" >/dev/null
    # The volume is created by root; let the host user write the cache.
    docker run --rm -v "$volume:/cache" "$image" chmod a+rwx /cache
}

ensure_caches() {
    ensure_cache "$cache_dir" "$cache_volume"
    ensure_cache "$ccache_dir" "$ccache_volume"
}

sources() {
    git -C "$root" ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp'
}

# One clang-tidy per file, its output printed in one piece so parallel runs do not mix their lines. Any diagnostic
# that fails clang-tidy fails the whole run: xargs exits with 123.
tidy_one='out="$(clang-tidy "$@" 2>&1)"; rc=$?; [[ -z "$out" ]] || printf "%s\n" "$out"; exit $((rc == 0 ? 0 : 1))'

# step <name> [--forbid <regex>] <command...>: runs a step of a check with its output in $log_dir/<name>.log and
# prints its result. With --forbid, a log line that matches fails the step even when the command passed.
step() {
    local name="$1" log="$log_dir/$1.log" start=$SECONDS forbid=""
    shift
    if [[ "$1" == "--forbid" ]]; then
        forbid="$2"
        shift 2
    fi
    echo "[$(date +%T)] $name"
    if "$@" >"$log" 2>&1 </dev/null && { [[ -z "$forbid" ]] || ! grep -qE "$forbid" "$log"; }; then
        echo "[$(date +%T)] $name OK ($((SECONDS - start)) s)"
    else
        echo "[$(date +%T)] $name FAILED ($((SECONDS - start)) s): $log"
        if [[ -n "$forbid" ]]; then grep -nE "$forbid" "$log" | head -n 20 | sed 's/^/    /'; fi
        tail -n 40 "$log" | sed 's/^/    /'
        return 1
    fi
}

# Prints the logs of a check folded, for the CI.
print_logs() {
    [[ "${GITHUB_ACTIONS:-}" == "true" ]] || return 0
    local name
    for name in "$@"; do
        echo "::group::$name"
        cat "$log_dir/$name.log"
        echo "::endgroup::"
    done
}

# The checks of a commit, and of the tip. The tip adds the format, the slow tests and clang-tidy; clang-tidy reads only
# the compile commands, so it runs at a lower priority next to the tests, which wait for the build.
check() {
    local tier="$1" status=0 steps
    shift
    export PRESET="${PRESET:-ci}"
    preset="$PRESET"
    mkdir -p "$log_dir"
    echo "check-$tier: preset $preset, $jobs jobs, logs in $log_dir"
    if [[ "$tier" == "full" ]]; then
        [[ -n "${DWE_DISC:-}" ]] || echo "DWE_DISC is not set: the tests on the user's disc skip"
        step format "$self" format --check || status=1
    fi
    # Warnings fail the build already, but not the linker's; clang-tidy fails on any diagnostic, and the log says so too.
    if step configure "$self" configure "$@" && step build --forbid 'warning:' "$self" build; then
        if [[ "$tier" == "full" ]]; then
            step test "$self" test &
            local test_pid=$!
            step tidy --forbid 'warning:|error:' env DWE_NICE=10 "$self" tidy --warnings-as-errors='*' || status=1
            wait "$test_pid" || status=1
        else
            step test "$self" test -LE slow || status=1
        fi
    else
        status=1
    fi
    steps=(configure build test)
    [[ "$tier" == "full" ]] && steps=(format "${steps[@]}" tidy)
    print_logs "${steps[@]}"
    echo "[$(date +%T)] check-$tier $([[ $status -eq 0 ]] && echo passed || echo FAILED)"
    return "$status"
}

command="${1:-}"
shift || true

case "$command" in
    image)
        docker build -t "$image" -f "$root/.devcontainer/Dockerfile" "$root/.devcontainer"
        ;;
    shell)
        ensure_caches
        in_container bash "$@"
        ;;
    configure)
        ensure_caches
        in_container cmake --preset "$preset" "$@"
        ;;
    build)
        ensure_caches
        in_container cmake --build --preset "$preset" -j "$jobs" "$@"
        ;;
    test)
        ensure_caches
        in_container ctest --preset "$preset" "$@"
        ;;
    format)
        ensure_caches
        if [[ "${1:-}" == "--check" ]]; then
            sources | in_container xargs -r -P "$jobs" -n 16 clang-format --dry-run --Werror
        else
            sources | in_container xargs -r -P "$jobs" -n 16 clang-format -i
        fi
        ;;
    tidy)
        ensure_caches
        sources | grep '\.cpp$' |
            in_container nice -n "${DWE_NICE:-0}" xargs -r -P "$jobs" -n 1 bash -c "$tidy_one" tidy \
                -p "build/$preset" --quiet "$@"
        ;;
    check-commit)
        check commit "$@"
        ;;
    check-full)
        check full "$@"
        ;;
    run)
        build_dir="$root/build/$preset"
        installed="$build_dir/vcpkg_installed/x64-linux-libcxx"
        if [[ -d "$installed/share/vulkan/explicit_layer.d" ]]; then
            export VK_LAYER_PATH="$installed/share/vulkan/explicit_layer.d"
            # The layer manifest names the library without a path. Search the release libraries first: the debug
            # build of the validation layer is many times slower and says so on every start.
            export LD_LIBRARY_PATH="$installed/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        fi
        exec "$build_dir/apps/game/dwe-game" "$@"
        ;;
    *)
        usage
        [[ -z "$command" ]] && exit 0 || exit 1
        ;;
esac
