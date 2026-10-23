dwe_add_module(video
    LAYER systems
    SOURCES
        src/movie_player.cpp
        src/picture.cpp
        src/y4m_reader.cpp
    DEPS core assets)
