#!/usr/bin/env bash
# The launcher's version against the newest published release, of either channel (CI's
# launcher-version check and release.yml). Installed launchers update themselves only to a higher
# version (latest.json carries tauri.conf.json's), so:
#   - tauri.conf.json, Cargo.toml and Cargo.lock say the same version;
#   - it is never lower than the newest release's;
#   - it is higher when the launcher's sources changed since that release.
# Needs gh (GH_TOKEN) and a checkout with the history and the tags.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
repo="${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY to owner/name}"
tauri="$root/apps/launcher/src-tauri"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$tauri/tauri.conf.json")"
cargo="$(sed -n 's/^version = "\(.*\)"$/\1/p' "$tauri/Cargo.toml" | head -1)"
lock="$(awk '/^name = "dwe-launcher"$/ { getline; print; exit }' "$tauri/Cargo.lock" \
  | sed -n 's/^version = "\(.*\)"$/\1/p')"
if [[ "$version:$version" != "$cargo:$lock" ]]; then
  echo "tauri.conf.json says $version, Cargo.toml $cargo and Cargo.lock $lock;" \
    "raise them together" >&2
  exit 1
fi

previous="$(gh release list -R "$repo" --exclude-drafts --limit 1 --json tagName \
  --jq '.[0].tagName // ""')"
if [[ -z "$previous" ]]; then
  echo "No earlier release; the launcher is $version"
  exit 0
fi
gh release download "$previous" -R "$repo" -p latest.json -O "$work/latest.json" --clobber
previous_version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$work/latest.json")"
git -C "$root" rev-parse -q --verify "refs/tags/$previous" >/dev/null \
  || git -C "$root" fetch -q --no-tags origin "refs/tags/$previous:refs/tags/$previous"
changed=0
if [[ -n "$(git -C "$root" diff-tree -r --name-only "$previous" HEAD -- apps/launcher/ui \
  apps/launcher/src-tauri ':(exclude)apps/launcher/src-tauri/tests')" ]]; then
  changed=1
fi
python3 "$root/scripts/ci/release.py" launcher-check --version "$version" \
  --previous "$previous_version" --tag "$previous" --changed "$changed"
