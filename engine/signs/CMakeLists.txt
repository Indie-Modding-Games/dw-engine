find_package(Freetype REQUIRED)

dwe_add_module(signs
    LAYER systems
    SOURCES
        src/map_signs.cpp
        src/sign_font.cpp
        src/sign_overlay.cpp
        src/sign_painter.cpp
    DEPS core assets
    PRIVATE_LIBS Freetype::Freetype)

# The tests draw with the game pack's Noto Sans from the source tree.
if(TARGET dwe_signs_tests)
    target_compile_definitions(dwe_signs_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
