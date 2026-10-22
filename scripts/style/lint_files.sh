#!/usr/bin/env bash
# Lints the given files: luacheck for Lua (.luacheckrc); shellcheck (.shellcheckrc), the column
# limit (no formatter wraps shell) and quoted array keys for shell; and for C++, only where the
# NOLINT comments go (clang-tidy checks the rest). Runs inside the development image, from the
# repository's root.
# Usage: lint_files.sh [--] FILE...
set -euo pipefail

max_columns=100
# A NOLINT after code: when clang-format splits the statement, the comment lands on a line the
# diagnostic is not on.
# NOLINTNEXTLINE on the line above stays right.
trailing_nolint='\S.*//\s*NOLINT([^A-Z]|$)'
# An array key with hyphens and no quotes, such as route-save-load in a subscript: shfmt reads it as
# arithmetic and spaces it out, which changes the key. Such keys go in quotes.
unquoted_key='\[[A-Za-z_][A-Za-z0-9_]+( ?- ?[A-Za-z_][A-Za-z0-9_]+)+\](=|})'
[[ "${1:-}" == "--" ]] && shift

cpp_files=()
lua_files=()
shell_files=()
for file in "$@"; do
  case "$file" in
    *.cpp | *.hpp) cpp_files+=("$file") ;;
    *.lua) lua_files+=("$file") ;;
    *.sh | scripts/hooks/*) shell_files+=("$file") ;;
    *) ;;
  esac
done

for tool in luacheck shellcheck; do
  if ! command -v "$tool" >/dev/null; then
    echo "lint_files.sh: the image has no $tool; set DWE_IMAGE to an image that has it," \
      "or build one with scripts/dev.sh image" >&2
    exit 1
  fi
done

status=0
if ((${#cpp_files[@]} > 0)) && grep -nHE "$trailing_nolint" -- "${cpp_files[@]}"; then
  echo "lint: put the NOLINT comments above on a NOLINTNEXTLINE line before the code"
  status=1
fi
if ((${#lua_files[@]} > 0)); then
  luacheck --no-color --codes --formatter plain -- "${lua_files[@]}" || status=1
fi
if ((${#shell_files[@]} > 0)); then
  shellcheck --format gcc -- "${shell_files[@]}" || status=1
  if grep -nHE "$unquoted_key" -- "${shell_files[@]}"; then
    echo "lint: quote the array keys above (shfmt reads an unquoted key with hyphens as arithmetic)"
    status=1
  fi
  if LC_ALL=C.UTF-8 grep -nHE "^.{$((max_columns + 1)),}" -- "${shell_files[@]}"; then
    echo "lint: the lines above pass $max_columns columns"
    status=1
  fi
fi
exit "$status"
