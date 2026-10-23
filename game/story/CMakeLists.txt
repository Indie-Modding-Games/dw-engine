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
    DEPS core assets)
