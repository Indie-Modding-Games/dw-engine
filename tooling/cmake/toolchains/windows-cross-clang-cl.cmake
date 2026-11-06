# clang-cl with the MSVC standard library, from Linux: the Windows build of windows-ci compiled and linked in the
# development container. The MSVC CRT and STL and the Windows SDK come from xwin (scripts/dev.sh windows-sdk) in the
# layout of a Visual Studio installation, which clang-cl and lld-link read with /winsysroot. Shared by the engine and
# by the x64-windows-static-cross triplet, like linux-clang-libcxx.cmake.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# The engine's Windows options (the static CRT) are the same as on Windows.
include("${CMAKE_CURRENT_LIST_DIR}/windows-clang-cl.cmake")

set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

set(DWE_WINSYSROOT "/opt/xwin/sysroot")
# Joined to the option: ccache would take a separate absolute path for a file and drop it.
foreach(lang C CXX)
    string(APPEND CMAKE_${lang}_FLAGS_INIT " /winsysroot${DWE_WINSYSROOT}")
    # A vcpkg port's flags from the triplet (x64-windows-static-cross), which vcpkg's own toolchains add themselves.
    if(VCPKG_${lang}_FLAGS)
        string(APPEND CMAKE_${lang}_FLAGS_INIT " ${VCPKG_${lang}_FLAGS}")
    endif()
endforeach()
# llvm-rc does not read /winsysroot: the resource scripts get the SDK's headers, of its newest version, by path.
file(GLOB DWE_WINSDK_INCLUDE LIST_DIRECTORIES true "${DWE_WINSYSROOT}/Windows Kits/10/Include/*")
list(SORT DWE_WINSDK_INCLUDE COMPARE NATURAL ORDER DESCENDING)
list(GET DWE_WINSDK_INCLUDE 0 DWE_WINSDK_INCLUDE)
foreach(dir um shared ucrt)
    string(APPEND CMAKE_RC_FLAGS_INIT " -I \"${DWE_WINSDK_INCLUDE}/${dir}\"")
endforeach()
foreach(kind EXE SHARED MODULE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS_INIT " /winsysroot:${DWE_WINSYSROOT}")
endforeach()

# Only the Windows libraries and headers: never a program or a package of the Linux system.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
