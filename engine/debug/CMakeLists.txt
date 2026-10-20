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

# Saving a tuned section writes to a pack on disk, in the temporary folder of the assets tests.
if(TARGET dwe_debug_tests)
    target_include_directories(dwe_debug_tests PRIVATE ${PROJECT_SOURCE_DIR}/engine/assets/tests)
    target_compile_definitions(dwe_debug_tests PRIVATE DWE_GAME_PACK="${PROJECT_SOURCE_DIR}/content/packs/game")
endif()
