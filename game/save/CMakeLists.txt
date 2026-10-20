dwe_add_module(save
    LAYER game
    SOURCES
        src/migration.cpp
        src/save_file.cpp
        src/sections.cpp
        src/storage.cpp
        src/settings.cpp
        src/autosave.cpp
        src/text_keys.cpp
        src/slots.cpp
    DEPS core assets ruleset)

# The tests read the game pack and the fixtures from the source tree.
if(TARGET dwe_save_tests)
    target_compile_definitions(dwe_save_tests PRIVATE
        DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game"
        DWE_SAVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
endif()
