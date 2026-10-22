cmake_minimum_required(VERSION 3.30)

project(dwe VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The project uses headers, not C++ modules (ADR 0008): without the scan, no source is preprocessed twice, and the
# compile commands that clang-tidy reads exist right after configuring, before any build.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(CTest)
include(cmake/DweModule.cmake)
include(cmake/DweShaders.cmake)

if(BUILD_TESTING)
    find_package(doctest CONFIG REQUIRED)
    add_library(dwe_test_main STATIC tests/main.cpp tests/support/src/user_assets.cpp)
    target_link_libraries(dwe_test_main PUBLIC doctest::doctest)
    # What the tests of every module share (tests/support/README.md).
    target_include_directories(dwe_test_main PUBLIC ${PROJECT_SOURCE_DIR}/tests/support/include)
    # doctest prints failed comparisons through std::ostream, which libc++ brings in with <string> but the MSVC STL
    # does not: USE_STD_HEADERS makes doctest include <ostream> itself.
    target_compile_definitions(dwe_test_main PUBLIC
        DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS DOCTEST_CONFIG_USE_STD_HEADERS)
    dwe_apply_options(dwe_test_main)

    # The guard of tests/main.cpp: an entry whose --test-case filter matches nothing fails, here a truncated name.
    add_executable(dwe_filter_guard_tests tests/filter_guard_tests.cpp)
    target_link_libraries(dwe_filter_guard_tests PRIVATE dwe_test_main)
    dwe_apply_options(dwe_filter_guard_tests)
    dwe_use_utf8(dwe_filter_guard_tests)
    set(guarded_case "the filter guard's test case runs")
    add_test(NAME "filter guard: a filter that matches a test case runs it"
        COMMAND dwe_filter_guard_tests "--test-case=${guarded_case}")
    # The guard's own exit code and message, not just any failure: a crash must not pass.
    add_test(NAME "filter guard: a filter that matches no test case fails"
        COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:dwe_filter_guard_tests>
            "-DARGUMENT=--test-case=the filter guard's test" -DEXIT_CODE=2 "-DMESSAGE=matches no test case"
            -P ${PROJECT_SOURCE_DIR}/cmake/expect_failure.cmake)
    add_test(NAME "filter guard: a catch-all entry may match no test case"
        COMMAND dwe_filter_guard_tests "--test-case=${guarded_case} in another name" --dwe-allow-no-match)

    # The decisions of scripts/gate.sh (docs/guides/gate.md), which only runs on a Linux host. -B keeps Python's
    # bytecode out of the sources.
    if(NOT WIN32)
        find_program(DWE_PYTHON3 python3 REQUIRED)
        set(gate_tests_dir ${PROJECT_SOURCE_DIR}/scripts/gate/tests)
        add_test(NAME "gate scripts: the scope of an incremental clang-tidy"
            COMMAND ${DWE_PYTHON3} -B -m unittest -v test_tidy_scope WORKING_DIRECTORY ${gate_tests_dir})
        add_test(NAME "gate scripts: the cache of cooked content"
            COMMAND ${DWE_PYTHON3} -B -m unittest -v test_cook_cache WORKING_DIRECTORY ${gate_tests_dir})
        find_program(DWE_BASH bash REQUIRED)
        add_test(NAME "gate scripts: only build folders of the worktree are deleted"
            COMMAND ${DWE_BASH} ${gate_tests_dir}/test_build_dirs.sh)
        add_test(NAME "gate scripts: a follow-up route runs only after its route, in the same job"
            COMMAND ${DWE_BASH} ${gate_tests_dir}/test_follow_up.sh)
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(formats)
add_subdirectory(apps)
