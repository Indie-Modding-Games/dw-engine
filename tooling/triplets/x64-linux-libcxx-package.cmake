# x64-linux-libcxx for a package (scripts/package_linux.sh): release libraries only, built with their source file
# names relative to their build folder, since some libraries keep __FILE__ in their assertion messages and no folder of
# the build machine may ship.
include("${CMAKE_CURRENT_LIST_DIR}/x64-linux-libcxx.cmake")
set(VCPKG_BUILD_TYPE release)
if(DEFINED CURRENT_BUILDTREES_DIR)
    set(VCPKG_C_FLAGS "-ffile-prefix-map=${CURRENT_BUILDTREES_DIR}/=${PORT}/")
    set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
