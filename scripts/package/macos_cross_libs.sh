#!/usr/bin/env bash
# The libraries an Intel (x86_64) macOS package needs when it is built on Apple silicon
# (scripts/package_macos.sh --arch x64): Homebrew installs LLVM's libc++ and MoltenVK for this Mac's
# processor only.
#
#   <out>/libcxx/libc++.a, libc++abi.a   built from the sources of the LLVM release in DWE_LLVM_ROOT
#                                         with the options of Homebrew's build, so they match the
#                                         headers its clang compiles with (checked: the same
#                                         __config_site)
#   <out>/moltenvk/libMoltenVK.dylib      MoltenVK's own release build (both processors), thinned
#   <out>/moltenvk/LICENSE
#
# A folder already built for the same LLVM, MoltenVK and minimum macOS is kept as it is (the CI
# caches it, .github/actions/macos-tools).
#
# Usage: scripts/package/macos_cross_libs.sh --arch x64|arm64 --out <folder> [--minimum-macos <x.y>]
# Needs Homebrew's LLVM (DWE_LLVM_ROOT, or `brew --prefix llvm`), CMake, Ninja and Xcode's SDK.
set -euo pipefail

arch=""
out=""
minimum_macos="13.3"
# MoltenVK's release; its tarball's SHA-256 is checked.
moltenvk_version="1.4.2"
moltenvk_sha256="f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e"

while (($#)); do
  case "$1" in
    --arch)
      arch="$2"
      shift
      ;;
    --out)
      out="$2"
      shift
      ;;
    --minimum-macos)
      minimum_macos="$2"
      shift
      ;;
    -h | --help)
      sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "macos_cross_libs: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift
done
case "$arch" in
  x64) target=x86_64 ;;
  arm64) target=arm64 ;;
  *)
    echo "macos_cross_libs: --arch is x64 or arm64" >&2
    exit 2
    ;;
esac
[[ -n "$out" ]] || {
  echo "macos_cross_libs: --out <folder> is required" >&2
  exit 2
}
[[ "$(uname -s)" == Darwin ]] || {
  echo "macos_cross_libs: runs on macOS only" >&2
  exit 1
}

llvm="${DWE_LLVM_ROOT:-$(brew --prefix llvm)}"
version="$("$llvm/bin/llvm-config" --version)"
stamp="llvm $version, MoltenVK $moltenvk_version, $target, macOS $minimum_macos"
if [[ -f "$out/STAMP" && "$(cat "$out/STAMP")" == "$stamp" ]]; then
  echo "macos_cross_libs: $out already holds $stamp"
  exit 0
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/dwe-macos-cross.XXXXXX")"
trap 'rm -rf "$work"' EXIT
sdk="$(xcrun --sdk macosx --show-sdk-path)"
result="$work/out"
mkdir -p "$result/libcxx" "$result/moltenvk"

# The sources Homebrew built this LLVM from: its formula's URL and checksum. Its patches do not
# touch libc++, libc++abi or libunwind. DWE_LLVM_ROOT is `brew --prefix llvm@<major>` (or llvm).
formula="$(basename "$llvm")"
[[ "$formula" =~ ^llvm(@[0-9]+)?$ ]] || formula="llvm@${version%%.*}"
read -r url checksum < <(brew info --json=v2 "$formula" | python3 -c '
import json, sys
stable = json.load(sys.stdin)["formulae"][0]["urls"]["stable"]
print(stable["url"], stable["checksum"])')
[[ "$url" == */llvmorg-"$version"/* ]] || {
  echo "macos_cross_libs: Homebrew's $formula builds $url, not LLVM $version:" \
    "brew upgrade $formula" >&2
  exit 1
}
curl -fsSL --retry 3 --max-time 900 -o "$work/llvm.tar.xz" "$url"
echo "$checksum  $work/llvm.tar.xz" | shasum -a 256 -c - >/dev/null
src_name="$(basename "$url" .tar.xz)"
# Only the runtimes' folders are needed; the biggest of the rest stay packed.
excluded=()
for folder in clang clang-tools-extra mlir flang flang-rt lldb lld polly openmp bolt offload \
  libclc compiler-rt cross-project-tests llvm/test llvm/unittests; do
  excluded+=(--exclude "$src_name/$folder")
done
tar -x "${excluded[@]}" -J -f "$work/llvm.tar.xz" -C "$work"
rm "$work/llvm.tar.xz"
src="$work/$src_name"

# Homebrew's macOS options for libc++ (Formula/l/llvm@<major>.rb): vendor availability
# annotations, the libdispatch parallel algorithms backend, libunwind beside libc++abi. Only the
# static libraries; the build folders are left out of the debug paths.
cmake -G Ninja -S "$src/runtimes" -B "$work/build" -Wno-dev \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$work/install" \
  -DCMAKE_C_COMPILER="$llvm/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm/bin/clang++" \
  -DCMAKE_OSX_ARCHITECTURES="$target" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum_macos" \
  -DCMAKE_OSX_SYSROOT="$sdk" \
  -DCMAKE_C_FLAGS="-ffile-prefix-map=$work/=" \
  -DCMAKE_CXX_FLAGS="-ffile-prefix-map=$work/=" \
  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
  -DLIBCXX_ENABLE_VENDOR_AVAILABILITY_ANNOTATIONS=ON \
  -DLIBCXX_PSTL_BACKEND=libdispatch \
  -DLIBCXX_ENABLE_SHARED=OFF \
  -DLIBCXXABI_ENABLE_SHARED=OFF \
  -DLIBUNWIND_ENABLE_SHARED=OFF \
  -DLIBCXX_INCLUDE_TESTS=OFF \
  -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
  -DLIBCXXABI_INCLUDE_TESTS=OFF \
  -DLIBUNWIND_INCLUDE_TESTS=OFF \
  >"$work/configure.log" || {
  tail -n 60 "$work/configure.log" >&2
  exit 1
}
cmake --build "$work/build"
cmake --install "$work/build" >/dev/null

# The headers the compiler reads are Homebrew's; the library must have been built with the same
# configuration.
config_site="include/c++/v1/__config_site"
if ! cmp -s "$work/install/$config_site" "$llvm/$config_site"; then
  diff "$llvm/$config_site" "$work/install/$config_site" >&2 || true
  echo "macos_cross_libs: this libc++'s __config_site differs from Homebrew's (above)" >&2
  exit 1
fi
for library in libc++.a libc++abi.a; do
  cp "$work/install/lib/$library" "$result/libcxx/$library"
  [[ "$(lipo -archs "$result/libcxx/$library")" == "$target" ]] || {
    echo "macos_cross_libs: $library is not for $target" >&2
    exit 1
  }
done

curl -fsSL --retry 3 --max-time 600 -o "$work/moltenvk.tar" \
  "https://github.com/KhronosGroup/MoltenVK/releases/download/v$moltenvk_version/MoltenVK-macos.tar"
echo "$moltenvk_sha256  $work/moltenvk.tar" | shasum -a 256 -c - >/dev/null
dylib="MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
tar -xf "$work/moltenvk.tar" -C "$work" MoltenVK/LICENSE "$dylib"
lipo "$work/$dylib" -thin "$target" -output "$result/moltenvk/libMoltenVK.dylib"
cp "$work/MoltenVK/LICENSE" "$result/moltenvk/LICENSE"

printf '%s\n' "$stamp" >"$result/STAMP"
rm -rf "$out"
mkdir -p "$(dirname "$out")"
mv "$result" "$out"
echo "macos_cross_libs: $stamp in $out"
