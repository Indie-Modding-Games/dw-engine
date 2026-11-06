# Module helpers. Every engine and game module is declared with dwe_add_module, which applies the project options
# and enforces the layer rules of docs/architecture/dependency-rules.md at configure time.

# The layers, from the bottom up. A module may depend on its own layer or the layers below it.
set(DWE_LAYERS core base systems services game apps)

# CMake's default flags for MSVC-style compilers turn exceptions on; the project builds without them (ADR 0007).
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# CMake has no system include flag for clang-cl, so the headers of the dependencies come in with -I and their
# warnings fail the build. -imsvc makes them system headers, as -isystem does on Linux.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_INCLUDE_SYSTEM_FLAG_CXX)
    set(CMAKE_INCLUDE_SYSTEM_FLAG_C "-imsvc ")
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-imsvc ")
endif()

# On Windows a program's char strings (argv, paths made from std::string, the CRT's file functions) use the system's
# ANSI code page unless its manifest asks for UTF-8 (Windows 10 1903 and later). Every program of the project embeds
# this manifest, the tests too, so a path is UTF-8 on every system: one under %APPDATA% with a user name outside the
# ANSI code page works.
set(DWE_UTF8_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/windows/utf8.manifest")

function(dwe_use_utf8 target)
    if(WIN32)
        target_sources(${target} PRIVATE ${DWE_UTF8_MANIFEST})
    endif()
endfunction()

option(DWE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
# A package build (scripts/package_*.sh) names no folder of the build machine: the programs find their packs next
# to the executable instead of in the source tree, and source file names (__FILE__) are relative to it.
option(DWE_PACKAGE "Build the programs of a package, without the build machine's folders" OFF)
if(DWE_PACKAGE AND NOT MSVC)
    add_compile_options("-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")
elseif(DWE_PACKAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl takes clang's own options through /clang:.
    add_compile_options("/clang:-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")
endif()
if(DWE_PACKAGE AND MSVC)
    # A Windows program names its debug file (.pdb) in its header; the package's names only the file, which is shipped
    # apart (scripts/package_windows.sh), and not the build folder.
    add_link_options("/PDBALTPATH:%_PDB%")
endif()
set(DWE_SANITIZERS "" CACHE STRING "Sanitizers to enable, separated by semicolons (address;undefined)")

function(_dwe_layer_index layer out)
    list(FIND DWE_LAYERS "${layer}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "Unknown layer '${layer}'. Known layers: ${DWE_LAYERS}")
    endif()
    set(${out} ${index} PARENT_SCOPE)
endfunction()

# Fails the configuration if <name> in <layer> may not depend on each of <deps>.
function(_dwe_check_deps name layer)
    _dwe_layer_index(${layer} own)
    foreach(dep IN LISTS ARGN)
        get_property(dep_layer GLOBAL PROPERTY DWE_LAYER_${dep})
        if(NOT dep_layer)
            message(FATAL_ERROR "${name}: unknown module '${dep}'; declare it before its users")
        endif()
        if(dep STREQUAL "formats" AND NOT name STREQUAL "dwx")
            message(FATAL_ERROR "${name}: only dwx may use formats; the runtime never reads PS1 formats")
        endif()
        _dwe_layer_index(${dep_layer} theirs)
        if(theirs GREATER own)
            message(FATAL_ERROR "${name} (${layer}) may not depend on ${dep} (${dep_layer}): dependencies only go down")
        endif()
        if(layer STREQUAL "game" AND dep_layer STREQUAL "game")
            message(FATAL_ERROR "${name}: game modules may not use each other directly (${dep})")
        endif()
    endforeach()
endfunction()

# Applies the language level, warnings, exceptions policy and sanitizers to a target of the project.
function(dwe_apply_options target)
    target_compile_features(${target} PUBLIC cxx_std_26)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    set(warnings -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
        -Wimplicit-fallthrough)
    if(MSVC)
        # clang-cl takes clang's -W options as they are, but its -Wall means -Weverything, as cl's /Wall does: /W4 is
        # clang's -Wall -Wextra. Exceptions are off in the compiler and in the MSVC STL.
        target_compile_options(${target} PRIVATE /W4 ${warnings} /EHs-c-)
        # Without exceptions at() cannot report anything, so the STL checks indexing instead: debug builds already
        # do with _ITERATOR_DEBUG_LEVEL=2, and hardening adds the cheap checks to the others.
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0 _MSVC_STL_HARDENING=1)
        # The CRT marks standard functions such as getenv as deprecated in favour of its own _s variants, which the
        # code does not use: it stays standard C++.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(DWE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra ${warnings} -fno-exceptions)
        # Without exceptions at() cannot report anything, so libc++ checks indexing and other preconditions instead.
        target_compile_definitions(${target} PRIVATE
            _LIBCPP_HARDENING_MODE=$<IF:$<CONFIG:Debug>,_LIBCPP_HARDENING_MODE_EXTENSIVE,_LIBCPP_HARDENING_MODE_FAST>)
        if(DWE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(DWE_SANITIZERS)
        list(JOIN DWE_SANITIZERS "," sanitizers)
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
    endif()
endfunction()

# The test cases on the user's disc are named "the user's disc: ..." and read the whole disc, which takes minutes. Each
# one is a test of its own, labelled slow, so they run in parallel and the check of every commit can leave them out
# (ctest -LE slow); without DWE_DISC they pass at once. The cases are found by reading the sources when configuring,
# and <name>.disc runs any case added since, so none is ever skipped. An entry whose filter matches no case fails
# (tests/main.cpp).
function(_dwe_add_tests name target)
    set(disc "the user's disc")
    add_test(NAME ${name} COMMAND ${target} "--test-case-exclude=${disc}*")
    set(found "")
    foreach(source IN LISTS ARGN)
        # The whole file, so a name that clang-format moves to the line after TEST_CASE( is found too.
        file(READ ${source} text)
        string(REGEX MATCHALL "TEST_CASE\\([ \t\r\n]*\"${disc}[^\"]*\"" cases "${text}")
        # Editing a source with disc cases configures again, so a renamed case gets its entry on the next build.
        if(cases)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
        endif()
        foreach(case IN LISTS cases)
            string(REGEX REPLACE "^TEST_CASE\\([ \t\r\n]*\"([^\"]*)\"$" "\\1" case_name "${case}")
            # A comma separates doctest's filters: ? matches it.
            string(REPLACE "," "?" filter "${case_name}")
            add_test(NAME "${name}: ${case_name}" COMMAND ${target} "--test-case=${filter}")
            set_tests_properties("${name}: ${case_name}" PROPERTIES LABELS slow COST 1000)
            list(APPEND found "${filter}")
        endforeach()
    endforeach()
    set(rest "--test-case=${disc}*")
    if(found)
        list(JOIN found "," found)
        list(APPEND rest "--test-case-exclude=${found}")
    endif()
    # The catch-all usually matches nothing: tests/main.cpp lets it pass then.
    add_test(NAME ${name}.disc COMMAND ${target} ${rest} --dwe-allow-no-match)
    set_tests_properties(${name}.disc PROPERTIES LABELS slow)
endfunction()

# dwe_add_module(<name> LAYER <layer> SOURCES <files...> [DEPS <modules...>] [PUBLIC_LIBS <libs...>]
#                [PRIVATE_LIBS <libs...>] [TEST_LIBS <libs...>])
# Declares the static library dwe::<name> from the current directory: include/ is public, src/ is private, and every
# tests/*.cpp file becomes part of the module's test executable.
function(dwe_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "SOURCES;DEPS;PUBLIC_LIBS;PRIVATE_LIBS;TEST_LIBS")
    _dwe_check_deps(${name} ${arg_LAYER} ${arg_DEPS})
    set_property(GLOBAL PROPERTY DWE_LAYER_${name} ${arg_LAYER})
    # The game modules, for the apps that may use them all (dwe-sim's scenarios).
    if(arg_LAYER STREQUAL "game")
        set_property(GLOBAL APPEND PROPERTY DWE_GAME_MODULES ${name})
    endif()

    add_library(dwe_${name} STATIC ${arg_SOURCES})
    add_library(dwe::${name} ALIAS dwe_${name})
    target_include_directories(dwe_${name}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    list(TRANSFORM arg_DEPS PREPEND dwe:: OUTPUT_VARIABLE dep_targets)
    target_link_libraries(dwe_${name} PUBLIC ${dep_targets} ${arg_PUBLIC_LIBS} PRIVATE ${arg_PRIVATE_LIBS})
    dwe_apply_options(dwe_${name})

    file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
    if(BUILD_TESTING AND test_sources)
        add_executable(dwe_${name}_tests ${test_sources})
        target_link_libraries(dwe_${name}_tests PRIVATE dwe::${name} dwe_test_main ${arg_TEST_LIBS})
        target_include_directories(dwe_${name}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        dwe_apply_options(dwe_${name}_tests)
        dwe_use_utf8(dwe_${name}_tests)
        _dwe_add_tests(${name} dwe_${name}_tests ${test_sources})
    endif()
endfunction()

# dwe_add_app(<name> LAYER <layer> SOURCES <files...> DEPS <modules...> [OUTPUT_NAME <name>])
# Declares an executable. On Linux libc++ is linked statically so the binary runs on a host without it; on Windows the
# toolchain selects the static CRT.
function(dwe_add_app name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER;OUTPUT_NAME" "SOURCES;DEPS")
    _dwe_check_deps(${name} ${arg_LAYER} ${arg_DEPS})
    add_executable(${name} ${arg_SOURCES})
    list(TRANSFORM arg_DEPS PREPEND dwe:: OUTPUT_VARIABLE dep_targets)
    target_link_libraries(${name} PRIVATE ${dep_targets})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${name} PRIVATE -static-libstdc++)
    endif()
    if(arg_OUTPUT_NAME)
        set_target_properties(${name} PROPERTIES OUTPUT_NAME ${arg_OUTPUT_NAME})
    endif()
    dwe_apply_options(${name})
    dwe_use_utf8(${name})
endfunction()
