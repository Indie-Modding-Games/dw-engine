find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

dwe_add_module(script
    LAYER services
    SOURCES
        src/script_vm.cpp
        src/command_bindings.cpp
        src/input_bindings.cpp
        src/text_bindings.cpp
    DEPS core input localization
    PRIVATE_LIBS sol2::sol2 ${LUA_LIBRARIES})

target_include_directories(dwe_script SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_compile_definitions(dwe_script PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
if(TARGET dwe_script_tests)
    target_include_directories(dwe_script_tests SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
    target_compile_definitions(dwe_script_tests PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
    target_link_libraries(dwe_script_tests PRIVATE sol2::sol2 ${LUA_LIBRARIES})
endif()
