#!/usr/bin/env bash
# Tests of the pre-commit hook (scripts/hooks/pre-commit) in a scratch repository: with
# DWE_HOOK_RUNNER=direct it runs the formatters and linters found in PATH, the image's own when
# ctest runs in it; a stub docker stands for a host without Docker.
set -uo pipefail

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
style_files=(.clang-format .editorconfig stylua.toml .luacheckrc .shellcheckrc scripts/dev.sh
  scripts/hooks/pre-commit scripts/style/format_files.sh scripts/style/lint_files.sh
  scripts/style/wrap_comments.py)

failures=0
check() {
  local description="$1"
  shift
  if "$@"; then
    echo "ok: $description"
  else
    echo "FAILED: $description"
    failures=$((failures + 1))
  fi
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
repo="$scratch/repo"
mkdir -p "$repo"
for file in "${style_files[@]}"; do
  mkdir -p "$repo/$(dirname "$file")"
  cp "$source_root/$file" "$repo/$file"
done
git_in() { git -C "$repo" -c user.name=test -c user.email=test@example.invalid "$@"; }
git_in init -q
git_in add -A
git_in commit -q --no-verify -m "The style tools"
"$repo/scripts/dev.sh" setup-hooks >/dev/null
export DWE_HOOK_RUNNER=direct

commit() { git_in commit -q -m "$1" >"$scratch/commit.log" 2>&1; }
committed() { git_in show "HEAD:$1"; }
same_text() { [[ "$1" == "$2" ]]; }
log_says() { grep -q "$1" "$scratch/commit.log"; }
fails() { ! "$@"; }

# Only the staged C++, Lua and shell files are formatted, and they are committed formatted.
mkdir -p "$repo/engine" "$repo/apps/game/scripts" "$repo/scripts/tools" "$repo/docs"
printf 'int answer() {\n        return 42;\n}\n' >"$repo/engine/answer.cpp"
printf 'local value = 1\nif value then\n        print(value)\nend\n' \
  >"$repo/apps/game/scripts/sample.lua"
printf '#!/usr/bin/env bash\nif true; then\n        echo staged\nfi\n' \
  >"$repo/scripts/tools/staged.sh"
printf '#!/usr/bin/env bash\nif true; then\n        echo unstaged\nfi\n' \
  >"$repo/scripts/tools/unstaged.sh"
printf 'keep\n        as is\n' >"$repo/docs/notes.txt"
unstaged_before="$(cat "$repo/scripts/tools/unstaged.sh")"
git_in add engine/answer.cpp apps/game/scripts/sample.lua scripts/tools/staged.sh docs/notes.txt
check "a commit of unformatted staged files passes" commit "Unformatted files"
check "  the C++ file is committed formatted" \
  same_text "$(committed engine/answer.cpp)" $'int answer() {\n  return 42;\n}'
check "  the Lua file is committed formatted" \
  same_text "$(committed apps/game/scripts/sample.lua)" \
  $'local value = 1\nif value then\n  print(value)\nend'
check "  the shell file is committed formatted" \
  same_text "$(committed scripts/tools/staged.sh)" \
  $'#!/usr/bin/env bash\nif true; then\n  echo staged\nfi'
check "  and nothing is left to stage" git_in diff --quiet
check "  a file that is not staged is not touched" \
  same_text "$(cat "$repo/scripts/tools/unstaged.sh")" "$unstaged_before"
check "  a staged file of another kind is not touched" \
  same_text "$(committed docs/notes.txt)" $'keep\n        as is'

# A file with changes both staged and not is formatted but not staged again: the commit stops.
printf 'int answer() {\n        return 43;\n}\n' >"$repo/engine/answer.cpp"
git_in add engine/answer.cpp
printf '\n' >>"$repo/engine/answer.cpp"
head_before="$(git_in rev-parse HEAD)"
check "a partly staged file that formatting changes stops the commit" fails commit "Partly staged"
check "  with the message" log_says "stage it again"
check "  and nothing is committed" same_text "$(git_in rev-parse HEAD)" "$head_before"
git_in checkout -q -- engine/answer.cpp
git_in reset -q --hard

# A lint error stops the commit, Lua and shell alike.
mkdir -p "$repo/content/packs/test/ui"
printf 'undefined_function()\n' >"$repo/content/packs/test/ui/broken.lua"
git_in add content/packs/test/ui/broken.lua
check "a luacheck error stops the commit" fails commit "A Lua error"
check "  with the message" log_says "The commit stops"
check "  and names the error" log_says "undefined_function"
git_in rm -q --cached content/packs/test/ui/broken.lua
# shellcheck disable=SC2016 # the text of a script with an unquoted argument
printf '#!/usr/bin/env bash\ncd $1\n' >"$repo/scripts/tools/broken.sh"
git_in add scripts/tools/broken.sh
check "a shellcheck error stops the commit" fails commit "A shell error"
check "  with the message" log_says "The commit stops"
check "  and names the error" log_says "SC2086"
git_in rm -q --cached scripts/tools/broken.sh
unquoted_key="first-route"
# shellcheck disable=SC2016 # the text of a script that keeps a key without quotes
printf '#!/usr/bin/env bash\ndeclare -A routes=([%s]=second)\necho "${routes[@]}"\n' \
  "$unquoted_key" >"$repo/scripts/tools/keys.sh"
git_in add scripts/tools/keys.sh
check "an array key with hyphens and no quotes stops the commit" fails commit "An unquoted key"
check "  with the message" log_says "quote the array keys"
git_in rm -q --cached scripts/tools/keys.sh
printf 'int* leak() {\n  return new int{1}; // NOLINT(cppcoreguidelines-owning-memory)\n}\n' \
  >"$repo/engine/leak.cpp"
git_in add engine/leak.cpp
check "a NOLINT after code stops the commit" fails commit "A trailing NOLINT"
check "  with the message" log_says "NOLINTNEXTLINE"
git_in rm -q --cached engine/leak.cpp
check "  and nothing was committed" same_text "$(git_in rev-parse HEAD)" "$head_before"

# Without Docker the hook warns and lets the commit through, unformatted.
stubs="$scratch/stubs"
mkdir -p "$stubs"
printf '#!/bin/sh\nexit 1\n' >"$stubs/docker"
cp "$stubs/docker" "$stubs/sg"
chmod +x "$stubs/docker" "$stubs/sg"
printf 'int other() {\n        return 1;\n}\n' >"$repo/engine/other.cpp"
git_in add engine/other.cpp
without_docker() { PATH="$stubs:$PATH" DWE_HOOK_RUNNER="" "$@"; }
check "without Docker the commit passes" without_docker commit "No Docker"
check "  with a warning" log_says "Docker is not reachable"
check "  unformatted" \
  same_text "$(committed engine/other.cpp)" $'int other() {\n        return 1;\n}'

((failures == 0)) || {
  echo "$failures failed"
  exit 1
}
echo "all passed"
