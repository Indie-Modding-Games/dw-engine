find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

dwe_add_module(rhi
    LAYER base
    SOURCES
        src/bindless.cpp
        src/buffer.cpp
        src/command_list.cpp
        src/device.cpp
        src/frame_capture.cpp
        src/gpu_diagnosis.cpp
        src/gpu_report.cpp
        src/image.cpp
        src/pipeline.cpp
        src/system_check.cpp
        src/system_facts.cpp
        src/texture.cpp
        src/upload_ring.cpp
        src/vma.cpp
        src/vulkan_calls.cpp
        src/vulkan_path.cpp
        src/vulkan_util.cpp
    DEPS core platform
    PUBLIC_LIBS Vulkan::Vulkan
    PRIVATE_LIBS vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(dwe_rhi PRIVATE user32 advapi32)
endif()
