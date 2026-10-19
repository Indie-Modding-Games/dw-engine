# Module helpers. Every engine and game module is declared with dwe_add_module, which applies the project options
# and enforces the layer rules of docs/architecture/dependency-rules.md at configure time.

# The layers, from the bottom up. A module may depend on its own layer or the layers below it.
set(DWE_LAYERS core base systems services game apps)

option(DWE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
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
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
        -Wold-style-cast -Wimplicit-fallthrough -fno-exceptions)
    if(DWE_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(DWE_SANITIZERS)
        list(JOIN DWE_SANITIZERS "," sanitizers)
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
    endif()
endfunction()

# dwe_add_module(<name> LAYER <layer> SOURCES <files...> [DEPS <modules...>] [PUBLIC_LIBS <libs...>]
#                [PRIVATE_LIBS <libs...>] [TEST_LIBS <libs...>])
# Declares the static library dwe::<name> from the current directory: include/ is public, src/ is private, and every
# tests/*.cpp file becomes part of the module's test executable.
function(dwe_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "SOURCES;DEPS;PUBLIC_LIBS;PRIVATE_LIBS;TEST_LIBS")
    _dwe_check_deps(${name} ${arg_LAYER} ${arg_DEPS})
    set_property(GLOBAL PROPERTY DWE_LAYER_${name} ${arg_LAYER})

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
        add_test(NAME ${name} COMMAND dwe_${name}_tests)
    endif()
endfunction()

# dwe_add_app(<name> LAYER <layer> SOURCES <files...> DEPS <modules...> [OUTPUT_NAME <name>])
# Declares an executable. libc++ is linked statically so the binary runs on a host without it.
function(dwe_add_app name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER;OUTPUT_NAME" "SOURCES;DEPS")
    _dwe_check_deps(${name} ${arg_LAYER} ${arg_DEPS})
    add_executable(${name} ${arg_SOURCES})
    list(TRANSFORM arg_DEPS PREPEND dwe:: OUTPUT_VARIABLE dep_targets)
    target_link_libraries(${name} PRIVATE ${dep_targets})
    target_link_options(${name} PRIVATE -static-libstdc++)
    if(arg_OUTPUT_NAME)
        set_target_properties(${name} PROPERTIES OUTPUT_NAME ${arg_OUTPUT_NAME})
    endif()
    dwe_apply_options(${name})
endfunction()
