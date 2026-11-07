# The AV1 codec of the cooked movies (BSD-2-Clause, with the AOMedia patent licence): the decoder here, the encoder in
# dwx cook. Its debug build encodes and decodes far slower and nobody steps into it, so debug builds link
# its release build, as with lodepng. Not on Windows, where debug and release code use different C runtimes.
find_package(AOM CONFIG REQUIRED)
if(NOT WIN32)
    set_target_properties(AOM::aom PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

dwe_add_module(video
    LAYER systems
    SOURCES
        src/movie_player.cpp
        src/av1_reader.cpp
        src/picture.cpp
        src/y4m_reader.cpp
    DEPS core assets
    PRIVATE_LIBS AOM::aom)
