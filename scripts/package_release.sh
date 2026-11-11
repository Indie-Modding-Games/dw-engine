#!/usr/bin/env bash
# Builds the game's release files for the launcher: a computer's package (no game data), renamed
# for the release, its minisign signature and game-manifest.json (docs/modulos/apps/launcher.md).
# It publishes nothing: uploading the files to a release is a separate, manual step (or
# .github/workflows/release.yml, started by hand).
#
# Usage: scripts/package_release.sh [--platform <key>] [--tarball <dwe-beta-...tar.xz>]
#                                   [--version <semver>] [--commit <sha>] [--extractor-version <n>]
#                                   [--out <folder>]
#   --platform           linux-x86_64 (default, scripts/package_linux.sh), windows-x86_64
#                        (scripts/package_windows.sh), darwin-aarch64 or darwin-x86_64 (built on a
#                        Mac by scripts/package_macos.sh, so --tarball is required)
#   --tarball            package this tarball instead of building one
#   --version            the release's version (default <CMake version>-beta.<commits on HEAD>)
#   --commit             the commit game-manifest.json names (default the package's VERSION): CI
#                        builds from a tree and names main's commit only when it publishes
#   --extractor-version  for a tarball without an EXTRACTOR_VERSION file (older packages)
#   --out                where the files go, inside the repository (default build/release); a
#                        game-manifest.json of the same version already there gets this computer's
#                        package added, so one manifest names every computer's
# Signing needs TAURI_SIGNING_PRIVATE_KEY (the key itself or its path) and
# TAURI_SIGNING_PRIVATE_KEY_PASSWORD, the same key the launcher's updater uses; it runs
# `tauri signer sign` in the launcher image (scripts/launcher.sh sign).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build/release"
tarball=""
version=""
commit=""
extractor=""
platform="linux-x86_64"

while (($#)); do
  case "$1" in
    --platform)
      platform="$2"
      shift
      ;;
    --tarball)
      tarball="$2"
      shift
      ;;
    --version)
      version="$2"
      shift
      ;;
    --commit)
      commit="$2"
      shift
      ;;
    --extractor-version)
      extractor="$2"
      shift
      ;;
    --out)
      out="$2"
      shift
      ;;
    -h | --help)
      sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "package_release: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done

# Its length, not its text: set -x would print the text.
if [[ ! -v TAURI_SIGNING_PRIVATE_KEY ]] || ((${#TAURI_SIGNING_PRIVATE_KEY} == 0)); then
  echo "package_release: TAURI_SIGNING_PRIVATE_KEY is not set (docs/modulos/apps/launcher.md)" >&2
  exit 1
fi

mkdir -p "$out"
out="$(cd "$out" && pwd)"
[[ "$out/" == "$root/"* ]] || {
  echo "package_release: --out must be inside the repository (the signer sees only it)" >&2
  exit 1
}
if [[ -z "$tarball" ]]; then
  case "$platform" in
    linux-x86_64) packager="package_linux.sh" ;;
    windows-x86_64) packager="package_windows.sh" ;;
    *)
      echo "package_release: $platform is built on its own computer; pass --tarball" >&2
      exit 2
      ;;
  esac
  "$root/scripts/$packager" --out "$out/$platform"
  pattern="dwe-beta-*-$platform.tar.xz"
  tarball="$(find "$out/$platform" -maxdepth 1 -name "$pattern" -printf '%T@ %p\n' \
    | sort -rn | sed -n '1s/^[^ ]* //p')"
fi
tarball="$(cd "$(dirname "$tarball")" && pwd)/$(basename "$tarball")"

# The package's own record of what it is.
top="$(tar -tJf "$tarball" | sed -n '1s|/.*||p')"
member() { tar -xJOf "$tarball" "$top/$1" 2>/dev/null | tr -d '[:space:]' || true; }
[[ -n "$commit" ]] || commit="$(member VERSION)"
[[ -n "$extractor" ]] || extractor="$(member EXTRACTOR_VERSION)"
[[ "$extractor" =~ ^[0-9]+$ ]] || {
  echo "package_release: the package has no EXTRACTOR_VERSION; pass --extractor-version" >&2
  exit 1
}
# The project's licence and the third-party notices travel with every package.
members="$(tar -tJf "$tarball")"
for file in LICENSE THIRD_PARTY_NOTICES.txt; do
  grep -qxF "$top/$file" <<<"$members" || {
    echo "package_release: the package has no $file (scripts/package/third_party_notices.py)" >&2
    exit 1
  }
done
if [[ -z "$version" ]]; then
  pattern='s/^project(dwe VERSION \([0-9.]*\).*/\1/p'
  cmake_version="$(sed -n "$pattern" "$root/CMakeLists.txt" | sed -n 1p)"
  version="${cmake_version:-0.0.0}-beta.$(git -C "$root" rev-list --count HEAD)"
fi

package="$out/dwe-game-$version-$platform.tar.xz"
cp "$tarball" "$package"
rm -f "$package.sig"
# The key goes to the container through the environment only, never on its command line.
"$root/scripts/launcher.sh" sign "$package"
[[ -s "$package.sig" ]] || {
  echo "package_release: signing failed" >&2
  exit 1
}
manifest="$out/game-manifest.json"
merge=()
same_version='import json, sys; sys.exit(json.load(open(sys.argv[1]))["version"] != sys.argv[2])'
if [[ -f "$manifest" ]] && python3 -c "$same_version" \
  "$manifest" "$version"; then
  merge=(--merge "$manifest")
fi
python3 "$root/scripts/package/release_manifest.py" game --package "$package" \
  --signature "$package.sig" --version "$version" --commit "$commit" \
  --extractor-version "$extractor" --platform "$platform" "${merge[@]}" >"$manifest.new"
mv "$manifest.new" "$manifest"
echo "package_release: $package"
echo "package_release: $manifest (version $version, extractor $extractor, $platform)"
