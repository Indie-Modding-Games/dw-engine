dwe_add_module(story
    LAYER game
    SOURCES
        src/story_state.cpp
        src/story_state_yaml.cpp
        src/story_orders.cpp
        src/story_runner.cpp
    DEPS core assets)
