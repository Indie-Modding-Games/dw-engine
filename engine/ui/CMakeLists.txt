find_package(RmlUi CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

dwe_add_module(ui
    LAYER services
    SOURCES
        src/bitmap_font.cpp
        src/font_engine.cpp
        src/minimap.cpp
        src/navigation.cpp
        src/prompts.cpp
        src/render_bridge.cpp
        src/rml_interfaces.cpp
        src/scripting.cpp
        src/text_font.cpp
        src/ui.cpp
        src/ui_renderer.cpp
    DEPS core platform input assets render script
    PRIVATE_LIBS RmlUi::RmlUi sol2::sol2 ${LUA_LIBRARIES})

dwe_add_shaders(dwe_ui FILES shaders/rml.slang)

target_include_directories(dwe_ui SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
target_compile_definitions(dwe_ui PRIVATE SOL_ALL_SAFETIES_ON=1 SOL_NO_EXCEPTIONS=1)
if(TARGET dwe_ui_tests)
    # The tests load the team pack's real menus and font from the source tree.
    target_compile_definitions(dwe_ui_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
