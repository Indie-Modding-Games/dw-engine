dwe_add_app(game
    LAYER apps
    SOURCES
        src/main.cpp
        src/test_scene.cpp
        src/ui_demo.cpp
    DEPS core platform rhi render input assets script ui debug
    OUTPUT_NAME dwe-game)

dwe_add_shaders(game FILES shaders/test_scene.slang)

# Until the asset pipeline exists, the scripts are read from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_GAME_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")

# The UI demo reads the team's packs (menus, HUD, fonts) from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_TEAM_PACKS="${PROJECT_SOURCE_DIR}/content/packs")
