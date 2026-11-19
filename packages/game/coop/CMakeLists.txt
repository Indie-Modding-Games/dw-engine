dwe_add_module(coop
    LAYER game
    SOURCES
        src/battle.cpp
        src/profile_transfer.cpp
        src/link.cpp
        src/protocol.cpp
    DEPS core net)
