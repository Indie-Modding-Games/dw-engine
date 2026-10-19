find_package(lodepng CONFIG REQUIRED)

# cgltf is two headers without a CMake package; src/model/cgltf.cpp compiles them once.
find_path(CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
add_library(dwe_cgltf INTERFACE)
target_include_directories(dwe_cgltf SYSTEM INTERFACE ${CGLTF_INCLUDE_DIR})

dwe_add_module(formats
    LAYER base
    SOURCES
        src/audio/adpcm.cpp
        src/audio/envelope.cpp
        src/audio/midi.cpp
        src/audio/seq.cpp
        src/audio/sf2.cpp
        src/audio/vab.cpp
        src/audio/vab_sf2.cpp
        src/audio/vhb.cpp
        src/audio/wav.cpp
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
        src/exe/overlay.cpp
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
        src/script/disassembly.cpp
        src/script/scn_file.cpp
        src/script/script_yaml.cpp
        src/text/charset.cpp
        src/text/font.cpp
        src/text/game_text.cpp
        src/text/string_manifest.cpp
    DEPS core
    PRIVATE_LIBS lodepng dwe_cgltf
    TEST_LIBS lodepng dwe_cgltf)

# lodepng compresses every image dwx writes, and its debug build is three times slower; nobody steps into it, so
# debug builds link its release build. Not on Windows, where debug and release code use different C runtimes.
if(NOT WIN32)
    set_target_properties(lodepng PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

# The tests check the YAML documents against the JSON Schemas of content/schemas.
if(TARGET dwe_formats_tests)
    target_compile_definitions(dwe_formats_tests PRIVATE DWE_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/content/schemas")
endif()
