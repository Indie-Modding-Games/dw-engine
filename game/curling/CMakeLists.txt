dwe_add_module(curling
    LAYER game
    SOURCES
        src/match.cpp
        src/physics.cpp
        src/rules.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_curling_tests)
    target_compile_definitions(dwe_curling_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
