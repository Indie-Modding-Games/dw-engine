#!/usr/bin/env bash
# Builds the macOS package on a Mac (the CI's macOS runners; there is no cross build): a tarball
# with the game as an application bundle and the same files around it as the Linux package.
#
#   dwe-beta-<version>-<commit>-darwin-<arch>/
#     dwe-game.app/Contents/MacOS/        dwe-game and dwx
#     dwe-game.app/Contents/Frameworks/   the Vulkan loader and MoltenVK (Vulkan over Metal)
#     dwe-game.app/Contents/Resources/    the team's packs and the ICD file that points the loader
#                                         at MoltenVK
#     README.md, VERSION, EXTRACTOR_VERSION, LICENSE, THIRD_PARTY_NOTICES.txt, licenses/
#
# The programs link Homebrew LLVM's libc++ statically
# (tooling/cmake/toolchains/macos-llvm-libcxx.cmake),
# so a player's Mac needs nothing installed. The bundle is signed ad hoc (codesign -s -), which
# Apple silicon requires to run anything; it is not notarized (docs/guides/beta-windows-macos.md).
#
# Usage: scripts/package_macos.sh [--arch arm64|x64] [--no-build] [--out <folder>]
#   --arch       the processor (default: this Mac's)
#   --no-build   package the build as it is (build/macos-<arch>-package)
#   --out        where the tarball goes (default build/package)
# Needs Homebrew's llvm, molten-vk and ninja, cmake and VCPKG_ROOT (a vcpkg checkout).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=""
out_dir="$root/build/package"
build=1
minimum_macos="13.3"

while (($#)); do
  case "$1" in
    --arch)
      arch="$2"
      shift
      ;;
    --no-build) build=0 ;;
    --out)
      out_dir="$2"
      shift
      ;;
    -h | --help)
      sed -n '2,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "package_macos: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done

[[ "$(uname -s)" == Darwin ]] || {
  echo "package_macos: runs on macOS only" >&2
  exit 1
}
if [[ -z "$arch" ]]; then
  [[ "$(uname -m)" == arm64 ]] && arch=arm64 || arch=x64
fi
case "$arch" in
  arm64) key="darwin-aarch64" ;;
  x64) key="darwin-x86_64" ;;
  *)
    echo "package_macos: --arch is arm64 or x64" >&2
    exit 2
    ;;
esac
preset="macos-$arch-package"
build_dir="$root/build/$preset"
triplet="$arch-osx-libcxx"

commit="$(git -C "$root" describe --always --dirty)"
version="$(sed -n 's/^project(dwe VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
name="dwe-beta-${version:-0}-${commit}-$key"
stage_root="$root/build/package-stage-$key"
stage="$stage_root/$name"
app="$stage/dwe-game.app/Contents"

if ((build)); then
  cmake --preset "$preset" -DDWE_BUILD_COMMIT="$commit"
  cmake --build --preset "$preset" --target game dwx_cli
fi

rm -rf "$stage_root"
mkdir -p "$stage" "$out_dir/$name-debug"
installed="$stage_root/installed"
cmake --install "$build_dir" --prefix "$installed" >/dev/null
mkdir -p "$app/MacOS" "$app/Frameworks" "$app/Resources/vulkan/icd.d"
for program in dwe-game dwx; do
  cp "$installed/bin/$program" "$app/MacOS/$program"
  # The debug information stays beside the tarball, for crash reports.
  dsymutil "$app/MacOS/$program" -o "$out_dir/$name-debug/$program.dSYM"
  strip -S "$app/MacOS/$program"
done
mv "$installed/packs" "$app/Resources/packs"
rm -rf "$installed"

# The Vulkan loader the game links (@rpath/libvulkan.1.dylib), copied as a plain file: the launcher
# unpacks no links.
loader_name="$(otool -L "$app/MacOS/dwe-game" \
  | sed -n 's|^[[:space:]]*@rpath/\(libvulkan[^ ]*\) .*|\1|p')"
[[ -n "$loader_name" ]] || {
  echo "package_macos: dwe-game does not link the Vulkan loader through @rpath" >&2
  exit 1
}
cp -L "$build_dir/vcpkg_installed/$triplet/lib/$loader_name" "$app/Frameworks/$loader_name"
install_name_tool -id "@rpath/$loader_name" "$app/Frameworks/$loader_name"
moltenvk="$(brew --prefix molten-vk)/lib/libMoltenVK.dylib"
cp -L "$moltenvk" "$app/Frameworks/libMoltenVK.dylib"
install_name_tool -id "@rpath/libMoltenVK.dylib" "$app/Frameworks/libMoltenVK.dylib"
# The loader looks for drivers in the bundle's Contents/Resources/vulkan/icd.d; the library path is
# relative to this file.
cat >"$app/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'EOF'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "../../../Frameworks/libMoltenVK.dylib",
    "api_version": "1.3.0",
    "is_portability_driver": true
  }
}
EOF

# Every library the programs and the shipped libraries load is the system's or in the bundle.
for binary in "$app"/MacOS/* "$app"/Frameworks/*.dylib; do
  otool -L "$binary" | tail -n +2 | awk '{print $1}' | while read -r library; do
    case "$library" in
      /usr/lib/* | /System/Library/*) ;;
      @rpath/libvulkan* | @rpath/libMoltenVK.dylib) ;;
      @rpath/*)
        [[ "$library" == "@rpath/$(basename "$binary")" ]] || {
          echo "package_macos: $(basename "$binary") loads $library, which is not shipped" >&2
          exit 1
        }
        ;;
      *)
        echo "package_macos: $(basename "$binary") loads $library, which is not shipped" >&2
        exit 1
        ;;
    esac
  done
done
for program in "$app"/MacOS/*; do
  if strings -n 6 "$program" | grep -F -q -e "$root" -e "/Users/"; then
    echo "package_macos: $(basename "$program") names a folder of the build machine" >&2
    exit 1
  fi
done
if find "$stage" -name pack.yaml -exec grep -l '^id: base$' {} + | grep -q .; then
  echo "package_macos: a base pack is in the package; it must never ship" >&2
  exit 1
fi

cat >"$app/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>dwe-game</string>
  <key>CFBundleIdentifier</key><string>org.indie-modding-games.dwe-game</string>
  <key>CFBundleName</key><string>dwe</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${version:-0}</string>
  <key>CFBundleVersion</key><string>${version:-0}</string>
  <key>LSMinimumSystemVersion</key><string>$minimum_macos</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

# Ad hoc signatures, inside out: the libraries, dwx, then the bundle (its main program).
for code in "$app"/Frameworks/*.dylib "$app/MacOS/dwx"; do
  codesign --force --sign - "$code"
done
codesign --force --sign - "$stage/dwe-game.app"
codesign --verify --deep --strict "$stage/dwe-game.app"

cp "$root/scripts/package/README-macos.md" "$stage/README.md"
printf '%s\n' "$commit" >"$stage/VERSION"
sed -n 's/^inline constexpr int ExtractorVersion = \([0-9]*\);.*/\1/p' \
  "$root/packages/engine/assets/include/dwe/assets/extractor_version.hpp" \
  >"$stage/EXTRACTOR_VERSION"
[[ -s "$stage/EXTRACTOR_VERSION" ]] || {
  echo "package_macos: cannot read ExtractorVersion" >&2
  exit 1
}
licences="$stage/licenses"
mkdir -p "$licences"
for copyright in "$build_dir"/vcpkg_installed/"$triplet"/share/*/copyright; do
  port="$(basename "$(dirname "$copyright")")"
  [[ "$port" == vcpkg-* ]] || cp "$copyright" "$licences/$port.txt"
done
cp "$root/content/packs/game/ui/fonts/noto_sans_license.txt" "$licences/noto-sans.txt"
cp "$(brew --prefix molten-vk)/LICENSE" "$licences/moltenvk.txt" 2>/dev/null \
  || echo "MoltenVK: Apache License 2.0" >"$licences/moltenvk.txt"
llvm_licence="$(brew --prefix llvm)/LICENSE.TXT"
[[ -f "$llvm_licence" ]] && cp "$llvm_licence" "$licences/llvm-libcxx.txt"

# The project's licence and THIRD_PARTY_NOTICES.txt: the credits, the Apache-2.0 NOTICE files and
# every licence text above in one file (scripts/package/third_party_notices.py).
cp "$root/LICENSE" "$stage/LICENSE"
python3 "$root/scripts/package/third_party_notices.py" game --licenses "$licences" \
  --out "$stage/THIRD_PARTY_NOTICES.txt"
python3 "$root/scripts/package/third_party_notices.py" check "$stage"

mkdir -p "$out_dir"
tarball="$out_dir/$name.tar.xz"
tar -C "$stage_root" -cJf "$tarball" "$name"
rm -rf "$stage_root"
echo "package_macos: $tarball ($(du -h "$tarball" | cut -f1))"
