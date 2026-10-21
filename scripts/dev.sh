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
# The MSVC libraries and the Windows SDK of the Windows cross build, fetched by windows-sdk.
xwin_volume="dwe-xwin"
xwin_dir="${DWE_XWIN_DIR:-}"
preset="${PRESET:-linux-clang-debug}"
jobs="${DWE_JOBS:-$(nproc)}"
log_dir="${DWE_LOG_DIR:-$root/build/logs}"
# The cooked forms of DWE_CONTENT's base pack (cook-content): a cooked pack folder and its .pak archive, each in a
# content folder of its own, since the game and dwx validate would see two packs with the same id in one folder.
# A manual cook-content updates its folder in place; check-full cooks into a fresh folder of its own every time.
cooked_dir="${DWE_COOKED_DIR:-$root/build/cooked-content}"
gate_cooked_dir="$root/build/gate-cooked"
base_pack="base"
cooked_folder_content="folder"
cooked_archive_content="archive"
# dwx cooks on the host's platform: a preset that does not build for Linux takes the dwx of this one.
linux_dwx_preset="ci"
missing_content_message="dev.sh: cook-content needs DWE_CONTENT, the content folder extracted with dwx"
protected_cooked_dir_message="dev.sh: DWE_COOKED_DIR may not be inside, or contain, a protected folder"
repository_cooked_dir_message="dev.sh: DWE_COOKED_DIR may not be the repository itself"
unknown_main_checkout_message="dev.sh: cook-content cannot find the main checkout from git, so it cannot tell which \
folders are protected"
missing_dwx_message="dev.sh: cook-content needs a Linux build of dwx; build the preset first"

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
  check-full [args]   The check of the tip: format, configure (with args), build, every test and clang-tidy, and
                      cook-content into a fresh build/gate-cooked when DWE_CONTENT is set and the preset builds for
                      Linux
  windows-sdk [args]  Fetch the MSVC libraries and the Windows SDK for PRESET=windows-cross with xwin, which first
                      asks you to accept Microsoft's license (--accept-license if you already have)
  cook-content        Cook the base pack of DWE_CONTENT with dwx cook into DWE_COOKED_DIR/$cooked_folder_content, pack it
                      into DWE_COOKED_DIR/$cooked_archive_content/$base_pack.pak, and check both with dwx validate (needs
                      a build of the preset, or of $linux_dwx_preset for a preset that does not build for Linux); an
                      earlier cook there is updated in place. The routes on a cooked pack run on the archive
  run [args]          Run the game on the host

Environment:
  PRESET              CMake preset (default linux-clang-debug; ci for the checks)
  DWE_IMAGE           Development image (default dwe-dev)
  DWE_JOBS            Parallel jobs of the build, the tests, clang-tidy and clang-format (default: the CPU count)
  DWE_CACHE_DIR       Host folder for the vcpkg binary cache (default: the $cache_volume volume)
  DWE_CCACHE_DIR      Host folder for the compiler cache (default: the $ccache_volume volume)
  DWE_CCACHE_SIZE     Size limit of the compiler cache (default 10G)
  DWE_XWIN_DIR        Host folder for the files of windows-sdk (default: the $xwin_volume volume)
  DWE_LOG_DIR         Folder for the logs of the checks (default build/logs)
  DWE_DISC            Path to the user's disc image (.bin or .cue); its folder is mounted read-only in the container at
                      the same path, so the tools and the disc tests can read it
  DWE_CONTENT         Path to a content folder extracted with dwx (packs such as base/); it is mounted read-only in the
                      container at the same path, so the tests that need real content can read it
  DWE_DECOMP          Path to a checkout of the decompilation; it is mounted read-only in the container at the same
                      path, so the tests that compare with it can read it
  DWE_COOKED_DIR      Folder for the output of cook-content (default build/cooked-content); outside the repository it
                      is mounted read-write in the container at the same path. It may not be inside or contain
                      DWE_CONTENT, DWE_DISC's folder, DWE_DECOMP, ~/code/assets or the team's protected folders.
                      check-full ignores it and cooks into build/gate-cooked, emptied first
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
        -v "$(mount_of "$xwin_dir" "$xwin_volume"):/opt/xwin" \
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

# Content extracted from the user's disc, for the tests that need real data: read-only, at the same path.
if [[ -n "${DWE_CONTENT:-}" ]]; then
    if [[ ! -d "$DWE_CONTENT" ]]; then
        echo "dev.sh: DWE_CONTENT is not a folder: $DWE_CONTENT" >&2
        exit 1
    fi
    DWE_CONTENT="$(cd "$DWE_CONTENT" && pwd)"
    export DWE_CONTENT
    disc_flags+=(-v "$DWE_CONTENT:$DWE_CONTENT:ro" -e DWE_CONTENT)
fi

# A checkout of the decompilation, the reference of behaviour for the parity tests: read-only, at the same path.
if [[ -n "${DWE_DECOMP:-}" ]]; then
    if [[ ! -d "$DWE_DECOMP" ]]; then
        echo "dev.sh: DWE_DECOMP is not a folder: $DWE_DECOMP" >&2
        exit 1
    fi
    DWE_DECOMP="$(cd "$DWE_DECOMP" && pwd)"
    export DWE_DECOMP
    disc_flags+=(-v "$DWE_DECOMP:$DWE_DECOMP:ro" -e DWE_DECOMP)
fi

# The main checkout, the folder of the repository's shared git folder; the team's _work sits next to it. Without it
# the team's protected folders are unknown, so this fails instead of protecting less.
main_checkout_of_repository() {
    local common main
    if ! common="$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)" || [[ -z "$common" ]]; then
        echo "$unknown_main_checkout_message" >&2
        exit 1
    fi
    main="$(dirname "$common")"
    if [[ -z "$main" || "$main" == "." ]]; then
        echo "$unknown_main_checkout_message: $common" >&2
        exit 1
    fi
    echo "$main"
}

# The folders cook-content never writes into, resolved: the user's disc, content and decompilation, and the shared
# folders of the team's layout around the main checkout given.
protected_folders() {
    local main_checkout="$1" work
    work="$(dirname "$main_checkout")/_work"
    local folders=("$HOME/code/assets" "$main_checkout/build/verify-p1" "$work"/content* "$work/p2/content"
        "$work/p2/scratch-routes" "$work/p3-inventory" "$work/saves-play" "$work/mouse-route" "$work/validate-runtime"
        "$work/viz")
    [[ -n "${DWE_CONTENT:-}" ]] && folders+=("$DWE_CONTENT")
    [[ -n "${DWE_DISC:-}" ]] && folders+=("$(dirname "$DWE_DISC")")
    [[ -n "${DWE_DECOMP:-}" ]] && folders+=("$DWE_DECOMP")
    local folder
    for folder in "${folders[@]}"; do
        realpath -m "$folder"
    done
}

# Fails when a folder is the repository, a protected folder, inside one, or holds one. Both sides are resolved,
# symbolic links too. The main checkout is found first, outside the loop's input, so a failure stops the script.
check_not_protected() {
    local folder main_checkout protected
    folder="$(realpath -m "$1")"
    if [[ "$folder" == "$(realpath -m "$root")" ]]; then
        echo "$repository_cooked_dir_message: $folder" >&2
        exit 1
    fi
    main_checkout="$(main_checkout_of_repository)"
    while IFS= read -r protected; do
        if [[ "$folder/" == "$protected/"* || "$protected/" == "$folder/"* ]]; then
            echo "$protected_cooked_dir_message: $folder ($protected)" >&2
            exit 1
        fi
    done < <(protected_folders "$main_checkout")
}

# The output of cook-content: checked before anything is written, then created so it belongs to the host user and not
# to the container's root. The repository is mounted already; any other folder is mounted read-write at the same path.
if [[ "${1:-}" == "cook-content" ]]; then
    if [[ -z "${DWE_CONTENT:-}" ]]; then
        echo "$missing_content_message" >&2
        exit 1
    fi
    check_not_protected "$cooked_dir"
    mkdir -p "$cooked_dir"
    cooked_dir="$(realpath "$cooked_dir")"
    if [[ "$cooked_dir/" != "$root/"* ]]; then
        disc_flags+=(-v "$cooked_dir:$cooked_dir")
    fi
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
    ensure_cache "$xwin_dir" "$xwin_volume"
}

sources() {
    git -C "$root" ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp'
}

# Fails when any C++ or Lua source of the repository uses goto (project rule: structured control flow only).
no_goto() {
    local found
    found="$(git -C "$root" ls-files --cached --others --exclude-standard -z -- '*.cpp' '*.hpp' '*.h' '*.lua' \
        | xargs -0 -r grep -nwH 'goto' --)" || true
    [[ -z "$found" ]] && return 0
    printf 'goto is not allowed:\n%s\n' "$found"
    return 1
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

# cook-content inside the container: cooks the extracted base pack, packs the cooked pack into a .pak, and checks
# the cooked folder, then the repository's game pack mounted over the archive, which checks the archive too, with dwx
# validate. dwx cook updates an earlier cook in place and refuses a folder it did not write. The old archive goes
# first, so a failed cook leaves none behind.
# Arguments: dwx, the extracted pack, the cooked pack folder, the archive.
cook_content='set -euo pipefail
dwx="$1" source="$2" cooked="$3" archive="$4" schemas=content/schemas
rm -f "$archive"
"$dwx" cook "$source" -o "$cooked"
mkdir -p "$(dirname "$archive")"
"$dwx" pack "$cooked" -o "$archive"
"$dwx" validate --engine --schemas "$schemas" "$(dirname "$cooked")"
"$dwx" validate --engine --schemas "$schemas" --with "$(dirname "$archive")" content/packs/game'

# Whether a preset builds programs that run here, on Linux.
builds_for_linux() {
    [[ "$1" == "$linux_dwx_preset" || "$1" == linux-* ]]
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
    step no-goto no_goto || status=1
    # Warnings fail the build already, but not the linker's; clang-tidy fails on any diagnostic, and the log says so too.
    if step configure "$self" configure "$@" && step build --forbid 'warning:' "$self" build; then
        if [[ "$tier" == "full" ]]; then
            step test "$self" test &
            local test_pid=$!
            step tidy --forbid 'warning:|error:' env DWE_NICE=10 "$self" tidy --warnings-as-errors='*' || status=1
            wait "$test_pid" || status=1
            # The routes on a cooked pack run on its output (docs/guides/prueba-vertical.md), cooked from scratch so
            # nothing of an earlier cook can hide a regression.
            if [[ -n "${DWE_CONTENT:-}" ]] && builds_for_linux "$preset"; then
                rm -rf "$gate_cooked_dir"
                step cook-content env DWE_COOKED_DIR="$gate_cooked_dir" "$self" cook-content || status=1
            elif [[ -n "${DWE_CONTENT:-}" ]]; then
                echo "cook-content skipped: preset $preset does not build for Linux"
            fi
        else
            step test "$self" test -LE slow || status=1
        fi
    else
        status=1
    fi
    steps=(no-goto configure build test)
    [[ "$tier" == "full" ]] && steps=(format "${steps[@]}" tidy)
    if [[ "$tier" == "full" && -n "${DWE_CONTENT:-}" ]] && builds_for_linux "$preset"; then steps+=(cook-content); fi
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
    windows-sdk)
        # xwin unpacks next to the result and moves the files into place, which only works on the same filesystem,
        # so it unpacks inside the volume and the unpacked copies are deleted afterwards: the volume keeps only the
        # layout of a Visual Studio installation. xwin names the SDK's folders in lower case, and /winsysroot looks for
        # Include and Lib.
        ensure_caches
        in_container bash -c 'rm -rf /opt/xwin/sysroot && xwin --cache-dir /opt/xwin/unpack "$@" splat --include-debug-libs \
            --use-winsysroot-style --preserve-ms-arch-notation --output /opt/xwin/sysroot \
            && rm -rf /opt/xwin/unpack \
            && ln -sfn include "/opt/xwin/sysroot/Windows Kits/10/Include" \
            && ln -sfn lib "/opt/xwin/sysroot/Windows Kits/10/Lib"' xwin "$@"
        ;;
    check-commit)
        check commit "$@"
        ;;
    check-full)
        check full "$@"
        ;;
    cook-content)
        dwx_preset="$preset"
        builds_for_linux "$dwx_preset" || dwx_preset="$linux_dwx_preset"
        dwx="build/$dwx_preset/apps/dwx/dwx"
        if [[ ! -x "$root/$dwx" ]]; then
            echo "$missing_dwx_message: $dwx" >&2
            exit 1
        fi
        ensure_caches
        in_container bash -c "$cook_content" cook-content "$dwx" "$DWE_CONTENT/$base_pack" \
            "$cooked_dir/$cooked_folder_content/$base_pack" "$cooked_dir/$cooked_archive_content/$base_pack.pak"
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
