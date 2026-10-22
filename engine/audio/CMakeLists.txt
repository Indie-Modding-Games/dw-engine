dwe_add_module(audio
    LAYER systems
    SOURCES
        src/audio.cpp
        src/clip.cpp
        src/mixer.cpp
        src/null_output.cpp
        src/sdl_output.cpp
        src/voice_player.cpp
    DEPS core platform)
