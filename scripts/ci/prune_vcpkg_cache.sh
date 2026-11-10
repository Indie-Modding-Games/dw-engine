#!/usr/bin/env bash
# Deletes from a vcpkg binary cache the packages that no build of this run installed, before CI
# saves it. A cache restored from an older key keeps every package it ever held and the new key
# saves them all again, so without this each change of the dependencies grows it, and the
# repository's cache space then evicts whole caches. The sources' downloads stay.
# Usage: scripts/ci/prune_vcpkg_cache.sh <cache folder> <build folder>...
#   every vcpkg_installed/vcpkg/status under the build folders names the packages to keep (Abi:)
set -euo pipefail
cache="${1:?usage: prune_vcpkg_cache.sh <cache folder> <build folder>...}"
shift
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

find "$@" -path '*/vcpkg_installed/vcpkg/status' -type f -exec grep -h '^Abi: ' {} + 2>/dev/null \
  | cut -d' ' -f2 | sort -u >"$work/keep" || true
if [[ ! -s "$work/keep" ]]; then
  echo "prune_vcpkg_cache: no installed packages found: nothing deleted"
  exit 0
fi

kept=0
deleted=0
while IFS= read -r -d '' zip; do
  if grep -qxF "$(basename "$zip" .zip)" "$work/keep"; then
    kept=$((kept + 1))
  else
    rm -f "$zip"
    deleted=$((deleted + 1))
  fi
done < <(find "$cache" -path "$cache/downloads" -prune -o -name '*.zip' -type f -print0)
find "$cache" -mindepth 1 -type d -empty -delete
echo "prune_vcpkg_cache: kept $kept packages, deleted $deleted"
