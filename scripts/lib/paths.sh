# shellcheck shell=bash
# Paths resolved the same way on Linux and on macOS, whose realpath has no -m or -s: GNU realpath
# where it is, python3 elsewhere. Sourced.

# resolved_path <path>: the absolute path with every symbolic link resolved, as GNU realpath -m
# prints it: the path, or its last parts, need not exist.
resolved_path() {
  if realpath -m / >/dev/null 2>&1; then
    realpath -m "$1"
  else
    python3 -I -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"
  fi
}

# absolute_path <path>: the absolute path without . and .. but with its symbolic links as written,
# as GNU realpath -ms prints it.
absolute_path() {
  if realpath -ms / >/dev/null 2>&1; then
    realpath -ms "$1"
  else
    python3 -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$1"
  fi
}
