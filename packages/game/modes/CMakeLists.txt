dwe_add_module(modes
    LAYER game
    SOURCES
        src/game_mode.cpp
        src/mode_catalog.cpp
    DEPS core assets)

# The tests read the game pack from the source tree.
if(TARGET dwe_modes_tests)
    target_compile_definitions(dwe_modes_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
