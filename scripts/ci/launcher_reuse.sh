#!/usr/bin/env bash
# Whether release.yml builds the launcher again or publishes the previous release's launcher files
# as they are (the same version, so installed launchers behave the same): prints "reuse" or
# "build", and why on the standard error. The files are reused only when
#   - nothing the launcher is built from changed since that release (its sources, its image, the
#     fonts and notices it carries and release.yml, where its build steps are);
#   - that release has every launcher file with its signature, and its latest.json has this
#     tauri.conf.json's version;
#   - its programs (the .deb's and the macOS bundle's) carry this run's gateway, public key, beta
#     key and links: they are repository variables and a secret, which git does not see. The
#     Windows program was built in the same run from the same values.
#
# Usage: scripts/ci/launcher_reuse.sh <previous release's tag>
# Needs gh (GH_TOKEN), GITHUB_REPOSITORY, a checkout with the history, DWE_LAUNCHER_GATEWAY,
# DWE_LAUNCHER_PUBKEY, DWE_LAUNCHER_BETA_KEY and DWE_LAUNCHER_LINKS (empty when there are none).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
repo="${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY to owner/name}"
previous="${1:-}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

build() {
  echo "launcher_reuse: building the launcher: $*" >&2
  echo build
  exit 0
}

[[ -n "$previous" ]] || build "there is no earlier release"
git -C "$root" rev-parse -q --verify "refs/tags/$previous" >/dev/null \
  || git -C "$root" fetch -q --no-tags origin "refs/tags/$previous:refs/tags/$previous"
inputs=(apps/launcher ':(exclude)apps/launcher/src-tauri/tests' scripts/launcher.sh
  .devcontainer/launcher.Dockerfile content/packs/game/ui/fonts LICENSE
  scripts/package/third_party_notices.py .github/workflows/release.yml)
changed="$(git -C "$root" diff-tree -r --name-only "$previous" HEAD -- "${inputs[@]}")"
[[ -z "$changed" ]] || build "changed since $previous: $(head -5 <<<"$changed" | tr '\n' ' ')"

assets="$(gh release view "$previous" -R "$repo" --json assets --jq '.assets[].name')" \
  || build "cannot list $previous's files"
files=('_amd64\.AppImage' '_amd64\.deb' '_x64-setup\.exe' '_universal\.dmg' '\.app\.tar\.gz')
for pattern in "${files[@]}"; do
  if [[ "$(grep -cE "^dwe-launcher.*$pattern\$" <<<"$assets")" != 1 ]] \
    || ! grep -qE "^dwe-launcher.*$pattern\.sig\$" <<<"$assets"; then
    build "$previous has no single signed launcher file like $pattern"
  fi
done

gh release download "$previous" -R "$repo" -D "$work" -p latest.json -p 'dwe-launcher*.deb' \
  -p 'dwe-launcher*.app.tar.gz' || build "cannot download $previous's launcher files"
version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$root/apps/launcher/src-tauri/tauri.conf.json")"
previous_version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$work/latest.json")"
[[ "$version" == "$previous_version" ]] \
  || build "the launcher is $version, $previous has $previous_version"

mkdir -p "$work/deb" "$work/mac"
if ! { dpkg-deb --fsys-tarfile "$work"/*.deb | tar -x -C "$work/deb" --wildcards '*/bin/*'; } \
  || ! tar -xzf "$work"/*.app.tar.gz -C "$work/mac" --wildcards '*/Contents/MacOS/*'; then
  build "cannot unpack $previous's .deb or macOS bundle"
fi
public_key="$(printf '%s' "${DWE_LAUNCHER_PUBKEY:-}" | tr -d '[:space:]')"
values=(gateway "${DWE_LAUNCHER_GATEWAY:-}" "public key" "$public_key"
  "beta key" "${DWE_LAUNCHER_BETA_KEY:-}" links "${DWE_LAUNCHER_LINKS:-}")
shopt -s nullglob
programs=("$work"/deb/usr/bin/* "$work"/mac/*/Contents/MacOS/*)
((${#programs[@]} >= 2)) || build "no launcher program found in $previous's .deb and macOS bundle"
for program in "${programs[@]}"; do
  for ((i = 0; i < ${#values[@]}; i += 2)); do
    what="${values[i]}"
    value="${values[i + 1]}"
    # The links are optional; the others are checked before this runs.
    [[ -n "$value" || "$what" == links ]] || build "no $what to compare with"
    [[ -z "$value" ]] || grep -q -a -F -e "$value" "$program" \
      || build "$previous's $(basename "$program") does not have this run's $what"
  done
done
echo "launcher_reuse: $previous's launcher $version is reused: unchanged, same values built in" >&2
echo reuse
