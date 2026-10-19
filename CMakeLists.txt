cmake_minimum_required(VERSION 3.30)

project(dwe VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(cmake/DweModule.cmake)

if(BUILD_TESTING)
    find_package(doctest CONFIG REQUIRED)
    add_library(dwe_test_main STATIC tests/main.cpp)
    target_link_libraries(dwe_test_main PUBLIC doctest::doctest)
    target_compile_definitions(dwe_test_main PUBLIC DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
    dwe_apply_options(dwe_test_main)
endif()

add_subdirectory(engine)
add_subdirectory(formats)
add_subdirectory(apps)
