#!/usr/bin/env bash
# Builds the Linux beta package: a tarball a player unpacks and runs. It holds the game and dwx
# (release build), the team's packs (content/packs), the Vulkan loader and a software Vulkan driver
# (lib/), the project's LICENSE, the third-party licences (licenses/ and THIRD_PARTY_NOTICES.txt)
# and the package README, and never anything read from a disc: each player reads their own disc on
# the first start (docs/guides/beta-linux.md).
#
# Usage: scripts/package_linux.sh [--no-build] [--out <folder>]
#   --no-build   package the build as it is (build/linux-clang-package), without building it first
#   --out        where the tarball goes (default build/package); a folder outside the repository is
#                fine
# The build runs in the development container (scripts/dev.sh); packaging needs only bash, tar and
# xz on the host.
# DWE_PACKAGE_SOURCE, when set, is what the package names as its source (VERSION, its folder and
# bug reports) instead of git describe: CI passes the tree it builds (scripts/ci/packages.py).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="linux-clang-package"
build_dir="$root/build/$preset"
stage_root="$root/build/package-stage"
out_dir="$root/build/package"
build=1

while (($#)); do
  case "$1" in
    --no-build) build=0 ;;
    --out)
      [[ $# -ge 2 ]] || {
        echo "package_linux: --out needs a folder" >&2
        exit 2
      }
      out_dir="$2"
      shift
      ;;
    -h | --help)
      sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "package_linux: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done

# The source the package names; CI's is the tree, the same in a pull request and in main after the
# squash merge, so a package built in the pull request is right for main as it is.
commit="${DWE_PACKAGE_SOURCE:-$(git -C "$root" describe --always --dirty)}"
[[ "$commit" =~ ^[0-9A-Za-z._-]+$ ]] || {
  echo "package_linux: DWE_PACKAGE_SOURCE may hold only letters, digits, '.', '_' and '-'" >&2
  exit 2
}
version="$(sed -n 's/^project(dwe VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
name="dwe-beta-${version:-0}-${commit}-linux-x86_64"
stage="$stage_root/$name"

if ((build)); then
  # The commit goes into the game, so a player's bug report (F12) names it.
  PRESET="$preset" "$root/scripts/dev.sh" configure -DDWE_BUILD_COMMIT="$commit"
  PRESET="$preset" "$root/scripts/dev.sh" build --target game dwx_cli
fi
for program in apps/game/dwe-game apps/dwx/dwx; do
  [[ -x "$build_dir/$program" ]] || {
    echo "package_linux: $build_dir/$program is missing; run without --no-build" >&2
    exit 1
  }
done

# cmake --install lays the package out (bin/, packs/) and drops the build tree's runtime path; strip
# leaves out the debug information, which is kept beside the tarball for crash reports.
rm -rf "$stage_root"
mkdir -p "$stage"
"$root/scripts/dev.sh" shell -c "cmake --install '$build_dir' --prefix '$stage' >/dev/null"
mkdir -p "$out_dir/$name-debug"
for program in dwe-game dwx; do
  objcopy --only-keep-debug "$stage/bin/$program" "$out_dir/$name-debug/$program.debug"
  strip --strip-debug --strip-unneeded "$stage/bin/$program"
  objcopy --add-gnu-debuglink="$out_dir/$name-debug/$program.debug" "$stage/bin/$program"
done

# The Vulkan loader ships next to the game (bin/dwe-game looks in ../lib first), so the player needs
# none installed; with it, Mesa's software Vulkan driver, for a computer whose GPU cannot run the
# game (the launcher and the dwe script turn it on only then).
mkdir -p "$stage/lib"
loader="$build_dir/vcpkg_installed/x64-linux-libcxx-package/lib/libvulkan.so.1"
cp -L "$loader" "$stage/lib/libvulkan.so.1"
"$root/scripts/dev.sh" shell -c "scripts/package/software_vulkan_linux.sh '$stage'"

# Every library a program needs is shipped in lib/ or part of every Linux (the C library and its
# runtime); the game finds lib/ through its runtime path.
dynamic="$("$root/scripts/dev.sh" shell -c "readelf -d '$stage/bin/dwe-game' '$stage/bin/dwx'")"
while read -r library; do
  case "$library" in
    libc.so.6 | libm.so.6 | libgcc_s.so.1 | ld-linux-x86-64.so.2) ;;
    libdl.so.2 | libpthread.so.0 | librt.so.1) ;;
    *)
      [[ -f "$stage/lib/$library" ]] || {
        echo "package_linux: $library is needed but neither shipped nor part of every Linux" >&2
        exit 1
      }
      ;;
  esac
done < <(sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p' <<<"$dynamic" | sort -u)
# shellcheck disable=SC2016 # $ORIGIN is the loader's, written as is.
grep -q 'RUNPATH.*\[\$ORIGIN/../lib\]' <<<"$dynamic" || {
  echo "package_linux: dwe-game does not look for its libraries in ../lib" >&2
  exit 1
}

# No folder of the build machine may ship: a package build (DWE_PACKAGE) finds its packs next to the
# executable, and the sources' and the libraries' file names are relative.
for program in dwe-game dwx; do
  if strings -n 6 "$stage/bin/$program" | grep -F -q -e "$root" -e "/home/"; then
    echo "package_linux: $program names a folder of the build machine:" >&2
    strings -n 6 "$stage/bin/$program" | grep -F -e "$root" -e "/home/" | head -5 >&2
    exit 1
  fi
done

# The team's packs ship cooked into .pak archives, which a package build loads (and only those).
"$root/scripts/dev.sh" shell -c "scripts/package/cook_team_packs.sh '$stage/packs' '$stage/bin/dwx'"

# Nothing read from a disc may ship: the team's packs only, no base pack and no file of the
# original.
source "$root/scripts/package/no_disc_data.sh"
package_no_disc_data "$stage" package_linux || exit 1

# The launcher a player runs: the game from wherever the package was unpacked.
cat >"$stage/dwe" <<'EOF'
#!/bin/sh
# Starts the game from the folder this script is in; options go to the game (README.md).
# When no GPU of this computer can run the game, it runs on the software Vulkan driver in
# lib/lavapipe (slow, but it starts).
here="$(dirname "$(readlink -f "$0")")"
software="$here/lib/lavapipe"
if [ -f "$software/lvp_icd.json" ] && ! "$here/bin/dwe-game" --check-system >/dev/null 2>&1; then
  echo "dwe: no GPU here can run the game; using the software renderer (slow)." >&2
  echo "dwe: updating the graphics driver may fix it." >&2
  VK_DRIVER_FILES="$software/lvp_icd.json"
  VK_ICD_FILENAMES="$software/lvp_icd.json"
  LD_LIBRARY_PATH="$software${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export VK_DRIVER_FILES VK_ICD_FILENAMES LD_LIBRARY_PATH
fi
exec "$here/bin/dwe-game" "$@"
EOF
chmod +x "$stage/dwe"

cp "$root/scripts/package/README.md" "$stage/README.md"
printf '%s\n' "$commit" >"$stage/VERSION"
# The extractor version of this dwx: the launcher reads the player's disc again when it changes
# (docs/modulos/apps/launcher.md).
sed -n 's/^inline constexpr int ExtractorVersion = \([0-9]*\);.*/\1/p' \
  "$root/packages/engine/assets/include/dwe/assets/extractor_version.hpp" \
  >"$stage/EXTRACTOR_VERSION"
[[ -s "$stage/EXTRACTOR_VERSION" ]] || {
  echo "package_linux: cannot read ExtractorVersion" >&2
  exit 1
}

# The licences of the libraries the programs are built with: vcpkg's copyright file of each port (a
# few are only build tools; listing them too is harmless), the font's own and the C++ standard
# library's (linked statically).
licences="$stage/licenses"
mkdir -p "$licences"
for copyright in "$build_dir"/vcpkg_installed/x64-linux-libcxx-package/share/*/copyright; do
  port="$(basename "$(dirname "$copyright")")"
  [[ "$port" == vcpkg-* ]] || cp "$copyright" "$licences/$port.txt"
done
cp "$root/content/packs/game/ui/fonts/noto_sans_license.txt" "$licences/noto-sans.txt"
libcxx_copyright="/usr/share/doc/libc++-*-dev/copyright"
"$root/scripts/dev.sh" shell -c "cat $libcxx_copyright" >"$licences/llvm-libcxx.txt"

# The project's licence and THIRD_PARTY_NOTICES.txt: the credits, the Apache-2.0 NOTICE files and
# every licence text above in one file (scripts/package/third_party_notices.py).
cp "$root/LICENSE" "$stage/LICENSE"
python3 "$root/scripts/package/third_party_notices.py" game --licenses "$licences" \
  --out "$stage/THIRD_PARTY_NOTICES.txt"
python3 "$root/scripts/package/third_party_notices.py" check "$stage"

mkdir -p "$out_dir"
tarball="$out_dir/$name.tar.xz"
XZ_OPT=-T0 tar -C "$stage_root" -cJf "$tarball" "$name"
rm -rf "$stage_root"
echo "package_linux: $tarball ($(du -h "$tarball" | cut -f1))"
