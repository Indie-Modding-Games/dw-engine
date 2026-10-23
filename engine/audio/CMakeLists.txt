# The music synthesizer is one header from the repository's overlay port (vcpkg-ports/tinysoundfont).
find_path(TSF_INCLUDE_DIR tsf.h REQUIRED)

dwe_add_module(audio
    LAYER systems
    SOURCES
        src/audio.cpp
        src/clip.cpp
        src/effect_bank.cpp
        src/envelope.cpp
        src/mixer.cpp
        src/music_player.cpp
        src/null_output.cpp
        src/sdl_output.cpp
        src/sound_font.cpp
        src/tsf.cpp
        src/voice_player.cpp
    DEPS core platform assets)

target_include_directories(dwe_audio SYSTEM PRIVATE ${TSF_INCLUDE_DIR})
# The synthesizer reads SoundFonts from memory only, so it needs no file functions.
target_compile_definitions(dwe_audio PRIVATE TSF_NO_STDIO)
