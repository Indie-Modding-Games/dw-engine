find_package(SDL3 CONFIG REQUIRED)
# SDL3's package marks its headers as not being system headers, so their warnings would fail the build where they
# are hit, as in its MSVC code paths. They are a dependency like any other.
set_property(TARGET SDL3::Headers PROPERTY SYSTEM 1)

dwe_add_module(platform
    LAYER base
    SOURCES
        src/platform.cpp
        src/window.cpp
    DEPS core
    PUBLIC_LIBS SDL3::SDL3)
