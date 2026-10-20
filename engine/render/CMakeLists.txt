# Only for the link order of tests/link_test.cpp.
find_package(vk-bootstrap CONFIG REQUIRED)

dwe_add_module(render
    LAYER systems
    SOURCES
        src/camera.cpp
        src/quad_batch.cpp
        src/renderer.cpp
    DEPS core rhi
    # tests/link_test.cpp: vk-bootstrap is loaded before assets, the link order that once made AddressSanitizer report
    # false container overflows.
    TEST_LIBS dwe::rhi vk-bootstrap::vk-bootstrap dwe::assets)

dwe_add_shaders(dwe_render FILES shaders/quad.slang)
