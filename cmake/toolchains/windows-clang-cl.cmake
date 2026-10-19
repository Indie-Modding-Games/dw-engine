# clang-cl with the MSVC standard library on Windows. libc++ is not used here: the MSVC STL is the library every
# Windows port of vcpkg is built and tested against.

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)

# The static CRT (/MT), to match the x64-windows-static triplet of the presets: like the static libc++ on Linux, the
# game then runs without the player installing a runtime. The two Vulkan ports that are always DLLs (the loader and
# the validation layers) carry their own copy of the CRT, which is safe because the Vulkan API passes no CRT objects
# (memory, files) across the DLL boundary.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
