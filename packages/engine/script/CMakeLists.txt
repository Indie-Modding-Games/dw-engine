find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

dwe_add_module(script
    LAYER services
    SOURCES
        src/address_free_text.cpp
        src/api_functions.cpp
        src/api_names.cpp
        src/api_registry.cpp
        src/budget.cpp
        src/budgeted_coroutines.cpp
        src/budgeted_error_handlers.cpp
        src/budgeted_strings.cpp
        src/budgeted_tables.cpp
        src/byte_charges.cpp
        src/command_bindings.cpp
        src/engine_code.cpp
        src/hook_registry.cpp
        src/input_bindings.cpp
        src/locked_tables.cpp
        src/lua_errors.cpp
        src/memory.cpp
        src/mod_environment.cpp
        src/mod_tables.cpp
        src/pack_logs.cpp
        src/pack_random.cpp
        src/pattern_matcher.cpp
        src/print_to_log.cpp
        src/script_assets.cpp
        src/script_coroutine.cpp
        src/script_vm.cpp
        src/string_patterns.cpp
        src/text_bindings.cpp
        src/vm_state.cpp
    DEPS core assets input localization
    PRIVATE_LIBS sol2::sol2 ${LUA_LIBRARIES})

target_include_directories(dwe_script SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_compile_definitions(dwe_script PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
if(TARGET dwe_script_tests)
    target_include_directories(dwe_script_tests SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
    target_compile_definitions(dwe_script_tests PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
    target_link_libraries(dwe_script_tests PRIVATE sol2::sol2 ${LUA_LIBRARIES})
endif()
