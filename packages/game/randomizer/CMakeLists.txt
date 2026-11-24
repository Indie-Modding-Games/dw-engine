dwe_add_module(randomizer
    LAYER game
    SOURCES
        src/run_settings.cpp
        src/randomizer.cpp
        src/rules.cpp
        src/map_enemies.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_randomizer_tests)
    target_compile_definitions(dwe_randomizer_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
