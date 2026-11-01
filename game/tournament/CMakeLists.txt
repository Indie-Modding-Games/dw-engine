dwe_add_module(tournament
    LAYER game
    SOURCES
        src/arena.cpp
        src/bracket.cpp
        src/bracket_animation.cpp
        src/cup_data.cpp
        src/cup_extras.cpp
        src/match_show.cpp
        src/rules.cpp
        src/schedule.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_tournament_tests)
    target_compile_definitions(dwe_tournament_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
