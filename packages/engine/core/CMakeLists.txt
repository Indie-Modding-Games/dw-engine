find_package(yaml CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

dwe_add_module(core
    LAYER core
    SOURCES
        src/log.cpp
        src/original_math.cpp
        src/log_ring.cpp
        src/sha1.cpp
        src/json_schema.cpp
        src/schema_pattern.cpp
        src/yaml.cpp
        src/number_text.cpp
    PUBLIC_LIBS glm::glm
    PRIVATE_LIBS yaml)

# One GLM configuration for every module (see dwe/core/math.hpp): angles in radians and Vulkan's 0..1 clip depth.
target_compile_definitions(dwe_core PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

# The tools hash whole disc images, hundreds of MB. Unoptimised, SHA-1 runs at about 6 MB/s (24 MB/s even written
# without any function call), so this file is optimised in debug builds too; it keeps its debug info and sanitizers.
set_source_files_properties(src/sha1.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
