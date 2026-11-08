# x64-linux-libcxx with release libraries only: the CI uses it to build each dependency once instead of twice.
include("${CMAKE_CURRENT_LIST_DIR}/x64-linux-libcxx.cmake")
set(VCPKG_BUILD_TYPE release)

# The programs built on this triplet (the ci preset) run with AddressSanitizer, and with it libc++ poisons the unused
# capacity of every std::vector. The linker keeps one copy of a vector's member functions for the whole program, the
# engine's instrumented one, while a release library inlines the rest: an uninstrumented push into poisoned capacity,
# then a growth whose copy reads it, is reported as an overflow. libdatachannel's release code does exactly that, so
# it is built with the sanitizer here too and its vectors agree with the engine's.
if(PORT STREQUAL "libdatachannel")
    set(VCPKG_C_FLAGS "-fsanitize=address")
    set(VCPKG_CXX_FLAGS "-fsanitize=address")
    set(VCPKG_LINKER_FLAGS "-fsanitize=address")
endif()
