#!/usr/bin/env bash
# Tests of the pre-commit hook (scripts/hooks/pre-commit) in a scratch repository: with
# DWE_HOOK_RUNNER=direct it runs the formatters and linters found in PATH, the image's own when
# ctest runs in it; a stub docker stands for a host without Docker.
set -uo pipefail

# The user's and the system's git configuration stay out of the scratch repository.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
style_files=(.clang-format .editorconfig stylua.toml .luacheckrc .shellcheckrc scripts/dev.sh
  scripts/hooks/pre-commit scripts/hooks/commit-msg scripts/hooks/policy.sh
  scripts/style/format_files.sh scripts/style/lint_files.sh scripts/style/wrap_comments.py)
# The identity the hooks require (scripts/hooks/policy.sh); test_policy_hooks.sh tests the policy.
identity=(-c user.name=Rivarux -c user.email=2211799+Rivarux@users.noreply.github.com)

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
git_in() { git -C "$repo" "${identity[@]}" "$@"; }
git_in init -q
git_in add -A
git_in commit -q --no-verify -m "The style tools"
"$repo/scripts/dev.sh" setup-hooks >/dev/null
export DWE_HOOK_RUNNER=direct

commit() { git_in commit -q -s -m "$1" >"$scratch/commit.log" 2>&1; }
# commit_with <variable=value...>: a commit with the hook's environment changed.
commit_with() { env "$@" git -C "$repo" "${identity[@]}" \
  commit -q -s -m "With $*" >"$scratch/commit.log" 2>&1; }
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

# A file with changes both staged and not is left as it is, in the index and in the working tree.
printf 'int answer() {\n        return 43;\n}\n' >"$repo/engine/answer.cpp"
git_in add engine/answer.cpp
printf '\n' >>"$repo/engine/answer.cpp"
staged_before="$(git_in show :engine/answer.cpp)"
working_before="$(cat "$repo/engine/answer.cpp")"
check "a partly staged file does not stop the commit" commit "Partly staged"
check "  it is named" log_says "engine/answer.cpp has unstaged changes; not formatted or linted"
check "  the staged text is committed as it was" \
  same_text "$(committed engine/answer.cpp)" "$staged_before"
check "  and the working tree keeps its own" \
  same_text "$(cat "$repo/engine/answer.cpp")" "$working_before"
git_in reset -q --hard

# A staged file deleted in the working tree is left as it is.
printf 'int gone() {\n        return 0;\n}\n' >"$repo/engine/gone.cpp"
git_in add engine/gone.cpp
rm "$repo/engine/gone.cpp"
check "a staged file deleted in the working tree does not stop the commit" commit "Deleted"
check "  it is named" log_says "engine/gone.cpp is deleted in the working tree"
check "  and committed as staged" \
  same_text "$(committed engine/gone.cpp)" $'int gone() {\n        return 0;\n}'
git_in reset -q --hard

# Renamed files and names with spaces or non-ASCII letters are formatted under their new name.
git_in mv engine/answer.cpp engine/renamed.cpp
printf 'int renamed() {\n        return 2;\n}\n' >"$repo/engine/renamed.cpp"
printf 'int spaced() {\n        return 3;\n}\n' >"$repo/engine/with space.cpp"
printf 'int accented() {\n        return 4;\n}\n' >"$repo/engine/año.cpp"
git_in add engine/renamed.cpp "engine/with space.cpp" engine/año.cpp
check "a rename and unusual names pass" commit "Names"
check "  the renamed file is committed formatted" \
  same_text "$(committed engine/renamed.cpp)" $'int renamed() {\n  return 2;\n}'
check "  the name with a space too" \
  same_text "$(committed "engine/with space.cpp")" $'int spaced() {\n  return 3;\n}'
check "  and the non-ASCII name" \
  same_text "$(committed engine/año.cpp)" $'int accented() {\n  return 4;\n}'

# The hook formats itself while it runs: bash must not read on in the rewritten file.
sed -i 's/^  local root dev image$/    local root dev image/' "$repo/scripts/hooks/pre-commit"
git_in add scripts/hooks/pre-commit
check "a commit of the hook itself, unformatted, passes" commit "The hook"
check "  without errors from the rewritten file" fails log_says "command not found\|syntax error"
check "  and commits it formatted" \
  same_text "$(committed scripts/hooks/pre-commit)" "$(cat "$source_root/scripts/hooks/pre-commit")"
head_before="$(git_in rev-parse HEAD)"

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

# DWE_SKIP_HOOK=1 skips the hook: one line, and the commit goes through unformatted.
printf 'int skipped() {\n        return 1;\n}\n' >"$repo/engine/skipped.cpp"
git_in add engine/skipped.cpp
check "with DWE_SKIP_HOOK=1 the commit passes" commit_with DWE_SKIP_HOOK=1
check "  with one line" test "$(wc -l <"$scratch/commit.log")" -eq 1
check "  that says so" log_says "DWE_SKIP_HOOK=1"
check "  unformatted" \
  same_text "$(committed engine/skipped.cpp)" $'int skipped() {\n        return 1;\n}'

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

# An image without the formatters warns in one line, as without Docker, and lets the commit
# through: the stub docker answers, has the image and lacks two tools.
# shellcheck disable=SC2016 # the text of a stub that reads its own argument
printf '#!/bin/sh\n[ "$1" = run ] && printf "stylua shfmt "\nexit 0\n' >"$stubs/docker"
printf 'int lacking() {\n        return 1;\n}\n' >"$repo/engine/lacking.cpp"
git_in add engine/lacking.cpp
check "with an image without the formatters the commit passes" without_docker commit "No tools"
check "  with one line" test "$(wc -l <"$scratch/commit.log")" -eq 1
check "  that names the missing tools and the image that has them" \
  log_says "has no stylua shfmt, so .*DWE_IMAGE=dwe-dev-speed"
check "  unformatted" \
  same_text "$(committed engine/lacking.cpp)" $'int lacking() {\n        return 1;\n}'

# A docker that never answers counts as no Docker after the timeout, sg included.
printf '#!/bin/sh\nexec sleep 30\n' >"$stubs/docker"
cp "$stubs/docker" "$stubs/sg"
printf 'int hanging() {\n        return 1;\n}\n' >"$repo/engine/hanging.cpp"
git_in add engine/hanging.cpp
started=$SECONDS
check "with a docker that hangs the commit passes" \
  commit_with DWE_HOOK_DOCKER_TIMEOUT=1 PATH="$stubs:$PATH" DWE_HOOK_RUNNER=""
check "  with a warning" log_says "Docker is not reachable"
check "  within a few seconds" test $((SECONDS - started)) -lt 10

((failures == 0)) || {
  echo "$failures failed"
  exit 1
}
echo "all passed"
