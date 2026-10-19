find_package(imgui CONFIG REQUIRED)

dwe_add_module(debug
    LAYER services
    SOURCES
        src/debug_ui.cpp
        src/asset_viewer.cpp
    DEPS core platform rhi input script assets
    PRIVATE_LIBS imgui::imgui)
