# Only for the link order of tests/link_test.cpp.
find_package(vk-bootstrap CONFIG REQUIRED)

dwe_add_module(render
    LAYER systems
    SOURCES
        src/camera.cpp
        src/model_renderer.cpp
        src/quad_batch.cpp
        src/screen_fade.cpp
        src/renderer.cpp
        src/map_camera.cpp
        src/map_layers.cpp
        src/map_scene.cpp
        src/map_cutouts.cpp
        src/map_renderer.cpp
        src/scene_light.cpp
        src/scene_grade.cpp
        src/ground_shadows.cpp
    DEPS core rhi assets
    # tests/link_test.cpp: vk-bootstrap is loaded before assets, the link order that once made AddressSanitizer report
    # false container overflows.
    TEST_LIBS dwe::rhi vk-bootstrap::vk-bootstrap dwe::assets)

dwe_add_shaders(dwe_render FILES shaders/quad.slang shaders/map.slang shaders/skinned_model.slang
    shaders/scene_grade.slang)
