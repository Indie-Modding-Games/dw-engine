#!/usr/bin/env bash
# Formats the given files in place, or with --check only reports the ones that are not formatted,
# each with its formatter: clang-format for C++, StyLua for Lua and shfmt for shell, then
# wrap_comments.py for the comments of Lua and shell, which the other two leave as they are. Runs
# inside the development image, from the repository's root.
#
# Usage: format_files.sh [--check] [--jobs N] [--] FILE...
#
# The last lines are a block, which bash reads whole before it runs it: this file may be one it
# formats, and bash must not read on from where it was in it.
set -euo pipefail

style_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
required_tools=(clang-format stylua shfmt python3)

check_tools() {
  local tool
  for tool in "${required_tools[@]}"; do
    if ! command -v "$tool" >/dev/null; then
      echo "format_files.sh: the image has no $tool; set DWE_IMAGE to an image that has it," \
        "or build one with scripts/dev.sh image" >&2
      return 1
    fi
  done
}

format_cpp() {
  if $check; then
    printf '%s\0' "$@" | xargs -0 -r -P "$jobs" -n 16 clang-format --dry-run --Werror
  else
    printf '%s\0' "$@" | xargs -0 -r -P "$jobs" -n 16 clang-format -i
  fi
}

format_lua() {
  if $check; then
    stylua --check -- "$@"
  else
    stylua -- "$@"
  fi
}

# shfmt takes its style from .editorconfig, as the editors do.
format_shell() {
  if $check; then
    shfmt -d -- "$@"
  else
    shfmt -w -- "$@"
  fi
}

wrap_comments() {
  if $check; then
    python3 "$style_dir/wrap_comments.py" --check -- "$@"
  else
    python3 "$style_dir/wrap_comments.py" -- "$@"
  fi
}

main() {
  check=false
  jobs="$(nproc)"
  while (($# > 0)); do
    case "$1" in
      --check) check=true ;;
      --jobs)
        jobs="$2"
        shift
        ;;
      --)
        shift
        break
        ;;
      *) break ;;
    esac
    shift
  done

  local cpp_files=() lua_files=() shell_files=() file status=0
  for file in "$@"; do
    case "$file" in
      *.cpp | *.hpp) cpp_files+=("$file") ;;
      *.lua) lua_files+=("$file") ;;
      *.sh | scripts/hooks/*) shell_files+=("$file") ;;
      *) ;;
    esac
  done

  check_tools || return 1
  ((${#cpp_files[@]} == 0)) || format_cpp "${cpp_files[@]}" || status=1
  ((${#lua_files[@]} == 0)) || format_lua "${lua_files[@]}" || status=1
  ((${#shell_files[@]} == 0)) || format_shell "${shell_files[@]}" || status=1
  local comment_files=("${lua_files[@]}" "${shell_files[@]}")
  ((${#comment_files[@]} == 0)) || wrap_comments "${comment_files[@]}" || status=1
  return "$status"
}

{
  main "$@"
  exit "$?"
}
