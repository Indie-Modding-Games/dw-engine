#!/usr/bin/env bash
# Builds the Windows package from Linux: the same layout as scripts/package_linux.sh (bin/ with
# dwe-game.exe and dwx.exe, packs/, licenses/, VERSION, EXTRACTOR_VERSION), cross-compiled with the
# windows-cross-package preset in the development container. The programs link the C and C++
# runtime statically, so the only library next to them is the Vulkan loader (vulkan-1.dll); every
# other DLL they import is part of Windows, which the script checks. Never anything read from a
# disc.
#
# Usage: scripts/package_windows.sh [--no-build] [--out <folder>]
#   --no-build   package the build as it is (build/windows-cross-package), without building it first
#   --out        where the tarball goes (default build/package)
# Needs the MSVC libraries and the Windows SDK once (scripts/dev.sh windows-sdk). The tarball is a
# .tar.xz like the Linux one, since the launcher installs both the same way.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="windows-cross-package"
build_dir="$root/build/$preset"
stage_root="$root/build/package-stage-windows"
out_dir="$root/build/package"
build=1
triplet="x64-windows-static-cross"
# The DLLs of Windows itself that the programs may import (lower case); anything else must ship in
# bin/. The api-ms-win-* names are Windows' API sets.
system_dlls="kernel32 user32 gdi32 shell32 ole32 oleaut32 advapi32 imm32 version winmm setupapi \
cfgmgr32 hid ws2_32 shlwapi dwmapi uxtheme comdlg32 crypt32 bcrypt bcryptprimitives ntdll dbghelp \
powrprof dxgi d3d11 d3d12 dinput8 xinput1_4 winhttp userenv secur32 mfplat mfreadwrite mf \
avrt synchronization"

while (($#)); do
  case "$1" in
    --no-build) build=0 ;;
    --out)
      [[ $# -ge 2 ]] || {
        echo "package_windows: --out needs a folder" >&2
        exit 2
      }
      out_dir="$2"
      shift
      ;;
    -h | --help)
      sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "package_windows: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done

commit="$(git -C "$root" describe --always --dirty)"
version="$(sed -n 's/^project(dwe VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
name="dwe-beta-${version:-0}-${commit}-windows-x86_64"
stage="$stage_root/$name"

if ((build)); then
  PRESET="$preset" "$root/scripts/dev.sh" configure -DDWE_BUILD_COMMIT="$commit"
  PRESET="$preset" "$root/scripts/dev.sh" build --target game dwx_cli
fi
for program in apps/game/dwe-game.exe apps/dwx/dwx.exe; do
  [[ -f "$build_dir/$program" ]] || {
    echo "package_windows: $build_dir/$program is missing; run without --no-build" >&2
    exit 1
  }
done

rm -rf "$stage_root"
mkdir -p "$stage" "$out_dir/$name-debug"
"$root/scripts/dev.sh" shell -c "cmake --install '$build_dir' --prefix '$stage' >/dev/null"
# The debug information stays beside the tarball, for crash reports.
for program in dwe-game dwx; do
  pdb="$(find "$build_dir/apps" -name "$program.pdb" -print -quit)"
  [[ -z "$pdb" ]] || cp "$pdb" "$out_dir/$name-debug/"
  rm -f "$stage/bin/$program.pdb"
done
cp "$build_dir/vcpkg_installed/$triplet/bin/vulkan-1.dll" "$stage/bin/"

# Every DLL a program or a shipped DLL imports is either shipped in bin/ or part of Windows.
# The image has llvm-readobj under its LLVM version's folder only.
# shellcheck disable=SC2016 # it expands in the container
readobj='$(ls /usr/lib/llvm-*/bin/llvm-readobj | sort -V | tail -1)'
programs="'$stage'/bin/*.exe '$stage'/bin/*.dll"
imports="$("$root/scripts/dev.sh" shell -c "$readobj --coff-imports $programs" \
  | sed -n 's/^ *Name: \(.*\.dll\)$/\1/Ip' | tr '[:upper:]' '[:lower:]' | sort -u)"
missing=0
for dll in $imports; do
  base="${dll%.dll}"
  [[ "$base" == api-ms-win-* || " $system_dlls " == *" $base "* ]] && continue
  if ! find "$stage/bin" -maxdepth 1 -iname "$dll" | grep -q .; then
    echo "package_windows: $dll is imported but neither shipped nor part of Windows" >&2
    missing=1
  fi
done
((missing == 0)) || exit 1

# No folder of the build machine may ship.
for program in "$stage"/bin/*.exe; do
  if strings -n 6 "$program" | grep -F -q -e "$root" -e "/home/"; then
    echo "package_windows: $(basename "$program") names a folder of the build machine:" >&2
    strings -n 6 "$program" | grep -F -e "$root" -e "/home/" | head -5 >&2
    exit 1
  fi
done

# Nothing read from a disc may ship: the team's packs only, no base pack and no file of the
# original.
source "$root/scripts/package/no_disc_data.sh"
package_no_disc_data "$stage" package_windows || exit 1

# The player's way in without the launcher.
printf '@echo off\r\nstart "" "%%~dp0bin\\dwe-game.exe" %%*\r\n' >"$stage/dwe.cmd"
cp "$root/scripts/package/README-windows.md" "$stage/README.md"
printf '%s\n' "$commit" >"$stage/VERSION"
sed -n 's/^inline constexpr int ExtractorVersion = \([0-9]*\);.*/\1/p' \
  "$root/engine/assets/include/dwe/assets/extractor_version.hpp" >"$stage/EXTRACTOR_VERSION"
[[ -s "$stage/EXTRACTOR_VERSION" ]] || {
  echo "package_windows: cannot read ExtractorVersion" >&2
  exit 1
}

licences="$stage/licenses"
mkdir -p "$licences"
for copyright in "$build_dir"/vcpkg_installed/"$triplet"/share/*/copyright; do
  port="$(basename "$(dirname "$copyright")")"
  [[ "$port" == vcpkg-* ]] || cp "$copyright" "$licences/$port.txt"
done
cp "$root/content/packs/game/ui/fonts/noto_sans_license.txt" "$licences/noto-sans.txt"

mkdir -p "$out_dir"
tarball="$out_dir/$name.tar.xz"
tar -C "$stage_root" -cJf "$tarball" "$name"
rm -rf "$stage_root"
echo "package_windows: $tarball ($(du -h "$tarball" | cut -f1))"
