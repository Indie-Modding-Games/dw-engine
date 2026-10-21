find_package(SDL3 CONFIG REQUIRED)
# SDL3's package marks its headers as not being system headers, so their warnings would fail the build where they
# are hit, as in its MSVC code paths. They are a dependency like any other.
set_property(TARGET SDL3::Headers PROPERTY SYSTEM 1)

dwe_add_module(platform
    LAYER base
    SOURCES
        src/locale.cpp
        src/platform.cpp
        src/process.cpp
        src/window.cpp
    DEPS core
    PUBLIC_LIBS SDL3::SDL3)

if(BUILD_TESTING)
    # A program for the process tests to run: it writes its arguments and ends with the code it is given.
    add_executable(dwe_process_probe tests/probe/process_probe.cpp)
    dwe_apply_options(dwe_process_probe)
    dwe_use_utf8(dwe_process_probe)
    add_dependencies(dwe_platform_tests dwe_process_probe)
    target_compile_definitions(dwe_platform_tests PRIVATE DWE_PROCESS_PROBE="$<TARGET_FILE:dwe_process_probe>")
endif()
