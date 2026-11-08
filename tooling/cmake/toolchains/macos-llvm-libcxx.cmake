# Clang and libc++ of Homebrew's LLVM on macOS (scripts/package_macos.sh). Apple's Clang and the system's libc++ lag
# behind the C++26 the project is written in (std::print, std::ranges::to, std::views::zip, std::jthread without
# -fexperimental-library, floating-point std::from_chars) and gate parts of the library on the system's version, so
# the programs link Homebrew's libc++ and libc++abi statically, as libc++ is linked statically on Linux: a player's Mac
# needs nothing installed. Shared by the engine and by the arm64-osx-libcxx and x64-osx-libcxx triplets.

# vcpkg's own macOS toolchain first: it sets the architecture (CMAKE_OSX_ARCHITECTURES) from the triplet.
include("$ENV{VCPKG_ROOT}/scripts/toolchains/osx.cmake")

# Homebrew lives in /opt/homebrew on Apple silicon and in /usr/local on Intel Macs. HOMEBREW_PREFIX is set only by
# `brew shellenv` (a login shell), not in the CI's steps, so without it the first of the two folders that holds
# Homebrew's LLVM is taken. DWE_LLVM_ROOT overrides all of it.
if(DEFINED ENV{DWE_LLVM_ROOT})
    set(DWE_LLVM_ROOT "$ENV{DWE_LLVM_ROOT}")
elseif(DEFINED ENV{HOMEBREW_PREFIX})
    set(DWE_LLVM_ROOT "$ENV{HOMEBREW_PREFIX}/opt/llvm")
elseif(EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
    set(DWE_LLVM_ROOT "/opt/homebrew/opt/llvm")
else()
    set(DWE_LLVM_ROOT "/usr/local/opt/llvm")
endif()
# Said here, before CMake or vcpkg's compiler detection fail with a message about something else.
if(NOT EXISTS "${DWE_LLVM_ROOT}/bin/clang++")
    message(FATAL_ERROR "No Homebrew LLVM in ${DWE_LLVM_ROOT}: run `brew install llvm`, or set DWE_LLVM_ROOT to "
                        "the folder of `brew --prefix llvm`")
endif()

set(CMAKE_C_COMPILER "${DWE_LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "${DWE_LLVM_ROOT}/bin/clang++")
# The oldest macOS the programs run on: libc++ formats floating point (std::to_chars) from 13.3 on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "")

# Homebrew's clang reads its own libc++ headers; the link takes its static libc++ and libc++abi instead of the
# system's dynamic ones (-nostdlib++). Their symbols stay hidden in each program (-load_hidden): exported, the system's
# libc++ (loaded in every process) took some of them at run time, and Clang 23's typed operator new then aborted
# every program at start ("typed operator new being invoked before its static initializer").
string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
set(DWE_LIBCXX_STATIC "-nostdlib++ -Wl,-load_hidden,${DWE_LLVM_ROOT}/lib/c++/libc++.a")
string(APPEND DWE_LIBCXX_STATIC " -Wl,-load_hidden,${DWE_LLVM_ROOT}/lib/c++/libc++abi.a")
foreach(kind EXE SHARED MODULE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " ${DWE_LIBCXX_STATIC}")
endforeach()

# Apple's archiver, not llvm-ar next to Homebrew's clang: llvm-ar wrote an archive whose objects have no symbols
# (mbedtls' libeverest on arm64) in the GNU format, which the linker of Xcode 16 refuses.
find_program(DWE_APPLE_AR ar PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
find_program(DWE_APPLE_RANLIB ranlib PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
set(CMAKE_AR "${DWE_APPLE_AR}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DWE_APPLE_RANLIB}" CACHE FILEPATH "")
