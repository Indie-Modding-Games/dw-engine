find_package(RmlUi CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)

dwe_add_module(ui
    LAYER services
    SOURCES
        src/bitmap_font.cpp
        src/code_trust.cpp
        src/font_engine.cpp
        src/glyph_art.cpp
        src/image_reference.cpp
        src/minimap.cpp
        src/minimap_disc.cpp
        src/navigation.cpp
        src/original_box.cpp
        src/prompts.cpp
        src/render_bridge.cpp
        src/rml_interfaces.cpp
        src/scripting.cpp
        src/skin.cpp
        src/skin_catalog.cpp
        src/skin_styler.cpp
        src/sprite_sheet_rcss.cpp
        src/text_fit.cpp
        src/text_font.cpp
        src/ui.cpp
        src/ui_renderer.cpp
        src/world_map.cpp
    DEPS core platform input assets render script
    PRIVATE_LIBS RmlUi::RmlUi sol2::sol2 ${LUA_LIBRARIES}
    # The image tests write indexed PNGs and KTX2 textures.
    TEST_LIBS lodepng KTX::ktx)

dwe_add_shaders(dwe_ui FILES shaders/rml.slang)

target_include_directories(dwe_ui SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_compile_definitions(dwe_ui PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
if(TARGET dwe_ui_tests)
    # The tests load the team pack's real menus and font from the source tree.
    target_compile_definitions(dwe_ui_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
    # And the test skin a mod pack adds, which the skin route uses too.
    target_compile_definitions(dwe_ui_tests
        PRIVATE DWE_SKIN_TEST_PACK="${PROJECT_SOURCE_DIR}/tests/mods/ui_skin/test_contrast")
endif()
