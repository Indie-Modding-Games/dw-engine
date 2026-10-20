find_package(EnTT CONFIG REQUIRED)

dwe_add_module(scene
    LAYER systems
    SOURCES
        src/components.cpp
        src/world.cpp
    DEPS core
    PUBLIC_LIBS EnTT::EnTT)
