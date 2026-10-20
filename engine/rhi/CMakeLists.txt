find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

dwe_add_module(rhi
    LAYER base
    SOURCES
        src/bindless.cpp
        src/buffer.cpp
        src/command_list.cpp
        src/device.cpp
        src/image.cpp
        src/pipeline.cpp
        src/texture.cpp
        src/upload_ring.cpp
        src/vma.cpp
        src/vulkan_util.cpp
    DEPS core platform
    PUBLIC_LIBS Vulkan::Vulkan
    PRIVATE_LIBS vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
