#!/usr/bin/env bash
# Tests of remove_build_dir (scripts/gate/build_dirs.sh): it deletes only folders inside the
# worktree's build/.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../build_dirs.sh"

failures=0
check() {
  local description="$1"
  shift
  if "$@"; then echo "ok: $description"; else
    echo "FAILED: $description"
    failures=$((failures + 1))
  fi
}

# check_refused <description> <folder>: remove_build_dir must fail on it.
check_refused() {
  if remove_build_dir "$root" "$2" 2>/dev/null; then
    echo "FAILED: $1"
    failures=$((failures + 1))
  else
    echo "ok: $1"
  fi
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
root="$scratch/repo"
outside="$scratch/outside"
mkdir -p "$root/build/windows-cross/sub" "$root/src" "$outside/keep"
ln -s "$outside" "$root/build/escape"

check "a folder inside build/ is deleted" remove_build_dir "$root" "$root/build/windows-cross"
check "  and is gone" test ! -e "$root/build/windows-cross"
check "a missing folder inside build/ is nothing to do" \
  remove_build_dir "$root" "$root/build/missing"
check_refused "build/ itself is refused" "$root/build"
check "  and kept" test -d "$root/build"
check_refused "a folder outside build/ is refused" "$root/src"
check "  and kept" test -d "$root/src"
check_refused "a path climbing out with .. is refused" "$root/build/../src"
check_refused "a symbolic link in build/ to a folder outside is refused" "$root/build/escape"
check "  and its target is kept" test -d "$outside/keep"
check_refused "a lookalike prefix is refused" "$root/build-other"
check_refused "the root is refused" "$root"

# A build/ that is itself a link elsewhere: nothing under it is the worktree's.
linked_root="$scratch/linked"
mkdir -p "$linked_root" "$outside/elsewhere/windows-cross"
ln -s "$outside/elsewhere" "$linked_root/build"
if remove_build_dir "$linked_root" "$linked_root/build/windows-cross" 2>/dev/null; then
  echo "FAILED: a folder under a build/ that is a symbolic link is refused"
  failures=$((failures + 1))
else
  echo "ok: a folder under a build/ that is a symbolic link is refused"
fi
check "  and its target is kept" test -d "$outside/elsewhere/windows-cross"

((failures == 0)) || {
  echo "$failures failed"
  exit 1
}
echo "all passed"
