# Clang and libc++ of Homebrew's LLVM on macOS (scripts/package_macos.sh). Apple's Clang and the system's libc++ lag
# behind the C++26 the project is written in (std::print, std::ranges::to, std::views::zip, std::jthread without
# -fexperimental-library, floating-point std::from_chars) and gate parts of the library on the system's version, so
# the programs link Homebrew's libc++ and libc++abi statically, as libc++ is linked statically on Linux: a player's Mac
# needs nothing installed. Shared by the engine and by the arm64-osx-libcxx and x64-osx-libcxx triplets.

# The engine's own build has no triplet: its processor is the preset's CMAKE_OSX_ARCHITECTURES (x86_64 on an Apple
# silicon Mac too, when scripts/package_macos.sh --arch x64 builds an Intel package there), not this Mac's.
if(NOT DEFINED VCPKG_TARGET_ARCHITECTURE AND CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64|arm64)$")
    set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}" CACHE STRING "")
endif()
# vcpkg's own macOS toolchain first: it sets the processor from the triplet.
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

# Building for the other processor (Intel on Apple silicon): Homebrew's clang has a configuration file with the SDK
# only for this Mac's processor, so the SDK is named here.
if(CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64|arm64)$" AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR
   AND NOT CMAKE_OSX_SYSROOT)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE DWE_MACOS_SDK
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE DWE_XCRUN_RESULT)
    if(DWE_XCRUN_RESULT EQUAL 0)
        set(CMAKE_OSX_SYSROOT "${DWE_MACOS_SDK}" CACHE PATH "")
    endif()
endif()

# Homebrew's clang reads its own libc++ headers; the link takes its static libc++ and libc++abi instead of the
# system's dynamic ones (-nostdlib++). Their symbols stay hidden in each program (-load_hidden): exported, the system's
# libc++ (loaded in every process) took some of them at run time, and Clang 23's typed operator new then aborted
# every program at start ("typed operator new being invoked before its static initializer").
#
# Homebrew's libc++ is built for this Mac's processor only: an Intel package built on Apple silicon links the ones
# scripts/package/macos_cross_libs.sh builds from the same LLVM release (DWE_LIBCXX_DIR, set by package_macos.sh).
if(DEFINED ENV{DWE_LIBCXX_DIR})
    set(DWE_LIBCXX_DIR "$ENV{DWE_LIBCXX_DIR}")
else()
    set(DWE_LIBCXX_DIR "${DWE_LLVM_ROOT}/lib/c++")
endif()
# Said here: a libc++ for another processor fails only at the first link, with a list of undefined symbols.
if(CMAKE_OSX_ARCHITECTURES MATCHES "^(x86_64|arm64)$")
    execute_process(COMMAND lipo -archs "${DWE_LIBCXX_DIR}/libc++.a" OUTPUT_VARIABLE DWE_LIBCXX_ARCHS
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE DWE_LIPO_RESULT ERROR_QUIET)
    # list(FIND), not IN_LIST: the ports' projects read this file under their own, older policies.
    separate_arguments(DWE_LIBCXX_ARCHS UNIX_COMMAND "${DWE_LIBCXX_ARCHS}")
    list(FIND DWE_LIBCXX_ARCHS "${CMAKE_OSX_ARCHITECTURES}" DWE_LIBCXX_ARCH_INDEX)
    if(DWE_LIPO_RESULT EQUAL 0 AND DWE_LIBCXX_ARCH_INDEX EQUAL -1)
        message(FATAL_ERROR "${DWE_LIBCXX_DIR}/libc++.a is for ${DWE_LIBCXX_ARCHS}, not ${CMAKE_OSX_ARCHITECTURES}: "
                            "build the package with scripts/package_macos.sh, which builds a libc++ for it")
    endif()
endif()
string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
set(DWE_LIBCXX_STATIC "-nostdlib++ -Wl,-load_hidden,${DWE_LIBCXX_DIR}/libc++.a")
string(APPEND DWE_LIBCXX_STATIC " -Wl,-load_hidden,${DWE_LIBCXX_DIR}/libc++abi.a")
foreach(kind EXE SHARED MODULE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " ${DWE_LIBCXX_STATIC}")
endforeach()

# Apple's archiver, not llvm-ar next to Homebrew's clang: llvm-ar wrote an archive whose objects have no symbols
# (mbedtls' libeverest on arm64) in the GNU format, which the linker of Xcode 16 refuses.
find_program(DWE_APPLE_AR ar PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
find_program(DWE_APPLE_RANLIB ranlib PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
set(CMAKE_AR "${DWE_APPLE_AR}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DWE_APPLE_RANLIB}" CACHE FILEPATH "")
