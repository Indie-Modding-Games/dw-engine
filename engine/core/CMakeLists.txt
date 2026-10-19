find_package(yaml CONFIG REQUIRED)

dwe_add_module(core
    LAYER core
    SOURCES
        src/log.cpp
        src/sha1.cpp
        src/json_schema.cpp
        src/schema_pattern.cpp
        src/yaml.cpp
    PRIVATE_LIBS yaml)

# The tools hash whole disc images, hundreds of MB. Unoptimised, SHA-1 runs at about 6 MB/s (24 MB/s even written
# without any function call), so this file is optimised in debug builds too; it keeps its debug info and sanitizers.
set_source_files_properties(src/sha1.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
