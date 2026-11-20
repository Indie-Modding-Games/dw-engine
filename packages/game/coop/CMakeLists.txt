dwe_add_module(coop
    LAYER game
    SOURCES
        src/battle.cpp
        src/authority.cpp
        src/profile_transfer.cpp
        src/link.cpp
        src/protocol.cpp
        src/sync_watch.cpp
        src/world_state.cpp
    DEPS core net)
