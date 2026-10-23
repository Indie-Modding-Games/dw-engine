dwe_add_module(gym
    LAYER game
    SOURCES
        src/gym_rules.cpp
        src/course_flow.cpp
        src/stat_countup.cpp
    DEPS core assets ruleset)
