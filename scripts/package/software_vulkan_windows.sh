#!/usr/bin/env bash
# Puts Mesa's software Vulkan driver (lavapipe) in a Windows package's lib/lavapipe/: the DLL, its
# manifest and its licence. The build is fixed by version and SHA256 so every package carries the
# same bytes; nothing is installed on the player's computer. The launcher turns the driver on only
# when `dwe-game --check-system` finds no GPU that can run the game (docs/modules/apps/launcher.md).
#
# lavapipe draws on the processor, so it is slow, but it starts on an old GPU, a virtual machine or
# a computer with no driver. Unlike Dozen (dzn), it needs no D3D12 and has the bindless features the
# game requires.
#
# Usage, in the development image (scripts/package_windows.sh runs it there):
#   scripts/package/software_vulkan_windows.sh <package folder>
set -euo pipefail

stage="${1:?usage: software_vulkan_windows.sh <package folder>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
version=26.3.1
asset="mesa-lavapipe-x64-$version.7z"
url="https://github.com/mmozeiko/build-mesa/releases/download/$version/$asset"
sha256=edbf9dc491e346f725730a7cc264e16baba1a7e8df9789153bbd38fb26ab9d3d
target="$stage/lib/lavapipe"
licences="$stage/licenses/lavapipe"

command -v 7z >/dev/null || {
  echo "software_vulkan_windows: 7z is needed to unpack $asset" >&2
  exit 1
}

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
curl -fsSL "$url" -o "$tmp/$asset"
echo "$sha256  $tmp/$asset" | sha256sum -c - >/dev/null || {
  echo "software_vulkan_windows: wrong checksum for $asset" >&2
  exit 1
}
7z x -y -o"$tmp/unpack" "$tmp/$asset" >/dev/null
manifest="$tmp/unpack/lvp_icd.x86_64.json"
driver="$tmp/unpack/vulkan_lvp.dll"
[[ -f "$manifest" && -f "$driver" ]] || {
  echo "software_vulkan_windows: $asset has no lavapipe manifest and driver" >&2
  exit 1
}
# The loader resolves the DLL's path relative to the manifest, so both go in the same folder.
grep -q 'vulkan_lvp.dll' "$manifest" || {
  echo "software_vulkan_windows: the manifest does not name vulkan_lvp.dll" >&2
  exit 1
}

rm -rf "$target" "$licences"
mkdir -p "$target" "$licences"
cp "$driver" "$target/vulkan_lvp.dll"
cp "$manifest" "$target/lvp_icd.x86_64.json"
cp "$root/scripts/package/licenses/lavapipe.txt" "$licences/mesa.txt"
echo "software_vulkan_windows: Mesa lavapipe $version, $(du -sh "$target" | cut -f1)"
