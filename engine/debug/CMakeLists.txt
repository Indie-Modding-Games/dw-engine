find_package(imgui CONFIG REQUIRED)

dwe_add_module(debug
    LAYER services
    SOURCES
        src/debug_ui.cpp
    DEPS core platform rhi input script
    PRIVATE_LIBS imgui::imgui)
