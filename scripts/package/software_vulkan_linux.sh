#!/usr/bin/env bash
# Copies the development image's software Vulkan driver (Mesa's lavapipe) into a Linux package's
# lib/lavapipe/: the driver, the libraries it needs that a desktop may lack, its manifest and their
# licences (licenses/lavapipe/). It draws on the processor, so it is slow, but the game starts on a
# computer whose GPU has no Vulkan 1.3 driver (a virtual machine, an old GPU, a missing driver). The
# launcher and the package's dwe script turn it on only when `dwe-game --check-system` finds no GPU
# that can run the game (docs/modulos/apps/launcher.md).
#
# Usage, in the development image (scripts/package_linux.sh runs it there):
#   scripts/package/software_vulkan_linux.sh <package folder>
set -euo pipefail

stage="${1:?usage: software_vulkan_linux.sh <package folder>}"
target="$stage/lib/lavapipe"
licences="$stage/licenses/lavapipe"
manifest=/usr/share/vulkan/icd.d/lvp_icd.json
driver="$(dpkg -L mesa-vulkan-drivers | grep '/libvulkan_lvp\.so$' | head -1)"
[[ -f "$driver" && -f "$manifest" ]] || {
  echo "software_vulkan_linux: lavapipe is not installed in this image (mesa-vulkan-drivers)" >&2
  exit 1
}

# Libraries every Linux desktop has, with a stable interface, and that the game's own process loads
# anyway: they come from the system. Everything else the driver needs ships (LLVM, ICU and libxml2
# have version-named files that differ between distributions).
system_libraries=(
  linux-vdso ld-linux-x86-64 libc libm libdl libpthread librt libgcc_s 'libstdc\+\+'
  libX11 libxcb libXau libXdmcp libwayland-client libz libzstd liblzma libexpat libffi
)
system="^($(
  IFS='|'
  echo "${system_libraries[*]}"
))\.so"

rm -rf "$target" "$licences"
mkdir -p "$target" "$licences"
cp -L "$driver" "$target/libvulkan_lvp.so"
shipped=("$driver")
while read -r name path; do
  [[ "$name" =~ $system ]] && continue
  cp -L "$path" "$target/$name"
  shipped+=("$path")
done < <(ldd "$driver" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }')

# The manifest names the driver next to it (a path with a folder is relative to the manifest).
sed 's|"library_path": *"[^"]*"|"library_path": "./libvulkan_lvp.so"|' "$manifest" \
  >"$target/lvp_icd.json"
grep -q '"./libvulkan_lvp.so"' "$target/lvp_icd.json" || {
  echo "software_vulkan_linux: cannot rewrite $manifest" >&2
  exit 1
}

# Each shipped file's package licence (Mesa, LLVM, ICU, libxml2...).
for path in "${shipped[@]}"; do
  real="$(readlink -f "$path")"
  owner="$(dpkg -S "$real" 2>/dev/null || dpkg -S "${real#/usr}" 2>/dev/null || true)"
  package="${owner%%:*}"
  [[ -n "$package" && -f "/usr/share/doc/$package/copyright" ]] || {
    echo "software_vulkan_linux: no licence found for $path" >&2
    exit 1
  }
  cp "/usr/share/doc/$package/copyright" "$licences/$package.txt"
done

# Every library the driver needs is shipped or one of the system's.
missing="$(
  LD_LIBRARY_PATH="$target" ldd "$target/libvulkan_lvp.so" | awk '/not found/ { print $1 }'
)"
[[ -z "$missing" ]] || {
  echo "software_vulkan_linux: missing $missing" >&2
  exit 1
}
echo "software_vulkan_linux: $(find "$target" -type f | wc -l) files, $(du -sh "$target" | cut -f1)"
