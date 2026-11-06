# x64-windows-static built from Linux: static libraries and the static CRT, compiled with clang-cl against the MSVC
# libraries that xwin fetches (scripts/dev.sh windows-sdk). On Windows the presets use vcpkg's own x64-windows-static.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/windows-cross-clang-cl.cmake")
# On Windows, vcpkg's own toolchain gives the dependencies the static CRT. Here the toolchain's
# CMAKE_MSVC_RUNTIME_LIBRARY does, which a dependency that asks for an older CMake ignores without this policy.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_POLICY_DEFAULT_CMP0091=NEW)
# Source file names relative to the port's build folder, as in x64-linux-libcxx-package: some libraries keep __FILE__
# in their assertion messages, and no folder of the build machine may ship. The toolchain passes these flags on, since
# it does not include vcpkg's own Windows toolchain.
if(DEFINED CURRENT_BUILDTREES_DIR)
    set(VCPKG_C_FLAGS "/clang:-ffile-prefix-map=${CURRENT_BUILDTREES_DIR}/=${PORT}/")
    set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
