dwe_add_module(evolution
    LAYER game
    SOURCES
        src/rules.cpp
        src/tables.cpp
        src/strategies.cpp
        src/state.cpp
        src/evolution_effects.cpp
        src/evolution.cpp
        src/simulation.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_evolution_tests)
    target_compile_definitions(dwe_evolution_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
