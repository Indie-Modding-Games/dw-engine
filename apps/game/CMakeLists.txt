dwe_add_app(game
    LAYER apps
    SOURCES
        src/autotest.cpp
        src/common.cpp
        src/dev_views.cpp
        src/device_names.cpp
        src/field_scene.cpp
        src/frame_stats.cpp
        src/game.cpp
        src/language.cpp
        src/main.cpp
        src/map_preview.cpp
        src/model_viewer.cpp
        src/options.cpp
        src/saves.cpp
        src/test_scene.cpp
        src/ui_demo.cpp
        src/user_settings.cpp
    DEPS core platform rhi assets localization animation render input script ui debug scene ruleset field save content
    OUTPUT_NAME dwe-game)

dwe_add_shaders(game FILES shaders/test_scene.slang)

# Until the asset pipeline exists, the scripts are read from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_GAME_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")

# The UI demo reads the team's packs (menus, HUD, fonts) from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_TEAM_PACKS="${PROJECT_SOURCE_DIR}/content/packs")
# The game pack (models/*.yaml), which the model viewer mounts over the extracted content.
target_compile_definitions(game PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
