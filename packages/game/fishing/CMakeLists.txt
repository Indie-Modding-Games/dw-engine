dwe_add_module(fishing
    LAYER game
    SOURCES
        src/rules.cpp
        src/pond.cpp
        src/rod.cpp
        src/view.cpp
        src/effects.cpp
        src/look.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_fishing_tests)
    target_compile_definitions(dwe_fishing_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
