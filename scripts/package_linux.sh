#!/usr/bin/env bash
# Builds the Linux beta package: a tarball a player unpacks and runs. It holds the game and dwx
# (release build), the team's packs (content/packs), the third-party licences and the package
# README, and never anything read from a disc: each player reads their own disc on the first start
# (docs/guides/beta-linux.md).
#
# Usage: scripts/package_linux.sh [--no-build] [--out <folder>]
#   --no-build   package the build as it is (build/linux-clang-package), without building it first
#   --out        where the tarball goes (default build/package); a folder outside the repository is
#                fine
# The build runs in the development container (scripts/dev.sh); packaging needs only bash, tar and
# xz on the host.
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
      sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "package_linux: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done

commit="$(git -C "$root" describe --always --dirty)"
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

# No folder of the build machine may ship: a package build (DWE_PACKAGE) finds its packs next to the
# executable, and the sources' and the libraries' file names are relative.
for program in dwe-game dwx; do
  if strings -n 6 "$stage/bin/$program" | grep -F -q -e "$root" -e "/home/"; then
    echo "package_linux: $program names a folder of the build machine:" >&2
    strings -n 6 "$stage/bin/$program" | grep -F -e "$root" -e "/home/" | head -5 >&2
    exit 1
  fi
done

# Nothing extracted from a disc may ship: the team's packs only, with no base pack and no extracted
# asset.
if find "$stage" -name pack.yaml -exec grep -l '^id: base$' {} + | grep -q .; then
  echo "package_linux: a base pack is in the package; it must never ship" >&2
  exit 1
fi

# The launcher a player runs: the game from wherever the package was unpacked.
cat >"$stage/dwe" <<'EOF'
#!/bin/sh
# Starts the game from the folder this script is in; options go to the game (README.md).
here="$(dirname "$(readlink -f "$0")")"
exec "$here/bin/dwe-game" "$@"
EOF
chmod +x "$stage/dwe"

cp "$root/scripts/package/README.md" "$stage/README.md"
printf '%s\n' "$commit" >"$stage/VERSION"
# The extractor version of this dwx: the launcher reads the player's disc again when it changes
# (docs/modulos/apps/launcher.md).
sed -n 's/^inline constexpr int ExtractorVersion = \([0-9]*\);.*/\1/p' \
  "$root/engine/assets/include/dwe/assets/extractor_version.hpp" >"$stage/EXTRACTOR_VERSION"
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

mkdir -p "$out_dir"
tarball="$out_dir/$name.tar.xz"
tar -C "$stage_root" -cJf "$tarball" "$name"
rm -rf "$stage_root"
echo "package_linux: $tarball ($(du -h "$tarball" | cut -f1))"
