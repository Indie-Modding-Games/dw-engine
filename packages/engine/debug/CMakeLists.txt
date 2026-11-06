find_package(imgui CONFIG REQUIRED)

dwe_add_module(debug
    LAYER services
    SOURCES
        src/debug_ui.cpp
        src/asset_viewer.cpp
        src/tuning_model.cpp
        src/tuning_panel.cpp
    DEPS core platform rhi input script assets ruleset
    PRIVATE_LIBS imgui::imgui)

# Saving a tuned section writes to a pack on disk, in a temporary folder (tests/support).
if(TARGET dwe_debug_tests)
    target_compile_definitions(dwe_debug_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
