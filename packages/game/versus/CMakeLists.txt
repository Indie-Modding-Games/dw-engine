dwe_add_module(versus
    LAYER game
    SOURCES
        src/link.cpp
        src/network.cpp
        src/protocol.cpp
        src/series.cpp
    DEPS core assets net)
