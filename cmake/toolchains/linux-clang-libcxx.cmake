# Clang with libc++ on Linux. Shared by the engine and by the vcpkg triplet, so every C++ library uses the same
# standard library.

# vcpkg's own Linux toolchain first: a chainloaded toolchain replaces it, and it sets what the ports rely on (the
# target processor, position-independent code for static libraries linked into shared ones, the triplet's flags).
include("$ENV{VCPKG_ROOT}/scripts/toolchains/linux.cmake")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
string(APPEND CMAKE_CXX_FLAGS_INIT " -stdlib=libc++")
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -stdlib=libc++ -fuse-ld=lld")
string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " -stdlib=libc++ -fuse-ld=lld")
