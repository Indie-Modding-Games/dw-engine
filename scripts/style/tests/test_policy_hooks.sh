#!/usr/bin/env bash
# Tests of the publication policy that the pre-commit and commit-msg hooks check
# (scripts/hooks/policy.sh), in a scratch repository: each rule lets a good commit through and stops
# a bad one, but the banned words in the files, which only warn. A stub docker stands for a host
# without Docker, so the formatting is skipped.
set -uo pipefail

# The user's and the system's git configuration stay out of the scratch repository.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
hook_files=(scripts/dev.sh scripts/hooks/pre-commit scripts/hooks/commit-msg
  scripts/hooks/policy.sh)
identity_name="Rivarux"
identity_email="2211799+Rivarux@users.noreply.github.com"

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
for file in "${hook_files[@]}"; do
  mkdir -p "$repo/$(dirname "$file")"
  cp "$source_root/$file" "$repo/$file"
done
git_in() { git -C "$repo" -c user.name="$identity_name" -c user.email="$identity_email" "$@"; }
git_in init -q
git_in add -A
git_in commit -q --no-verify -m "The hooks"
"$repo/scripts/dev.sh" setup-hooks >/dev/null

stubs="$scratch/stubs"
mkdir -p "$stubs"
printf '#!/bin/sh\nexit 1\n' >"$stubs/docker"
cp "$stubs/docker" "$stubs/sg"
chmod +x "$stubs/docker" "$stubs/sg"
export PATH="$stubs:$PATH" DWE_HOOK_RUNNER=""

# commit <message> [git commit options...]: a signed-off commit of what is staged.
commit() {
  local message="$1"
  shift
  git_in commit -q -s -m "$message" "$@" >"$scratch/commit.log" 2>&1
}
log_says() { grep -q -- "$1" "$scratch/commit.log"; }
fails() { ! "$@"; }
# stage <path> <text>: writes a file and stages it.
stage() {
  mkdir -p "$(dirname "$repo/$1")"
  printf '%s\n' "$2" >"$repo/$1"
  git_in add -- "$1"
}
unstage_all() {
  git_in reset -q --hard
  git_in clean -q -fdx
}

# Banned words in the files warn without stopping the commit: only in added lines, only on their
# own, and not where the allowlist lets them.
cited=$'\n// tamer.c: tamerTickWalkingState, kept by name.'
stage engine/walk.cpp "// Walks the partner.$cited"
check "added lines without banned words pass" commit "Walk the partner"
check "  without a warning" fails log_says "WARNING"
stage engine/walk.cpp "// Walks the Digimon partner.$cited"
check "an added line with a banned word passes" commit "Name the creature"
check "  with a warning" log_says "pre-commit: WARNING: added lines name the franchise"
check "  that names the file, the line and the word" log_says "engine/walk.cpp:1: digimon"
printf '// The player.\n' >"$repo/engine/old.cpp"
printf '// The PS1 frame.\n' >>"$repo/engine/old.cpp"
git_in add engine/old.cpp
git_in commit -q --no-verify -s -m "An old file"
printf '// The player, walking.\n// The PS1 frame.\n' >"$repo/engine/old.cpp"
git_in add engine/old.cpp
check "a banned word in a line the commit leaves as is does not warn" commit "Change a line"
check "  at all" fails log_says "WARNING"
stage engine/platform/pad.cpp 'case GamepadFamily::PlayStation:'
check "an allowlisted word in its files does not warn" commit "Name a pad family"
check "  at all" fails log_says "WARNING"
stage apps/game/console.cpp '// Runs like a PlayStation.'
check "the same word outside the allowlist's files warns" commit "Name a console"
check "  and names it" log_says "apps/game/console.cpp:1: playstation"

# Forbidden paths: disc images and data, savestates, traces, run folders and .claude.
stage docs/notes.txt 'Notes'
check "an ordinary path passes" commit "Add notes"
for path in disc/GAME.BIN disc/game.cue saves/field.sav _work/run/trace.jsonl \
  content/base/maps/map.yaml .claude/settings.json; do
  stage "$path" 'data'
  check "staging $path stops the commit" fails commit "Add data"
  check "  and names it" log_says "$path"
  unstage_all
done

# The author's identity.
stage docs/more.txt 'More'
check "the project's identity passes" commit "Add more notes"
stage docs/other.txt 'Other'
git -C "$repo" -c user.name=test -c user.email=test@example.invalid commit -q -s -m "Add other" \
  >"$scratch/commit.log" 2>&1
check "another identity stops the commit" test "$?" -ne 0
check "  and names both" log_says "the author is test <test@example.invalid>; commits are authored"
unstage_all

# The commit message.
stage docs/a.txt 'A'
check "a signed-off message passes" commit "Add a"
stage docs/b.txt 'B'
git_in commit -q -m "Add b" >"$scratch/commit.log" 2>&1
check "a message without Signed-off-by stops the commit" test "$?" -ne 0
check "  and says so" log_says "there is no Signed-off-by line"
for message in $'Add b\n\nCo-Authored-By: Someone <someone@example.invalid>' \
  $'Add b\n\nGenerated with a tool' "Add b with Claude" "Add b, AI-generated"; do
  check "the message \"${message//$'\n'/ }\" stops the commit" fails commit "$message"
  check "  as naming an assistant" log_says "names a co-author, a generator or an AI assistant"
done
check "a message with a banned word stops the commit" fails commit "Add the Bandai notes"
check "  and names it" log_says "message:1: bandai"
check "a word that only contains a name passes" commit "Add b from the playstations table"
git_in checkout -q -b psx-notes
stage docs/c.txt 'C'
check "comment lines of the message, here the branch name, are not checked" \
  env GIT_EDITOR=true git -C "$repo" -c user.name="$identity_name" \
  -c user.email="$identity_email" commit -q -s -e -m "Add c" 2>"$scratch/commit.log"
git_in checkout -q -
git_in merge -q --no-ff -m "Merge the notes" psx-notes >"$scratch/commit.log" 2>&1
check "a merge commit needs no Signed-off-by" test "$?" -eq 0

# DWE_SKIP_HOOK=1 skips every rule.
stage docs/skipped.txt 'The Sony notes'
DWE_SKIP_HOOK=1 git -C "$repo" -c user.name=test -c user.email=test@example.invalid \
  commit -q -m "Skip the PSX checks" >"$scratch/commit.log" 2>&1
check "with DWE_SKIP_HOOK=1 the commit passes" test "$?" -eq 0
check "  with one line" test "$(wc -l <"$scratch/commit.log")" -eq 1

((failures == 0)) || {
  echo "$failures failed"
  exit 1
}
echo "all passed"
