dwe_add_app(game
    LAYER apps
    SOURCES
        src/main.cpp
    DEPS core platform rhi input assets script debug
    OUTPUT_NAME dwe-game)

# Until the asset pipeline exists, the scripts are read from the source tree, so editing them reloads them live.
target_compile_definitions(game PRIVATE DWE_GAME_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
