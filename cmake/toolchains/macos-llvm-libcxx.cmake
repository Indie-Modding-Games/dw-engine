# Clang and libc++ of Homebrew's LLVM on macOS (scripts/package_macos.sh). Apple's Clang and the system's libc++ lag
# behind the C++26 the project is written in (std::print, std::ranges::to, std::views::zip, std::jthread without
# -fexperimental-library, floating-point std::from_chars) and gate parts of the library on the system's version, so
# the programs link Homebrew's libc++ and libc++abi statically, as libc++ is linked statically on Linux: a player's Mac
# needs nothing installed. Shared by the engine and by the arm64-osx-libcxx and x64-osx-libcxx triplets.

# vcpkg's own macOS toolchain first: it sets the architecture (CMAKE_OSX_ARCHITECTURES) from the triplet.
include("$ENV{VCPKG_ROOT}/scripts/toolchains/osx.cmake")

# HOMEBREW_PREFIX is /opt/homebrew on Apple silicon and /usr/local on Intel Macs; DWE_LLVM_ROOT overrides it.
if(DEFINED ENV{DWE_LLVM_ROOT})
    set(DWE_LLVM_ROOT "$ENV{DWE_LLVM_ROOT}")
elseif(DEFINED ENV{HOMEBREW_PREFIX})
    set(DWE_LLVM_ROOT "$ENV{HOMEBREW_PREFIX}/opt/llvm")
else()
    set(DWE_LLVM_ROOT "/opt/homebrew/opt/llvm")
endif()

set(CMAKE_C_COMPILER "${DWE_LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${DWE_LLVM_ROOT}/bin/clang++")
# The oldest macOS the programs run on; MoltenVK and the launcher ask for less.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")

# Homebrew's clang reads its own libc++ headers; the link takes its static libc++ and libc++abi instead of the
# system's dynamic ones (-nostdlib++).
string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
set(DWE_LIBCXX_STATIC "-nostdlib++ ${DWE_LLVM_ROOT}/lib/c++/libc++.a ${DWE_LLVM_ROOT}/lib/c++/libc++abi.a")
foreach(kind EXE SHARED MODULE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " ${DWE_LIBCXX_STATIC}")
endforeach()
