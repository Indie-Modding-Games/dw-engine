dwe_add_module(gym
    LAYER game
    SOURCES
        src/brain_learning.cpp
        src/course_brain.cpp
        src/course_defense.cpp
        src/course_flow.cpp
        src/course_mp.cpp
        src/course_offense.cpp
        src/course_speed.cpp
        src/slot_machine.cpp
        src/growing_box.cpp
        src/gym_rules.cpp
        src/stat_countup.cpp
    DEPS core assets ruleset)
