dwe_add_module(animation
    LAYER systems
    SOURCES
        src/clip_sampling.cpp
        src/animation_player.cpp
    DEPS core assets)

# The tests check the game pack's character models against the extracted ones.
if(BUILD_TESTING)
    target_compile_definitions(dwe_animation_tests PRIVATE DWE_GAME_PACK_DIR="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
