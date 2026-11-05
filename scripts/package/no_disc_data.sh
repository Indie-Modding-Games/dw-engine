# shellcheck shell=bash
# The check the Linux-built packages pass before they ship (scripts/package_linux.sh,
# package_windows.sh and, on the finished tarballs, scripts/release_local.sh): nothing read from a
# disc. No base pack (the one dwx extracts from the player's disc) and no file the publication
# policy forbids in a commit (disc images, files of the original, savestates, traces;
# scripts/hooks/policy.sh). GNU find and bash 4: scripts/package_macos.sh keeps its own check.
#
# Usage: source it, then `package_no_disc_data <folder> <who>`: it says why on the standard error
# and fails when the folder holds any.

# shellcheck source=scripts/hooks/policy.sh
source "$(dirname "${BASH_SOURCE[0]}")/../hooks/policy.sh"

package_no_disc_data() {
  local folder="$1" who="$2" bases hits failed=0
  bases="$(find "$folder" -name pack.yaml -exec grep -l '^id: base$' {} + || true)"
  if [[ -n "$bases" ]]; then
    echo "$who: a base pack is in the package; it must never ship:" >&2
    head -5 <<<"$bases" >&2
    failed=1
  fi
  hits="$(cd "$folder" && find . -mindepth 1 -printf '%P\0' | policy_forbidden_paths_in || true)"
  if [[ -n "$hits" ]]; then
    echo "$who: the package holds files read from a disc (or other forbidden files):" >&2
    head -20 <<<"$hits" >&2
    failed=1
  fi
  return "$failed"
}
