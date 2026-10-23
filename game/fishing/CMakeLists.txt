dwe_add_module(fishing
    LAYER game
    SOURCES
        src/rules.cpp
        src/pond.cpp
        src/rod.cpp
    DEPS core assets ruleset)
