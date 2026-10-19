find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

dwe_add_module(rhi
    LAYER base
    SOURCES
        src/device.cpp
        src/texture.cpp
    DEPS core platform
    PUBLIC_LIBS Vulkan::Vulkan
    PRIVATE_LIBS vk-bootstrap::vk-bootstrap)
