#!/usr/bin/env bash
# Development entry point. Builds and tests run inside the development container, so the host needs
# nothing but Docker; the game itself runs on the host, which has the GPU and the display.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
self="$root/scripts/dev.sh"
image="${DWE_IMAGE:-dwe-dev}"
# The vcpkg binary cache and the compiler cache: named volumes by default, or host folders when
# DWE_CACHE_DIR and DWE_CCACHE_DIR are set (CI caches them).
cache_volume="dwe-vcpkg-cache"
cache_dir="${DWE_CACHE_DIR:-}"
ccache_volume="dwe-ccache"
ccache_dir="${DWE_CCACHE_DIR:-}"
# Room for every worktree's ci, debug and Windows builds side by side: at 10G the cache evicted
# constantly.
default_ccache_size="20G"
# The MSVC libraries and the Windows SDK of the Windows cross build, fetched by windows-sdk.
xwin_volume="dwe-xwin"
xwin_dir="${DWE_XWIN_DIR:-}"
preset="${PRESET:-linux-clang-debug}"
jobs="${DWE_JOBS:-$(nproc)}"
log_dir="${DWE_LOG_DIR:-$root/build/logs}"
# The cooked forms of DWE_CONTENT's base pack (cook-content): a cooked pack folder and its .pak
# archive, each in a content folder of its own, since the game and dwx validate would see two packs
# with the same id in one folder. A manual cook-content updates its folder in place; check-full
# cooks into a fresh folder of its own every time.
cooked_dir="${DWE_COOKED_DIR:-$root/build/cooked-content}"
gate_cooked_dir="$root/build/gate-cooked"
base_pack="base"
cooked_folder_content="folder"
cooked_archive_content="archive"
# dwx cooks on the host's platform: a preset that does not build for Linux takes the dwx of this
# one.
linux_dwx_preset="ci"
missing_content_message="dev.sh: cook-content needs DWE_CONTENT, the content folder extracted with \
dwx"
protected_cooked_dir_message="dev.sh: DWE_COOKED_DIR may not be inside, or contain, a protected \
folder"
repository_cooked_dir_message="dev.sh: DWE_COOKED_DIR may not be the repository itself"
unknown_main_checkout_message="dev.sh: cook-content cannot find the main checkout from git, so it \
cannot tell which folders are protected"
missing_dwx_message="dev.sh: cook-content needs a Linux build of dwx; build the preset first"
# check-gate: the tip's check with the scope of clang-tidy and the reuse of the cook decided by
# scripts/gate/gate_tool.py (docs/guides/gate.md). Its state (the markers of full clang-tidy runs
# and the cache of cooks) is shared by every worktree, in the repository's git folder unless
# DWE_GATE_STATE_DIR says otherwise.
gate_tool="$root/scripts/gate/gate_tool.py"
gate_mode_full="full"
gate_mode_incremental="incremental"
gate_tidy_mode="${DWE_GATE_TIDY:-$gate_mode_incremental}"
gate_cook_mode="${DWE_GATE_COOK:-$gate_mode_incremental}"
gate_state_folder="dwe-gate"
dwx_target="apps/dwx/dwx"
bad_gate_mode_message="dev.sh: DWE_GATE_TIDY and DWE_GATE_COOK must be full or incremental"
# The documentation site (docs/, VitePress): its build fails on a dead link. The preview server
# listens on every interface inside the container, so Docker can forward it, and on the host only
# on localhost.
docs_source="docs"
docs_container_port="5173"
docs_port="${DWE_DOCS_PORT:-$docs_container_port}"
# A site build takes about a minute; a hung one must not hold a gate's lock.
docs_build_timeout="10m"
# The formatters and linters of the C++, Lua and shell sources (scripts/style), and the hooks that
# run them and check the publication policy on a commit.
style_dir="$root/scripts/style"
hooks_dir="scripts/hooks"
style_runner_direct="direct"
# The packages (packages/ and apps/) and their dependencies, for build and test of only some of
# them (--filter, --affected), as a monorepo's task runner does.
packages_tool="$root/scripts/packages.py"
affected_base="${DWE_AFFECTED_BASE:-origin/main}"

usage() {
  cat <<EOF
Usage: scripts/dev.sh <command> [args]

  image               Build the development image
  image-name          Print the name of the image the commands run in (DWE_IMAGE or the default)
  shell               Open a shell in the container
  configure           Configure the preset (PRESET=$preset)
  build [--filter <spec>]... [--affected] [args]
                      Build the preset, or only the packages selected (Packages, below)
  test [--filter <spec>]... [--affected] [args]
                      Run the tests of the preset, in parallel, or only those of the packages
                      selected (built first)
  packages            List the packages: name, folder, kind and the packages they depend on
  affected            List the packages changed since the merge base with DWE_AFFECTED_BASE, the
                      working tree too, and every package that depends on them
  format [--check|--staged|--since <commit>]
                      Format the C++ (clang-format), Lua (StyLua) and shell (shfmt) sources, and
                      wrap the comments of Lua and shell; --check only reports, --staged formats the
                      staged ones and stages them again, --since only reports on the ones changed
                      since the commit (a pull request's check)
  lint [--staged|--since <commit>]
                      Lint the Lua (luacheck) and shell (shellcheck, the column limit) sources and
                      the NOLINT comments of C++, or only the staged ones, or only the ones changed
                      since the commit
  setup-hooks         Check the publication policy (the commit and its message) and run the
                      formatters and linters on every commit: git's hooks folder becomes $hooks_dir
  tidy [args]         Run clang-tidy on the sources in parallel, passing it args (needs a configure,
                      not a build)
  check-commit [args] The check of every commit: configure (with args), build and the tests but the
                      slow ones
  check-full [args]   The check of the tip: format, lint, the documentation site, configure (with
                      args), build, every test and clang-tidy, and cook-content into a fresh
                      build/gate-cooked when DWE_CONTENT is set and the preset builds for Linux
  check-gate [args]   check-full with the scope of clang-tidy (DWE_GATE_TIDY) and the reuse of a
                      cached cook (DWE_GATE_COOK) decided and logged by scripts/gate/gate_tool.py;
                      with both full it runs the same steps as check-full. It records full
                      clang-tidy passes and validated cooks for later gates
  check-short [args]  The push gate's check: format, lint, the documentation site, configure (with
                      args), build, every test with DWE_DISC and DWE_CONTENT unset (those on the
                      disc and the content skip) beside clang-tidy scoped as check-gate's
                      (DWE_GATE_TIDY), and the disc tests DWE_GATE_DISC_TESTS names, with both set
  windows-sdk [args]  Fetch the MSVC libraries and the Windows SDK for PRESET=windows-cross with
                      xwin, which first asks you to accept Microsoft's license (--accept-license if
                      you already have)
  cook-content        Cook the base pack of DWE_CONTENT with dwx cook into
                      DWE_COOKED_DIR/$cooked_folder_content, pack it into
                      DWE_COOKED_DIR/$cooked_archive_content/$base_pack.pak, and check both with dwx
                      validate (needs a build of the preset, or of $linux_dwx_preset for a preset
                      that does not build for Linux); an earlier cook there is updated in place. The
                      routes on a cooked pack run on the archive
  docs                Build the documentation site from docs/ into build/docs-site; a dead link
                      fails it
  docs-dev            Serve the documentation site with live reload on http://localhost:$docs_port
                      (DWE_DOCS_PORT)
  run [args]          Run the game on the host
  training-check [args]
                      Play every gym course's session headless on DWE_CONTENT (or --content) and
                      check it against the original's bounds (scripts/training-check.sh)

Packages:
  A package is a folder of packages/ or apps/ (scripts/packages.py): packages/engine/core is core,
  apps/game is game, and repo is the repository's own tests. A CMake package builds with the
  target pkg_<name> and its tests carry the ctest label <name>; the launcher builds and tests with
  scripts/launcher.sh, the map editor with scripts/mapper.sh, the beta gateway tests with node. A
  spec of --filter is a name (core), a name with its dependencies (game...), a name with its
  dependents (...core) or a folder pattern (packages/game/*). --affected selects what affected
  lists.

Environment:
  PRESET              CMake preset (default linux-clang-debug; ci for the checks)
  DWE_IMAGE           Development image (default dwe-dev)
  DWE_JOBS            Parallel jobs of the build, the tests, clang-tidy and clang-format (default:
                      the CPU count)
  DWE_CACHE_DIR       Host folder for the vcpkg binary cache (default: the $cache_volume volume)
  DWE_CCACHE_DIR      Host folder for the compiler cache (default: the $ccache_volume volume)
  DWE_CCACHE_SIZE     Size limit of the compiler cache (default $default_ccache_size)
  DWE_XWIN_DIR        Host folder for the files of windows-sdk (default: the $xwin_volume volume)
  DWE_LOG_DIR         Folder for the logs of the checks (default build/logs)
  DWE_DISC            Path to the user's disc image (.bin or .cue); its folder is mounted read-only
                      in the container at the same path, so the tools and the disc tests can read it
  DWE_CONTENT         Path to a content folder extracted with dwx (packs such as base/); it is
                      mounted read-only in the container at the same path, so the tests that need
                      real content can read it
  DWE_DECOMP          Path to a checkout of the decompilation; it is mounted read-only in the
                      container at the same path, so the tests that compare with it can read it
  DWE_COOKED_DIR      Folder for the output of cook-content (default build/cooked-content); outside
                      the repository it is mounted read-write in the container at the same path. It
                      may not be inside or contain DWE_CONTENT, DWE_DISC's folder, DWE_DECOMP,
                      ~/code/assets or the team's protected folders. check-full ignores it and cooks
                      into build/gate-cooked, emptied first
  DWE_GATE_TIDY       check-gate and check-short: full, or incremental (default) to check only what
                      changed since the last full clang-tidy on main, when one passed within a day
                      and nothing forces a full one
  DWE_GATE_COOK       check-gate: full, or incremental (default) to reuse a cook of the same inputs
                      from the last day
  DWE_CONTAINER_NAME  Start of the names of the containers (default dwe-<worktree folder>); the
                      command, the PID of the shell that starts it and a count follow
  DWE_GATE_DISC_TESTS check-short: a ctest regular expression of the tests on the user's disc to run
                      too
  DWE_GATE_STATE_DIR  check-gate and check-short: folder of the markers and the cook cache (default
                      <git common folder>/$gate_state_folder)
  DWE_DOCS_PORT       docs-dev: the port on localhost (default $docs_container_port)
  DWE_AFFECTED_BASE   affected and --affected: the branch or commit to compare with (default
                      origin/main)
  DWE_HOOK_RUNNER     format and lint: $style_runner_direct runs the formatters and linters found in
                      PATH instead of in the image (the hook's test, which runs inside the image
                      already)
EOF
}

# Every container is named, so whoever sees it running knows whose it is and what it does:
# DWE_CONTAINER_NAME (a gate's step) or dwe-<worktree>, then the command, the PID of the shell that
# starts it (BASHPID: a pipeline's part has its own) and a count of the containers that shell
# started.
container_count=0
# Extra options of the next containers' docker run, such as a published port.
container_options=()
next_container_name() {
  local shell_pid="$BASHPID"
  container_count=$((container_count + 1))
  container_name="${DWE_CONTAINER_NAME:-dwe-$(basename "$root")}"
  container_name+="-${command:-setup}-$shell_pid-$container_count"
  container_name="$(echo "$container_name" \
    | tr -c 'a-zA-Z0-9_.\n-' '-')"
}

in_container() {
  # -i always, so commands can read piped input; -t only when there is a terminal.
  local tty_flags=(-i)
  if [[ -t 0 && -t 1 ]]; then tty_flags=(-it); fi
  next_container_name
  docker run --rm --name "$container_name" "${tty_flags[@]}" \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp/home \
    -e VCPKG_DOWNLOADS=/cache/vcpkg/downloads \
    -e CTEST_PARALLEL_LEVEL="$jobs" \
    -e CCACHE_DIR=/cache/ccache \
    -e CCACHE_MAXSIZE="${DWE_CCACHE_SIZE:-$default_ccache_size}" \
    -e CCACHE_BASEDIR="$root" -e CCACHE_NOHASHDIR=true \
    -v "$root:$root" -w "$root" \
    -v "$(mount_of "$cache_dir" "$cache_volume"):/cache/vcpkg" \
    -v "$(mount_of "$ccache_dir" "$ccache_volume"):/cache/ccache" \
    -v "$(mount_of "$xwin_dir" "$xwin_volume"):/opt/xwin" \
    "${disc_flags[@]}" \
    "${container_options[@]}" \
    "$image" "$@"
}

# The disc image is the user's: mount only its folder, read-only, at the same path so DWE_DISC means
# the same inside.
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

# Content extracted from the user's disc, for the tests that need real data: read-only, at the same
# path.
if [[ -n "${DWE_CONTENT:-}" ]]; then
  if [[ ! -d "$DWE_CONTENT" ]]; then
    echo "dev.sh: DWE_CONTENT is not a folder: $DWE_CONTENT" >&2
    exit 1
  fi
  DWE_CONTENT="$(cd "$DWE_CONTENT" && pwd)"
  export DWE_CONTENT
  disc_flags+=(-v "$DWE_CONTENT:$DWE_CONTENT:ro" -e DWE_CONTENT)
fi

# A checkout of the decompilation, the reference of behaviour for the parity tests: read-only, at
# the same path.
if [[ -n "${DWE_DECOMP:-}" ]]; then
  if [[ ! -d "$DWE_DECOMP" ]]; then
    echo "dev.sh: DWE_DECOMP is not a folder: $DWE_DECOMP" >&2
    exit 1
  fi
  DWE_DECOMP="$(cd "$DWE_DECOMP" && pwd)"
  export DWE_DECOMP
  disc_flags+=(-v "$DWE_DECOMP:$DWE_DECOMP:ro" -e DWE_DECOMP)
fi

# The main checkout, the folder of the repository's shared git folder; the team's _work sits next to
# it. Without it the team's protected folders are unknown, so this fails instead of protecting less.
main_checkout_of_repository() {
  local common main
  if ! common="$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)" \
    || [[ -z "$common" ]]; then
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

# The folders cook-content never writes into, resolved: the user's disc, content and decompilation,
# and the shared folders of the team's layout around the main checkout given.
protected_folders() {
  local main_checkout="$1" work
  work="$(dirname "$main_checkout")/_work"
  local folders=("$HOME/code/assets" "$main_checkout/build/verify-p1" "$work"/content*
    "$work/p2/content" "$work/p2/scratch-routes" "$work/p3-inventory" "$work/saves-play"
    "$work/mouse-route" "$work/validate-runtime" "$work/viz")
  [[ -n "${DWE_CONTENT:-}" ]] && folders+=("$DWE_CONTENT")
  [[ -n "${DWE_DISC:-}" ]] && folders+=("$(dirname "$DWE_DISC")")
  [[ -n "${DWE_DECOMP:-}" ]] && folders+=("$DWE_DECOMP")
  local folder
  for folder in "${folders[@]}"; do
    realpath -m "$folder"
  done
}

# Fails when a folder is the repository, a protected folder, inside one, or holds one. Both sides
# are resolved, symbolic links too. The main checkout is found first, outside the loop's input, so a
# failure stops the script.
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

# The output of cook-content: checked before anything is written, then created so it belongs to the
# host user and not to the container's root. The repository is mounted already; any other folder is
# mounted read-write at the same path.
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
  next_container_name
  docker run --rm --name "$container_name" -v "$volume:/cache" "$image" chmod a+rwx /cache
}

ensure_caches() {
  ensure_cache "$cache_dir" "$cache_volume"
  ensure_cache "$ccache_dir" "$ccache_volume"
  ensure_cache "$xwin_dir" "$xwin_volume"
}

# git with the paths as they are, not quoted, non-ASCII names too.
git_paths() {
  git -C "$root" -c core.quotePath=false "$@"
}

sources() {
  git_paths ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp'
}

# The engine's own Lua: the game's scripts and the packs' (the extracted story is dwx's output, not
# a source).
lua_sources() {
  git_paths ls-files --cached --others --exclude-standard -- \
    ':(glob)apps/**/*.lua' ':(glob)content/packs/**/*.lua'
}

shell_sources() {
  git_paths ls-files --cached --others --exclude-standard \
    -- ':(glob)scripts/**/*.sh' "$hooks_dir/*"
}

style_sources() {
  sources
  lua_sources
  shell_sources
}

# The style sources in the index, added or changed, that the working tree has as staged: a file
# with changes not staged, or deleted, is left out (named on stderr with --report), so the hook
# never touches the working tree's own changes.
staged_style_sources() {
  local file
  while IFS= read -r file; do
    if [[ ! -e "$root/$file" ]]; then
      [[ "${1:-}" != "--report" ]] || echo "pre-commit: $file is deleted in the working tree;" \
        "not formatted or linted, the gate checks it" >&2
    elif ! git -C "$root" --literal-pathspecs diff --quiet -- "$file"; then
      [[ "${1:-}" != "--report" ]] || echo "pre-commit: $file has unstaged changes;" \
        "not formatted or linted, the gate checks it" >&2
    else
      echo "$file"
    fi
  done < <(comm -12 <(git_paths diff --cached --name-only --diff-filter=ACMR | sort) \
    <(style_sources | sort))
}

# Runs a formatter or linter of scripts/style from the root, in the image or, with
# DWE_HOOK_RUNNER=direct, as is.
run_style() {
  if [[ "${DWE_HOOK_RUNNER:-}" == "$style_runner_direct" ]]; then
    (cd "$root" && "$@")
  else
    ensure_caches
    in_container "$@"
  fi
}

# The style sources added or changed since a commit, as the working tree has them.
changed_style_sources() {
  comm -12 <(style_sources | sort -u) \
    <(git -C "$root" diff --name-only --diff-filter=d "$1" -- | sort -u)
}

# Formats the staged style sources and stages them again; staged_style_sources leaves out the files
# whose working tree differs from the index, so staging them adds only the formatting.
format_staged() {
  local files=()
  mapfile -t files < <(staged_style_sources --report)
  ((${#files[@]} > 0)) || return 0
  run_style "$style_dir/format_files.sh" --jobs "$jobs" -- "${files[@]}" || return 1
  git -C "$root" --literal-pathspecs add -- "${files[@]}"
}

# Fails when any C++ or Lua source of the repository uses goto (project rule: structured control
# flow only).
no_goto() {
  local found
  found="$(git -C "$root" ls-files --cached --others --exclude-standard -z \
    -- '*.cpp' '*.hpp' '*.h' '*.lua' \
    | xargs -0 -r grep -nwH 'goto' --)" || true
  [[ -z "$found" ]] && return 0
  printf 'goto is not allowed:\n%s\n' "$found"
  return 1
}

# One clang-tidy per file, its output printed in one piece so parallel runs do not mix their lines.
# Any diagnostic that fails clang-tidy fails the whole run: xargs exits with 123.
# shellcheck disable=SC2016 # code for bash -c, which expands it
tidy_one='out="$(clang-tidy "$@" 2>&1)"; rc=$?; [[ -z "$out" ]] || printf "%s\n" "$out"; '
# shellcheck disable=SC2016 # code for bash -c, which expands it
tidy_one+='exit $((rc == 0 ? 0 : 1))'

# step <name> [--forbid <regex>] <command...>: runs a step of a check with its output in
# $log_dir/<name>.log and prints its result. With --forbid, a log line that matches fails the step
# even when the command passed.
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

# Checks a cook with dwx validate: the cooked folder, then the repository's game pack mounted over
# the archive, which checks the archive too. Reads dwx, cooked, archive and schemas.
# shellcheck disable=SC2016 # code for bash -c, which expands it
validate_cooked_steps='"$dwx" validate --engine --schemas "$schemas" "$(dirname "$cooked")"
"$dwx" validate --engine --schemas "$schemas" --with "$(dirname "$archive")" content/packs/game'

# cook-content inside the container: cooks the extracted base pack, packs the cooked pack into a
# .pak, and validates both. dwx cook updates an earlier cook in place and refuses a folder it did
# not write. The old archive goes first, so a failed cook leaves none behind.
# Arguments: dwx, the extracted pack, the cooked pack folder, the archive.
# shellcheck disable=SC2016 # code for bash -c, which expands it
cook_content='set -euo pipefail
dwx="$1" source="$2" cooked="$3" archive="$4" schemas=content/schemas
rm -f "$archive"
"$dwx" cook "$source" -o "$cooked"
mkdir -p "$(dirname "$archive")"
"$dwx" pack "$cooked" -o "$archive"
'"$validate_cooked_steps"

# The validation alone, of a cook check-gate restored from its cache. Arguments: dwx, the cooked
# pack folder, the archive.
# shellcheck disable=SC2016 # code for bash -c, which expands it
validate_cooked='set -euo pipefail
dwx="$1" cooked="$2" archive="$3" schemas=content/schemas
'"$validate_cooked_steps"

# Whether a preset builds programs that run here, on Linux.
builds_for_linux() {
  [[ "$1" == "$linux_dwx_preset" || "$1" == linux-* ]]
}

# The packages that --filter <spec> and --affected select (scripts/packages.py), as name<TAB>kind
# lines in selected_packages; package_selection says whether either was given, and package_args
# keeps the other arguments.
package_selection=false
selected_packages=()
package_args=()
select_packages() {
  local specs=() affected=false listed=""
  while (($#)); do
    case "$1" in
      --filter)
        [[ $# -ge 2 ]] || {
          echo "dev.sh: --filter needs a package" >&2
          exit 1
        }
        specs+=("$2")
        shift 2
        ;;
      --filter=*)
        specs+=("${1#--filter=}")
        shift
        ;;
      --affected)
        affected=true
        shift
        ;;
      *)
        package_args+=("$1")
        shift
        ;;
    esac
  done
  if ((${#specs[@]})); then
    package_selection=true
    listed+="$(python3 "$packages_tool" select --kinds "${specs[@]}")"$'\n'
  fi
  if [[ "$affected" == true ]]; then
    package_selection=true
    listed+="$(python3 "$packages_tool" affected --kinds --base "$affected_base")"$'\n'
  fi
  mapfile -t selected_packages < <(sed '/^$/d' <<<"$listed" | sort -u)
  if [[ "$package_selection" == true ]]; then
    echo "dev.sh: packages: $(cut -f1 <<<"$listed" | sed '/^$/d' | sort -u | tr '\n' ' ')" >&2
  fi
}

# Builds (build) or tests (test) the selected packages: the CMake ones with their pkg_<name> targets
# and their ctest labels, the others with their own tools.
run_packages() {
  local action="$1" entry name kind targets=() labels=()
  for entry in "${selected_packages[@]}"; do
    name="${entry%%$'\t'*}"
    kind="${entry##*$'\t'}"
    case "$kind" in
      cmake | repo)
        targets+=("pkg_$name")
        labels+=("$name")
        ;;
      *) run_other_package "$action" "$name" ;;
    esac
  done
  ((${#targets[@]})) || return 0
  ensure_caches
  if [[ "$action" == build ]]; then
    in_container cmake --build --preset "$preset" -j "$jobs" --target "${targets[@]}" \
      "${package_args[@]}"
    return
  fi
  in_container cmake --build --preset "$preset" -j "$jobs" --target "${targets[@]}"
  if [[ "$action" == test ]]; then
    local pattern
    pattern="^($(
      IFS='|'
      echo "${labels[*]}"
    ))\$"
    in_container ctest --preset "$preset" -L "$pattern" "${package_args[@]}"
  fi
}

run_other_package() {
  local action="$1" name="$2"
  case "$name" in
    launcher) "$root/scripts/launcher.sh" "$action" ;;
    mapper) "$root/scripts/mapper.sh" "$action" ;;
    beta-gateway)
      if [[ "$action" != test ]]; then
        echo "dev.sh: beta-gateway has nothing to build" >&2
      elif command -v node >/dev/null; then
        (cd "$root/apps/beta-gateway" && node --test)
      else
        echo "dev.sh: beta-gateway's tests need node on the host; skipped" >&2
      fi
      ;;
    *) echo "dev.sh: $name has no $action here; see apps/$name/README.md" >&2 ;;
  esac
}

# clang-tidy, one process per source read from the input, in parallel at DWE_NICE.
run_tidy() {
  in_container nice -n "${DWE_NICE:-0}" xargs -r -P "$jobs" -n 1 bash -c "$tidy_one" tidy \
    -p "build/$preset" --quiet "$@"
}

gate_state_dir() {
  if [[ -n "${DWE_GATE_STATE_DIR:-}" ]]; then
    echo "$DWE_GATE_STATE_DIR"
    return
  fi
  echo "$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)/$gate_state_folder"
}

image_id() {
  docker image inspect --format '{{.Id}}' "$image"
}

tidy_version() {
  in_container clang-tidy --version </dev/null | tr -s ' \n' ' ' | sed 's/ $//'
}

# check-gate's clang-tidy: the scope gate_tool.py plans (logged in tidy-plan.log and at the top of
# tidy.log), every source when it says full, as check-full, or only the sources it lists.
gate_tidy() {
  # Also run on its own (dev.sh gate-tidy), not only inside a check that made the folder.
  mkdir -p "$log_dir"
  python3 "$gate_tool" tidy-plan --repo "$root" --state "$(gate_state_dir)" \
    --compile-commands "$root/build/$preset/compile_commands.json" --image-id "$(image_id)" \
    --tool-version "$(tidy_version)" --mode "$gate_tidy_mode" --list-out "$log_dir/tidy-files.txt" \
    --scope-out "$log_dir/tidy-scope.txt" --log "$log_dir/tidy-plan.log" || return 1
  if [[ "$(cat "$log_dir/tidy-scope.txt")" == "$gate_mode_full" ]]; then
    sources | grep '\.cpp$' | run_tidy "$@"
  else
    run_tidy "$@" <"$log_dir/tidy-files.txt"
  fi
}

# After a full clang-tidy of check-gate passed: the marker later gates start an incremental one
# from.
gate_mark_tidy() {
  [[ "$(cat "$log_dir/tidy-scope.txt")" == "$gate_mode_full" ]] || return 0
  python3 "$gate_tool" tidy-mark --repo "$root" --state "$(gate_state_dir)" \
    --image-id "$(image_id)" --tool-version "$(tidy_version)"
}

# check-gate's cook-content: the cook of the key of every input (cook-key.json, each part in the
# log) restored from the cache and validated again, or, on any miss, a cook from scratch as
# check-full's, stored in the cache once validated.
gate_cook() {
  local state key="$log_dir/cook-key.json" inputs="$log_dir/dwx-inputs.txt"
  local deps="$log_dir/dwx-deps.txt"
  state="$(gate_state_dir)"
  in_container ninja -C "build/$preset" -t inputs "$dwx_target" </dev/null >"$inputs" || return 1
  # The headers each object of the tool included, from the deps log of the build just made.
  grep '\.o$' "$inputs" | in_container xargs -r ninja -C "build/$preset" -t deps >"$deps" \
    || return 1
  python3 "$gate_tool" cook-key --repo "$root" --build-dir "$root/build/$preset" \
    --ninja-inputs "$inputs" --ninja-deps "$deps" --content "$DWE_CONTENT/$base_pack" \
    --image-id "$(image_id)" --preset "$preset" --key-out "$key" || return 1
  if python3 "$gate_tool" cook-restore --repo "$root" --state "$state" --key "$key" \
    --to "$gate_cooked_dir" --mode "$gate_cook_mode"; then
    in_container bash -c "$validate_cooked" validate-cooked "build/$preset/$dwx_target" \
      "$gate_cooked_dir/$cooked_folder_content/$base_pack" \
      "$gate_cooked_dir/$cooked_archive_content/$base_pack.pak"
    return
  fi
  rm -rf "$gate_cooked_dir"
  env DWE_COOKED_DIR="$gate_cooked_dir" "$self" cook-content || return 1
  python3 "$gate_tool" cook-store --repo "$root" --state "$state" --key "$key" \
    --cooked "$gate_cooked_dir"
}

# Prints the gate: lines of a step's log, the decisions the coordinator checks in the gate's log.
print_gate_lines() {
  grep -h '^gate: ' "$@" 2>/dev/null || true
}

is_gate_mode() {
  [[ "$1" == "$gate_mode_full" || "$1" == "$gate_mode_incremental" ]]
}

# The checks of a commit, and of the tip (full, gate and short). The tip adds the format and
# clang-tidy, and full and gate the slow tests and the cook; each tier's steps after the build are
# in check_<tier>_tip.
check() {
  local tier="$1" status=0 steps tip=true
  shift
  [[ "$tier" == "commit" ]] && tip=false
  export PRESET="${PRESET:-ci}"
  preset="$PRESET"
  mkdir -p "$log_dir"
  echo "check-$tier: preset $preset, $jobs jobs, logs in $log_dir"
  if [[ "$tier" == "gate" || "$tier" == "short" ]]; then
    if [[ "$tier" == "short" ]]; then
      echo "gate: tier=check-$tier tidy-mode=$gate_tidy_mode"
      echo "gate: cook=none reason=check-short does not cook; check-gate does"
    else
      echo "gate: tier=check-$tier tidy-mode=$gate_tidy_mode cook-mode=$gate_cook_mode"
    fi
    if ! is_gate_mode "$gate_tidy_mode" || ! is_gate_mode "$gate_cook_mode"; then
      echo "$bad_gate_mode_message" >&2
      return 1
    fi
    rm -f "$log_dir"/tidy-plan.log "$log_dir"/tidy-scope.txt "$log_dir"/tidy-files.txt \
      "$log_dir"/cook-key.json
  fi
  if [[ "$tier" == "full" || "$tier" == "gate" ]]; then
    [[ -n "${DWE_DISC:-}" ]] || echo "DWE_DISC is not set: the tests on the user's disc skip"
  fi
  if $tip; then
    step format "$self" format --check || status=1
    step lint "$self" lint || status=1
    step docs "$self" docs || status=1
  fi
  step no-goto no_goto || status=1
  # Warnings fail the build already, but not the linker's; clang-tidy fails on any diagnostic, and
  # the log says so too.
  if step configure "$self" configure "$@" && step build --forbid 'warning:' "$self" build; then
    case "$tier" in
      full) check_full_tip || status=1 ;;
      gate) check_gate_tip || status=1 ;;
      short) check_short_tip || status=1 ;;
      *) step test "$self" test -LE slow || status=1 ;;
    esac
  else
    status=1
  fi
  steps=(no-goto configure build test)
  $tip && steps=(format lint docs "${steps[@]}" tidy)
  [[ "$tier" == "short" && -n "${DWE_GATE_DISC_TESTS:-}" ]] && steps+=(disc-tests)
  if [[ "$tier" == "full" || "$tier" == "gate" ]] && [[ -n "${DWE_CONTENT:-}" ]] \
    && builds_for_linux "$preset"; then
    steps+=(cook-content)
  fi
  print_logs "${steps[@]}"
  echo "[$(date +%T)] check-$tier $([[ $status -eq 0 ]] && echo passed || echo FAILED)"
  return "$status"
}

# check-full after the build: every test and clang-tidy side by side (clang-tidy reads only the
# compile commands, so it runs at a lower priority next to the tests), then cook-content.
check_full_tip() {
  local status=0 test_pid
  step test "$self" test &
  test_pid=$!
  step tidy --forbid 'warning:|error:' env DWE_NICE=10 "$self" tidy --warnings-as-errors='*' \
    || status=1
  wait "$test_pid" || status=1
  # The routes on a cooked pack run on its output (docs/guides/prueba-vertical.md), cooked from
  # scratch so nothing of an earlier cook can hide a regression.
  if [[ -n "${DWE_CONTENT:-}" ]] && builds_for_linux "$preset"; then
    rm -rf "$gate_cooked_dir"
    step cook-content env DWE_COOKED_DIR="$gate_cooked_dir" "$self" cook-content || status=1
  elif [[ -n "${DWE_CONTENT:-}" ]]; then
    echo "cook-content skipped: preset $preset does not build for Linux"
  fi
  return "$status"
}

# check-gate's clang-tidy, at a lower priority next to the given test step; a full pass leaves its
# marker.
gate_tidy_beside() {
  local status=0 test_pid
  "$@" &
  test_pid=$!
  if step tidy --forbid 'warning:|error:' \
    env DWE_NICE=10 "$self" gate-tidy --warnings-as-errors='*'; then
    "$self" gate-tidy-mark || status=1
  else
    status=1
  fi
  print_gate_lines "$log_dir/tidy-plan.log"
  wait "$test_pid" || status=1
  return "$status"
}

# check-gate after the build: check-full's steps, with the scope of clang-tidy and the reuse of the
# cook decided.
check_gate_tip() {
  local status=0
  gate_tidy_beside step test "$self" test || status=1
  if [[ -n "${DWE_CONTENT:-}" ]] && builds_for_linux "$preset"; then
    step cook-content "$self" gate-cook || status=1
    print_gate_lines "$log_dir/cook-content.log"
  elif [[ -n "${DWE_CONTENT:-}" ]]; then
    echo "cook-content skipped: preset $preset does not build for Linux"
  fi
  return "$status"
}

# check-short after the build: every test, with no label left out, beside the decided clang-tidy,
# and the tests on the user's disc that DWE_GATE_DISC_TESTS names (a ctest regular expression;
# matching none fails). The full run has DWE_DISC and DWE_CONTENT unset, so the tests on the disc or
# the extracted content skip at once in it; only the named ones read them, with both set.
check_short_tip() {
  local status=0
  gate_tidy_beside step test env -u DWE_DISC -u DWE_CONTENT "$self" test || status=1
  if [[ -n "${DWE_GATE_DISC_TESTS:-}" ]]; then
    # Verbose, so the log has each test's command line (its filter) and doctest's count of
    # assertions.
    step disc-tests "$self" test -V --no-tests=error -R "$DWE_GATE_DISC_TESTS" || status=1
  fi
  return "$status"
}

command="${1:-}"
shift || true

case "$command" in
  image-name)
    echo "$image"
    ;;
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
  build | test)
    select_packages "$@"
    if [[ "$package_selection" == true ]]; then
      run_packages "$command"
    elif [[ "$command" == build ]]; then
      ensure_caches
      in_container cmake --build --preset "$preset" -j "$jobs" "${package_args[@]}"
    else
      ensure_caches
      in_container ctest --preset "$preset" "${package_args[@]}"
    fi
    ;;
  packages)
    python3 "$packages_tool" list | column -t -s $'\t'
    ;;
  affected)
    python3 "$packages_tool" affected --base "$affected_base" "$@"
    ;;
  format)
    case "${1:-}" in
      --staged) format_staged ;;
      --since)
        mapfile -t files < <(changed_style_sources "${2:?format --since needs a commit}")
        echo "format: ${#files[@]} changed sources"
        ((${#files[@]} == 0)) \
          || run_style "$style_dir/format_files.sh" --check --jobs "$jobs" -- "${files[@]}"
        ;;
      --check | "")
        mapfile -t files < <(style_sources)
        run_style "$style_dir/format_files.sh" ${1:+"$1"} --jobs "$jobs" -- "${files[@]}"
        ;;
      *)
        usage
        exit 1
        ;;
    esac
    # The formatters may have changed this file: bash must not read on from where it was in it.
    exit
    ;;
  lint)
    if [[ "${1:-}" == "--staged" ]]; then
      mapfile -t files < <(staged_style_sources)
    elif [[ "${1:-}" == "--since" ]]; then
      mapfile -t files < <(changed_style_sources "${2:?lint --since needs a commit}")
      echo "lint: ${#files[@]} changed sources"
    else
      mapfile -t files < <(style_sources)
    fi
    ((${#files[@]} == 0)) || run_style "$style_dir/lint_files.sh" -- "${files[@]}"
    ;;
  setup-hooks)
    git -C "$root" config core.hooksPath "$hooks_dir"
    echo "git runs $hooks_dir/pre-commit and commit-msg on every commit of this clone" \
      "(DWE_SKIP_HOOK=1 or git commit --no-verify skips them)"
    ;;
  tidy)
    ensure_caches
    sources | grep '\.cpp$' | run_tidy "$@"
    ;;
  windows-sdk)
    # xwin unpacks next to the result and moves the files into place, which only works on the same
    # filesystem, so it unpacks inside the volume and the unpacked copies are deleted afterwards:
    # the volume keeps only the layout of a Visual Studio installation. xwin names the SDK's folders
    # in lower case, and /winsysroot looks for Include and Lib.
    ensure_caches
    in_container bash -c 'rm -rf /opt/xwin/sysroot \
            && xwin --cache-dir /opt/xwin/unpack "$@" splat --include-debug-libs \
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
  check-gate)
    check gate "$@"
    ;;
  check-short)
    check short "$@"
    ;;
  gate-tidy)
    # The steps of check-gate and check-short, run by step so their output lands in the step's log.
    ensure_caches
    gate_tidy "$@"
    ;;
  gate-tidy-mark)
    gate_mark_tidy
    ;;
  gate-cook)
    ensure_caches
    gate_cook
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
      "$cooked_dir/$cooked_folder_content/$base_pack" \
      "$cooked_dir/$cooked_archive_content/$base_pack.pak"
    ;;
  docs)
    ensure_caches
    in_container timeout "$docs_build_timeout" vitepress build "$docs_source"
    ;;
  docs-dev)
    ensure_caches
    container_options=(-p "127.0.0.1:$docs_port:$docs_container_port")
    in_container vitepress dev "$docs_source" --host 0.0.0.0 --port "$docs_container_port" \
      --strictPort
    ;;
  training-check)
    exec "$root/scripts/training-check.sh" "$@"
    ;;
  run)
    build_dir="$root/build/$preset"
    installed="$build_dir/vcpkg_installed/x64-linux-libcxx"
    if [[ -d "$installed/share/vulkan/explicit_layer.d" ]]; then
      export VK_LAYER_PATH="$installed/share/vulkan/explicit_layer.d"
      # The layer manifest names the library without a path. Search the release libraries first: the
      # debug build of the validation layer is many times slower and says so on every start.
      export LD_LIBRARY_PATH="$installed/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    fi
    exec "$build_dir/apps/game/dwe-game" "$@"
    ;;
  help | -h | --help)
    usage
    ;;
  *)
    usage
    [[ -z "$command" ]] && exit 0 || exit 1
    ;;
esac
