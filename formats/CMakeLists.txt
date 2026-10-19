find_package(lodepng CONFIG REQUIRED)

# cgltf is two headers without a CMake package; src/model/cgltf.cpp compiles them once.
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
add_library(dwe_cgltf INTERFACE)
target_include_directories(dwe_cgltf SYSTEM INTERFACE ${CGLTF_INCLUDE_DIR})

dwe_add_module(formats
    LAYER base
    SOURCES
        src/disc/boot.cpp
        src/disc/cue_sheet.cpp
        src/disc/disc_image.cpp
        src/disc/file_table.cpp
        src/disc/iso9660.cpp
        src/disc/known_releases.cpp
        src/disc/sector.cpp
        src/exe/psx_exe.cpp
        src/exe/executable_layout.cpp
        src/exe/memory_image.cpp
        src/exe/tables.cpp
        src/exe/tables_yaml.cpp
        src/image/png.cpp
        src/image/tim.cpp
        src/image/tim_packs.cpp
        src/map/map_file.cpp
        src/map/map_yaml.cpp
        src/map/tfs_file.cpp
        src/model/animation.cpp
        src/model/cgltf.cpp
        src/model/gltf.cpp
        src/model/mmd.cpp
        src/model/skeleton.cpp
        src/model/tmd.cpp
    DEPS core
    PRIVATE_LIBS lodepng dwe_cgltf
    TEST_LIBS lodepng dwe_cgltf)

# The tests check the YAML documents against the JSON Schemas of content/schemas.
if(TARGET dwe_formats_tests)
    target_compile_definitions(dwe_formats_tests PRIVATE DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()
