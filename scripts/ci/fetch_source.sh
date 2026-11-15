#!/usr/bin/env bash
# vcpkg's asset source for the dependencies' downloads (X_VCPKG_ASSET_SOURCES, x-script): fetches
# one with retries on any error, and for kernel.org from its other hosts too, so a slow or refused
# mirror (mirrors.edge.kernel.org timed out and refused TLS in 2026-11) no longer fails a build
# whose binary cache was evicted. When every try fails, vcpkg still tries the port's own addresses.
# Usage: scripts/ci/fetch_source.sh <url> <sha512> <destination>
#   sha512 may be empty (a download vcpkg does not check); vcpkg checks the file again after.
set -euo pipefail
url="${1:?usage: fetch_source.sh <url> <sha512> <destination>}"
sha512="${2-}"
destination="${3:?usage: fetch_source.sh <url> <sha512> <destination>}"
partial="$destination.part"
trap 'rm -f "$partial"' EXIT

candidates=("$url")
case "$url" in
  https://mirrors.edge.kernel.org/pub/* | https://cdn.kernel.org/pub/* | \
    https://www.kernel.org/pub/*)
    path="/pub/${url#*/pub/}"
    for host in cdn.kernel.org mirrors.edge.kernel.org www.kernel.org; do
      if [[ "https://$host$path" != "$url" ]]; then
        candidates+=("https://$host$path")
      fi
    done
    ;;
  *) ;;
esac

sha512_of() {
  if command -v sha512sum >/dev/null; then
    sha512sum "$1"
  else
    shasum -a 512 "$1"
  fi | cut -d' ' -f1
}

for candidate in "${candidates[@]}"; do
  if curl -fsSL --retry 3 --retry-all-errors --retry-delay 5 --connect-timeout 30 --max-time 900 \
    -o "$partial" "$candidate"; then
    if [[ -z "$sha512" || "$(sha512_of "$partial")" == "$(echo "$sha512" | tr 'A-F' 'a-f')" ]]; then
      mv "$partial" "$destination"
      exit 0
    fi
    echo "fetch_source: $candidate does not match its SHA-512" >&2
  else
    echo "fetch_source: $candidate failed" >&2
  fi
done
exit 1
