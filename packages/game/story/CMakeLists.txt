dwe_add_module(story
    LAYER game
    SOURCES
        src/story_state.cpp
        src/story_state_yaml.cpp
        src/story_orders.cpp
        src/orders_npc.cpp
        src/story_runner.cpp
        src/dialogue.cpp
        src/script_motion.cpp
        src/story_routines.cpp
        src/story_battle.cpp
        src/screen_history.cpp
        src/partner_routines.cpp
        src/new_day.cpp
        src/prosperity.cpp
        src/medals.cpp
        src/story_names.cpp
    DEPS core assets)

# The tests read the game pack from the source tree.
if(TARGET dwe_story_tests)
    target_compile_definitions(dwe_story_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
