# x64-linux-libcxx with release libraries only: the CI uses it to build each dependency once instead of twice.
include("${CMAKE_CURRENT_LIST_DIR}/x64-linux-libcxx.cmake")
set(VCPKG_BUILD_TYPE release)
