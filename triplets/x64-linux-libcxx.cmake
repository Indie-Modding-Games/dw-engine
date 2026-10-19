# Static libraries built with Clang and libc++, like the engine itself.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/linux-clang-libcxx.cmake")

# The Vulkan validation layers are loaded by the Vulkan loader at runtime, so they are a shared library. libc++ is
# linked into it statically, because the host that runs the game does not have libc++.
if(PORT STREQUAL "vulkan-validationlayers")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
    set(VCPKG_LINKER_FLAGS "-static-libstdc++")
endif()
