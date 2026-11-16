dwe_add_module(shop_job
    LAYER game
    SOURCES
        src/rules.cpp
        src/lineup.cpp
        src/haggle.cpp
    DEPS core assets ruleset)

# The tests read the game pack from the source tree.
if(TARGET dwe_shop_job_tests)
    target_compile_definitions(dwe_shop_job_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
