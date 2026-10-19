find_package(SDL3 CONFIG REQUIRED)

dwe_add_module(platform
    LAYER base
    SOURCES
        src/platform.cpp
        src/window.cpp
    DEPS core
    PUBLIC_LIBS SDL3::SDL3)
