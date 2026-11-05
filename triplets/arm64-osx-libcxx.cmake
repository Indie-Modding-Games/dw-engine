# arm64-osx built with Homebrew's Clang and libc++ (cmake/toolchains/macos-llvm-libcxx.cmake), like the engine: static
# libraries, release only (a package build). The Vulkan loader is a shared library inside the application bundle
# (Contents/Frameworks), next to MoltenVK, so it finds MoltenVK through the bundle's ICD file
# (scripts/package_macos.sh).
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/macos-llvm-libcxx.cmake")
if(PORT STREQUAL "vulkan-loader")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
if(DEFINED CURRENT_BUILDTREES_DIR)
    set(VCPKG_C_FLAGS "-ffile-prefix-map=${CURRENT_BUILDTREES_DIR}/=${PORT}/")
    set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
endif()
